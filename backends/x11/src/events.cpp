#include <x11_backend.hpp>

#include <core.hpp>
#include <log.hpp>
#include <runtime.hpp>
#include <xconn.hpp>

#include <xcb/xcb_keysyms.h>
#include <algorithm>
#include <string>
#include <vector>

namespace {

struct ConfigureRequestPatch {
    uint16_t              mask = 0;
    std::vector<uint32_t> values;
};

static ConfigureRequestPatch pack_configure_request(const xcb_configure_request_event_t* ev) {
    ConfigureRequestPatch patch;
    uint16_t              m = ev->value_mask;
    patch.values.reserve(7);

    auto put = [&](uint16_t flag, uint32_t value) {
            if (!(m & flag))
                return;
            patch.mask |= flag;
            patch.values.push_back(value);
        };

    put(XCB_CONFIG_WINDOW_X, static_cast<uint32_t>(ev->x));
    put(XCB_CONFIG_WINDOW_Y, static_cast<uint32_t>(ev->y));
    put(XCB_CONFIG_WINDOW_WIDTH, static_cast<uint32_t>(ev->width));
    put(XCB_CONFIG_WINDOW_HEIGHT, static_cast<uint32_t>(ev->height));
    put(XCB_CONFIG_WINDOW_BORDER_WIDTH, static_cast<uint32_t>(ev->border_width));
    put(XCB_CONFIG_WINDOW_SIBLING, static_cast<uint32_t>(ev->sibling));
    put(XCB_CONFIG_WINDOW_STACK_MODE, static_cast<uint32_t>(ev->stack_mode));

    return patch;
}

static void send_synthetic_configure_notify(XConnection& xconn,
    xcb_window_t win,
    int32_t x, int32_t y,
    uint32_t w, uint32_t h,
    uint32_t border_width) {
    xcb_configure_notify_event_t ce = {};
    ce.response_type     = XCB_CONFIGURE_NOTIFY;
    ce.event             = win;
    ce.window            = win;
    ce.x                 = (int16_t)x;
    ce.y                 = (int16_t)y;
    ce.width             = (uint16_t)w;
    ce.height            = (uint16_t)h;
    ce.border_width      = (uint16_t)border_width;
    ce.above_sibling     = XCB_WINDOW_NONE;
    ce.override_redirect = 0;
    xconn.send_event(win, XCB_EVENT_MASK_STRUCTURE_NOTIFY, (char*)&ce);
}

static event::ButtonEv make_button_ev(xcb_button_press_event_t* ev, bool release) {
    return {
        .window  = ev->event,
        .root    = ev->root,
        .root_x  = ev->root_x,
        .root_y  = ev->root_y,
        .event_x = ev->event_x,
        .event_y = ev->event_y,
        .time    = ev->time,
        .button  = ev->detail,
        .state   = ev->state,
        .release = release,
    };
}

static bool is_override_redirect_window(XConnection& xconn, xcb_window_t win) {
    auto attrs = xconn.get_window_attributes(win);
    return attrs.valid && attrs.override_redirect;
}

static bool has_atom(const std::vector<xcb_atom_t>& atoms, xcb_atom_t needle) {
    if (needle == XCB_ATOM_NONE)
        return false;
    return std::find(atoms.begin(), atoms.end(), needle) != atoms.end();
}

struct WindowTypeAtoms {
    xcb_atom_t net_wm_window_type = XCB_ATOM_NONE;
    xcb_atom_t dialog             = XCB_ATOM_NONE;
    xcb_atom_t utility            = XCB_ATOM_NONE;
    xcb_atom_t splash             = XCB_ATOM_NONE;
    xcb_atom_t modal              = XCB_ATOM_NONE;
};

static const WindowTypeAtoms& window_type_atoms(XConnection& xconn) {
    static const WindowTypeAtoms atoms = [&xconn]() {
            auto m = xconn.intern_atoms({
                "_NET_WM_WINDOW_TYPE",
                "_NET_WM_WINDOW_TYPE_DIALOG",
                "_NET_WM_WINDOW_TYPE_UTILITY",
                "_NET_WM_WINDOW_TYPE_SPLASH",
                "_NET_WM_WINDOW_TYPE_MODAL",
            });
            WindowTypeAtoms out;
            out.net_wm_window_type = m["_NET_WM_WINDOW_TYPE"];
            out.dialog             = m["_NET_WM_WINDOW_TYPE_DIALOG"];
            out.utility            = m["_NET_WM_WINDOW_TYPE_UTILITY"];
            out.splash             = m["_NET_WM_WINDOW_TYPE_SPLASH"];
            out.modal              = m["_NET_WM_WINDOW_TYPE_MODAL"];
            return out;
        }();
    return atoms;
}

struct WindowMetadata {
    std::string  wm_instance;
    std::string  wm_class;
    WindowType   type              = WindowType::Normal;
    bool         wm_fixed_size     = false;
    bool         wm_never_focus    = false;
    bool         wm_static_gravity = false;
    bool         wm_no_decorations = false;
    // Geometry facts for intent classification in core.
    bool         covers_monitor       = false;
    bool         pre_fullscreen_state = false;
    bool         is_xembed            = false;
    // Relationship facts.
    WindowId     transient_for        = NO_WINDOW;
};

static WindowMetadata read_window_metadata(XConnection& xconn, WindowId window) {
    WindowMetadata out;
    auto [instance, cls] = xconn.get_wm_class(window);
    out.wm_instance      = std::move(instance);
    out.wm_class         = std::move(cls);

    const auto& atoms = window_type_atoms(xconn);
    auto        types = xconn.get_atom_list_property(window, atoms.net_wm_window_type);
    if (has_atom(types, atoms.modal))        out.type = WindowType::Modal;
    else if (has_atom(types, atoms.dialog))  out.type = WindowType::Dialog;
    else if (has_atom(types, atoms.utility)) out.type = WindowType::Utility;
    else if (has_atom(types, atoms.splash))  out.type = WindowType::Splash;
    out.wm_fixed_size     = xconn.has_fixed_size_hints(window);
    out.wm_never_focus    = xconn.get_wm_hints_no_input(window);
    out.wm_static_gravity = xconn.has_static_gravity(window);
    out.wm_no_decorations = xconn.motif_no_decorations(window);
    return out;
}

static void apply_window_metadata(Core& core, WindowId window, WindowMetadata meta) {
    (void)core.dispatch(command::SetWindowMetadata{
            .window               = window,
            .wm_instance          = std::move(meta.wm_instance),
            .wm_class             = std::move(meta.wm_class),
            .type                 = meta.type,
            .wm_fixed_size        = meta.wm_fixed_size,
            .wm_never_focus       = meta.wm_never_focus,
            .preserve_position    = meta.wm_static_gravity,
            .wm_no_decorations    = meta.wm_no_decorations,
            .covers_monitor       = meta.covers_monitor,
            .pre_fullscreen_state = meta.pre_fullscreen_state,
            .is_xembed            = meta.is_xembed,
            .transient_for        = meta.transient_for,
        });
}


static int monitor_for_visible_workspace(const Core& core, int ws_id) {
    if (ws_id < 0)
        return -1;
    const auto& mons = core.monitor_states();
    for (int i = 0; i < (int)mons.size(); i++)
        if (mons[i].active_ws == ws_id)
            return i;
    return -1;
}

static void place_window_on_monitor(Core& core,
    XConnection& xconn,
    WindowId window,
    const Monitor& mon,
    bool prefer_center) {
    if (window == NO_WINDOW || mon.width <= 0 || mon.height <= 0)
        return;

    auto geo = xconn.get_window_geometry(window);
    if (!geo)
        return;

    int gx = geo->x;
    int gy = geo->y;
    int gw = std::max<int>(1, geo->width);
    int gh = std::max<int>(1, geo->height);

    int nw = std::min(gw, mon.width);
    int nh = std::min(gh, mon.height);

    int nx = gx;
    int ny = gy;
    if (prefer_center) {
        nx = mon.x + (mon.width - nw) / 2;
        ny = mon.y + (mon.height - nh) / 2;
    } else {
        nx = std::clamp(nx, mon.x, mon.x + mon.width - nw);
        ny = std::clamp(ny, mon.y, mon.y + mon.height - nh);
    }

    bool crosses = (gx < mon.x) || (gy < mon.y) ||
        (gx + gw > mon.x + mon.width) ||
        (gy + gh > mon.y + mon.height);
    bool resized = (nw != gw) || (nh != gh);
    bool moved   = (nx != gx) || (ny != gy);
    if (!crosses && !resized && !moved)
        return;

    (void)core.dispatch(command::SetWindowGeometry{ window, nx, ny, (uint32_t)nw, (uint32_t)nh });
    core.update_window(window);
}

} // namespace

void X11Backend::handle_map_request(xcb_map_request_event_t* ev) {
    if (is_override_redirect_window(xconn, ev->window)) {
        xconn.call(xcb_map_window, ev->window);
        LOG_DEBUG("MapRequest(%d): override-redirect, mapping unmanaged", ev->window);
        return;
    }

    event::ManageWindowQuery map_q{ ev->window, true };
    handle(map_q);
    if (map_q.manage)
        runtime.query(core, map_q, [](const event::ManageWindowQuery& s) {
                return !s.manage;
            });
    if (!map_q.manage) {
        xconn.call(xcb_map_window, ev->window);
        LOG_DEBUG("MapRequest(%d): unmanaged, mapping as-is", ev->window);
        return;
    }

    (void)core.dispatch(command::EnsureWindow{
        .window = ev->window,
    });
    {
        auto meta = read_window_metadata(xconn, ev->window);

        // Collect geometry facts for intent classification in core.
        auto geo = xconn.get_window_geometry(ev->window);
        if (geo) {
            const auto& mons    = core.monitor_states();
            int         top     = std::max(0, core.monitor_top_inset());
            int         bottom  = std::max(0, core.monitor_bottom_inset());
            int         outer_w = (int)(geo->width  + 2 * geo->border_width);
            int         outer_h = (int)(geo->height + 2 * geo->border_width);
            for (const auto& mon : mons) {
                if (outer_w >= mon.width && outer_h >= mon.height - top - bottom) {
                    meta.covers_monitor = true;
                    break;
                }
            }
        }
        if (ewmh_has_fullscreen_state(ev->window)) {
            meta.pre_fullscreen_state = true;
            xcb_atom_t xembed_atom = xconn.intern_atom_reply(
                xconn.intern_atom_async("_XEMBED_INFO", sizeof("_XEMBED_INFO") - 1));
            meta.is_xembed = xconn.has_property_32(ev->window, xembed_atom, 2);
        }

        meta.transient_for = xconn.get_transient_for_window(ev->window).value_or(XCB_WINDOW_NONE);

        LOG_INFO("MapRequest(%d): class='%s' no_decos=%d preserve_pos=%d fixed=%d pre_fs=%d covers=%d transient=%d",
            ev->window, meta.wm_class.c_str(),
            (int)meta.wm_no_decorations, (int)meta.wm_static_gravity,
            (int)meta.wm_fixed_size, (int)meta.pre_fullscreen_state,
            (int)meta.covers_monitor, (int)(meta.transient_for != NO_WINDOW));
        apply_window_metadata(core, ev->window, std::move(meta));
    }

    {
        uint32_t mask = XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_ENTER_WINDOW
                      | XCB_EVENT_MASK_FOCUS_CHANGE | XCB_EVENT_MASK_PROPERTY_CHANGE;
        xconn.change_window_attributes(ev->window, XCB_CW_EVENT_MASK, &mask);
    }

    // Rules apply only to non-transient windows; transients are routed in SetWindowMetadata.
    xcb_window_t transient_for = xconn.get_transient_for_window(ev->window).value_or(XCB_WINDOW_NONE);
    bool managed_transient = (transient_for != XCB_WINDOW_NONE) &&
                             (core.workspace_of_window(transient_for) >= 0);
    if (!managed_transient)
        runtime.emit(core, event::ApplyWindowRules{ ev->window });

    auto mapped_window = core.window_state_any(ev->window);
    int  ws_id         = core.workspace_of_window(ev->window);
    bool ws_visible    = (ws_id >= 0) && core.is_workspace_visible(ws_id);

    // Core classified this window as needing borderless treatment at map time.
    if (mapped_window && mapped_window->promote_to_borderless &&
        !mapped_window->borderless && !mapped_window->floating) {
        int         mon_idx   = core.monitor_of_workspace(ws_id);
        const auto& mons      = core.monitor_states();
        if (mon_idx >= 0 && mon_idx < (int)mons.size()) {
            const auto& mon       = mons[(size_t)mon_idx];
            int         top       = std::max(0, core.monitor_top_inset());
            int         bottom    = std::max(0, core.monitor_bottom_inset());
            int         phy_y     = mon.y - top;
            int         phy_h     = mon.height + top + bottom;
            LOG_INFO("MapRequest(%d): %s, promoting to borderless at %d,%d %dx%d",
                ev->window,
                mapped_window->self_managed ? "self-managed (Wine/Proton)" : "borderless",
                mon.x, phy_y, mon.width, phy_h);
            (void)core.dispatch(command::SetWindowBorderless{ ev->window, true });
            (void)core.dispatch(command::SetWindowBorderWidth{ ev->window, 0 });
            // Self-managed: client controls its own geometry, do not override.
            if (!mapped_window->self_managed) {
                (void)core.dispatch(command::SetWindowGeometry{
                    ev->window, mon.x, phy_y, (uint32_t)mon.width, (uint32_t)phy_h });
            }
            runtime.emit(core, event::RaiseDocks{});
            mapped_window = core.window_state_any(ev->window);
        }
    } else if (mapped_window && mapped_window->borderless &&
               !mapped_window->self_managed) {
        // Re-map of an already-borderless window: repin geometry to prevent drift.
        int         mon_idx = core.monitor_of_workspace(ws_id);
        const auto& mons    = core.monitor_states();
        if (mon_idx >= 0 && mon_idx < (int)mons.size()) {
            const auto& mon    = mons[(size_t)mon_idx];
            int         top    = std::max(0, core.monitor_top_inset());
            int         bottom = std::max(0, core.monitor_bottom_inset());
            int         phy_y  = mon.y - top;
            int         phy_h  = mon.height + top + bottom;
            LOG_DEBUG("MapRequest(%d): re-map of borderless, repinning geometry at %d,%d %dx%d",
                ev->window, mon.x, phy_y, mon.width, phy_h);
            (void)core.dispatch(command::SetWindowGeometry{
                ev->window, mon.x, phy_y, (uint32_t)mon.width, (uint32_t)phy_h });
            mapped_window = core.window_state_any(ev->window);
        }
    }

    if (mapped_window && ws_visible && mapped_window->floating) {
        int target_mon = -1;

        // Use transient parent's monitor for placement if available.
        xcb_window_t transient_for = xconn.get_transient_for_window(ev->window)
            .value_or(XCB_WINDOW_NONE);
        if (transient_for != XCB_WINDOW_NONE) {
            int parent_ws = core.workspace_of_window(transient_for);
            target_mon = monitor_for_visible_workspace(core, parent_ws);
        }

        if (target_mon < 0)
            target_mon = monitor_for_visible_workspace(core, ws_id);

        if (target_mon < 0)
            target_mon = core.focused_monitor_index();

        const auto& mons = core.monitor_states();
        if (target_mon >= 0 && target_mon < (int)mons.size()) {
            bool center = (mapped_window && mapped_window->is_dialog()) || (mapped_window && mapped_window->wm_fixed_size);
            place_window_on_monitor(core, xconn, ev->window, mons[target_mon], center);
        }
    }

    if (ws_visible) {
        (void)core.dispatch(command::SetWindowHiddenByWorkspace{ ev->window, false });
        (void)core.dispatch(command::MapWindow{ ev->window });
    } else {
        (void)core.dispatch(command::SetWindowHiddenByWorkspace{ ev->window, true });
        (void)core.dispatch(command::UnmapWindow{ ev->window });
    }

    runtime.emit(core, event::WindowMapped{ ev->window });
    notify(event::WindowMapped{ ev->window });

    (void)core.dispatch(command::ReconcileNow{});

    bool suppress_focus     = core.consume_window_suppress_focus_once(ev->window);

    bool focus_new_window   = core.current_settings().focus_new_window;
    bool force_dialog_focus = mapped_window && mapped_window->is_dialog();
    if ((focus_new_window || force_dialog_focus) &&
        !suppress_focus &&
        ws_visible &&
        mapped_window &&
        mapped_window->is_visible()) {
        (void)core.dispatch(command::FocusWindow{ ev->window });
        focus_window(ev->window);
        core.emit_focus_changed(ev->window);
    } else {
        restore_visible_focus();
    }

    LOG_DEBUG("MapRequest(%d): parent %d", ev->window, ev->parent);
}

void X11Backend::handle_map_notify(xcb_map_notify_event_t* ev) {
    if (ev->window == root_window)
        return;

    // A pending WM unmap means the MapNotify is from our own map_window call
    // immediately before an unmap (workspace visibility sync race). Do not clear
    // hidden_by_workspace — the window is about to be unmapped again.
    bool pending_wm_unmap = pending_wm_unmaps_.count(ev->window) > 0 &&
                            pending_wm_unmaps_.at(ev->window) > 0;

    (void)core.dispatch(command::SetWindowMapped{ ev->window, !pending_wm_unmap });
    if (!pending_wm_unmap) {
        // Only clear hidden_by_workspace if the window's workspace is currently active.
        // If the window mapped on an inactive workspace (e.g. rule-routed), the WM will
        // immediately unmap it again; clearing here would leave mapped=true,
        // hidden_by_workspace=false with no xcb_map_window pending for the next switch.
        int ws_id  = core.workspace_of_window(ev->window);
        bool ws_active = ws_id >= 0 && core.is_workspace_visible(ws_id);
        if (ws_active)
            (void)core.dispatch(command::SetWindowHiddenByWorkspace{ ev->window, false });
    }

    // Override-redirect windows (menus, tooltips) must appear above bars, not below.
    if (!ev->override_redirect)
        runtime.emit(core, event::RaiseDocks{});

    LOG_DEBUG("MapNotify(%d): parent %d%s", ev->window, ev->event,
        pending_wm_unmap ? " [pending unmap, keeping hidden]" : "");
}

void X11Backend::handle_reparent_notify(xcb_reparent_notify_event_t* ev) {
    if (ev->window == root_window)
        return;

    if (!core.window_state_any(ev->window)) {
        xcb_atom_t xembed_info_atom = xconn.intern_atom_reply(
            xconn.intern_atom_async("_XEMBED_INFO", sizeof("_XEMBED_INFO") - 1));
        bool       has_xembed       = xconn.has_property_32(ev->window, xembed_info_atom, 2);
        // Emit TrayIconDocked only when reparented to root (MANAGER broadcast).
        // Reparents to non-root are either our own transfers or initial docks.
        if (has_xembed && ev->parent == root_window) {
            LOG_DEBUG("ReparentNotify(%u): xembed icon returned to root, re-adopting", ev->window);
            runtime.emit(core, event::TrayIconDocked{ ev->window });
        } else {
            LOG_DEBUG("ReparentNotify(%u): parent %u (unmanaged, xembed=%d)", ev->window, ev->parent,
                has_xembed ? 1 : 0);
        }
        return;
    }

    if (ev->parent == root_window) {
        LOG_DEBUG("ReparentNotify(%u): parent %u (managed -> root)", ev->window, ev->parent);
        return;
    }

    LOG_DEBUG("ReparentNotify(%d): parent %d (defer unmanage)", ev->window, ev->parent);
}

void X11Backend::handle_unmap_notify(xcb_unmap_notify_event_t* ev) {
    if (ev->window == root_window)
        return;

    int  ws_id      = core.workspace_of_window(ev->window);
    bool ws_visible = (ws_id >= 0) && core.is_workspace_visible(ws_id);

    if (!core.window_state_any(ev->window)) {
        runtime.emit(core, event::WindowUnmapped{ ev->window, /*withdrawn=*/ true });
        notify(event::WindowUnmapped{ ev->window, /*withdrawn=*/ true });
        return;
    }

    // WM-initiated unmap (workspace visibility sync): not a client withdrawal.
    if (consume_wm_unmap(ev->window)) {
        LOG_DEBUG("UnmapNotify(%d): WM-initiated, ignoring", ev->window);
        notify(event::WindowUnmapped{ ev->window, /*withdrawn=*/ false });
        return;
    }

    bool ws_hidden_unmap = core.is_window_hidden_by_workspace(ev->window);
    (void)core.dispatch(command::SetWindowMapped{ ev->window, false });

    if (ws_hidden_unmap) {
        runtime.emit(core, event::WindowUnmapped{ ev->window, /*withdrawn=*/ false });
        notify(event::WindowUnmapped{ ev->window, /*withdrawn=*/ false });
        return;
    }

    runtime.emit(core, event::WindowUnmapped{ ev->window, /*withdrawn=*/ true });
    notify(event::WindowUnmapped{ ev->window, /*withdrawn=*/ true });
    if (ws_visible) {
        (void)core.dispatch(command::ReconcileNow{});
        restore_visible_focus();
    }

    LOG_DEBUG("UnmapNotify(%d): parent %d", ev->window, ev->event);
}

void X11Backend::handle_destroy_notify(xcb_destroy_notify_event_t* ev) {
    if (!core.window_state_any(ev->window)) {
        runtime.emit(core, event::DestroyNotify{ ev->window });
        LOG_DEBUG("DestroyNotify(%d): unmanaged", ev->window);
        return;
    }

    int  ws_id      = core.workspace_of_window(ev->window);
    bool ws_visible = (ws_id >= 0) && core.is_workspace_visible(ws_id);

    runtime.emit(core, event::DestroyNotify{ ev->window });
    (void)core.dispatch(command::RemoveWindowFromAllWorkspaces{ ev->window });
    runtime.emit(core, event::WindowUnmapped{ ev->window, /*withdrawn=*/ true });
    notify(event::WindowUnmapped{ ev->window, /*withdrawn=*/ true });

    if (ws_visible) {
        (void)core.dispatch(command::ReconcileNow{});
        restore_visible_focus();
    }

    LOG_DEBUG("DestroyNotify(%d)", ev->window);
}

void X11Backend::handle_property_notify(xcb_property_notify_event_t* ev) {
    auto window = core.window_state_any(ev->window);
    if (window) {
        const auto&       atoms        = window_type_atoms(xconn);
        static xcb_atom_t motif_atom   = xconn.intern_atoms({"_MOTIF_WM_HINTS"})["_MOTIF_WM_HINTS"];
        bool              refresh_meta =
            ev->atom == XCB_ATOM_WM_CLASS ||
            ev->atom == atoms.net_wm_window_type ||
            ev->atom == XCB_ATOM_WM_NORMAL_HINTS ||
            ev->atom == motif_atom;
        if (refresh_meta) {
            auto meta = read_window_metadata(xconn, ev->window);
            // Geometry facts are set once at MapRequest; preserve them on property refresh.
            meta.pre_fullscreen_state = window->self_managed;
            meta.covers_monitor       = window->self_managed || window->borderless;
            if (ev->atom == motif_atom) {
                LOG_INFO("PropertyNotify(%d): _MOTIF_WM_HINTS changed, no_decos=%d",
                    ev->window, (int)meta.wm_no_decorations);
                // Promote to borderless if MOTIF says no decorations and we haven't yet.
                if (meta.wm_no_decorations && !window->borderless) {
                    if (window->self_managed) {
                        // Self-managed: mark borderless only, do not override geometry.
                        LOG_INFO("PropertyNotify(%d): MOTIF no-decorations (self-managed), marking borderless",
                            ev->window);
                        (void)core.dispatch(command::SetWindowBorderless{ ev->window, true });
                        (void)core.dispatch(command::SetWindowBorderWidth{ ev->window, 0 });
                        runtime.emit(core, event::RaiseDocks{});
                        (void)core.dispatch(command::ReconcileNow{});
                    } else {
                        int         mon_idx = core.monitor_of_workspace(core.workspace_of_window(ev->window));
                        const auto& mons    = core.monitor_states();
                        if (mon_idx >= 0 && mon_idx < (int)mons.size()) {
                            const auto& mon    = mons[(size_t)mon_idx];
                            // mon.y/height are inset-adjusted; recover physical extents.
                            int         top    = std::max(0, core.monitor_top_inset());
                            int         bottom = std::max(0, core.monitor_bottom_inset());
                            int         phy_y  = mon.y - top;
                            int         phy_h  = mon.height + top + bottom;
                            LOG_INFO("PropertyNotify(%d): MOTIF no-decorations, promoting to borderless at %d,%d %dx%d",
                                ev->window, mon.x, phy_y, mon.width, phy_h);
                            (void)core.dispatch(command::SetWindowBorderless{ ev->window, true });
                            (void)core.dispatch(command::SetWindowBorderWidth{ ev->window, 0 });
                            (void)core.dispatch(command::SetWindowGeometry{
                                ev->window, mon.x, phy_y,
                                (uint32_t)mon.width, (uint32_t)phy_h });
                            runtime.emit(core, event::RaiseDocks{});
                            (void)core.dispatch(command::ReconcileNow{});
                        }
                    }
                }
            }
            apply_window_metadata(core, ev->window, std::move(meta));
        }

        if (ev->atom == XCB_ATOM_WM_TRANSIENT_FOR && !window->floating) {
            xcb_window_t trans = xconn.get_transient_for_window(ev->window).value_or(XCB_WINDOW_NONE);
            if (trans != XCB_WINDOW_NONE && core.window_state_any(trans)) {
                int parent_ws = core.workspace_of_window(trans);
                if (parent_ws >= 0) {
                    (void)core.dispatch(command::SetWindowSuppressFocusOnce{ ev->window, true });
                    (void)core.dispatch(command::MoveWindowToWorkspace{ ev->window, parent_ws });
                }
                (void)core.dispatch(command::SetWindowFloating{ ev->window, true });

                int  ws_id      = core.workspace_of_window(ev->window);
                bool ws_visible = (ws_id >= 0) && core.is_workspace_visible(ws_id);
                if (ws_visible) {
                    int target_mon = monitor_for_visible_workspace(core, core.workspace_of_window(trans));
                    if (target_mon < 0)
                        target_mon = monitor_for_visible_workspace(core, ws_id);
                    if (target_mon < 0)
                        target_mon = core.focused_monitor_index();

                    const auto& mons = core.monitor_states();
                    if (target_mon >= 0 && target_mon < (int)mons.size())
                        place_window_on_monitor(core, xconn, ev->window, mons[target_mon], true);
                }
                (void)core.dispatch(command::ReconcileNow{});
            }
        }

    }
    runtime.emit(core, event::PropertyNotify{ ev->window, ev->atom });
}

void X11Backend::handle_configure_request(xcb_configure_request_event_t* ev) {
    auto window = core.window_state_any(ev->window);

    if (!window) {
        auto msg = pack_configure_request(ev);
        LOG_DEBUG("ConfigureRequest from unknown window %d, redirecting", ev->window);
        xconn.call(xcb_configure_window, ev->window, msg.mask, msg.values.data());
        return;
    }

    uint16_t m = ev->value_mask;

    // Floating and borderless clients honour their own ConfigureRequest geometry.
    // StaticGravity clients self-position (inner-origin coords, border_width pre-subtracted).
    // Tiled clients receive a synthetic ConfigureNotify with current WM-assigned geometry.
    bool floating       = core.is_window_floating(ev->window);
    bool borderless     = window->borderless;
    bool static_gravity = window->preserve_position;

    if (m & XCB_CONFIG_WINDOW_BORDER_WIDTH)
        (void)core.dispatch(command::SetWindowBorderWidth{ ev->window, ev->border_width });
    // Reject restack requests from fullscreen windows — raise docks instead.
    // Restack (sibling/stack_mode) is X11-specific and applied directly without routing through core.
    if (m & XCB_CONFIG_WINDOW_STACK_MODE) {
        if (window->fullscreen) {
            runtime.emit(core, event::RaiseDocks{});
        } else {
            uint16_t restack_mask = XCB_CONFIG_WINDOW_STACK_MODE;
            uint32_t restack_vals[2] = {};
            int      ri = 0;
            if (m & XCB_CONFIG_WINDOW_SIBLING) {
                restack_mask    |= XCB_CONFIG_WINDOW_SIBLING;
                restack_vals[ri++] = ev->sibling;
            }
            restack_vals[ri] = ev->stack_mode;
            xconn.configure_window(ev->window, restack_mask, restack_vals);
        }
    }

    // A tiled window requesting monitor-covering geometry is going fullscreen
    // without EWMH. Promote to borderless so the layout engine skips it.
    bool borderless_fs = false;
    if (!floating && !borderless && !static_gravity &&
        (m & (XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT))) {
        int         mon_idx = core.monitor_of_workspace(core.workspace_of_window(ev->window));
        const auto& mons    = core.monitor_states();
        if (mon_idx >= 0 && mon_idx < (int)mons.size()) {
            const auto& mon      = mons[(size_t)mon_idx];
            // SDL2 subtracts bar insets from its requested size; compare against usable area.
            int         usable_h = mon.height
                - std::max(0, core.monitor_top_inset())
                - std::max(0, core.monitor_bottom_inset());
            if ((int)ev->width >= mon.width && (int)ev->height >= usable_h) {
                LOG_INFO(
                    "ConfigureRequest(%d): borderless fullscreen detected (%dx%d >= %dx%d), promoting to borderless",
                    ev->window, ev->width, ev->height, mon.width, usable_h);
                (void)core.dispatch(command::SetWindowBorderless{ ev->window, true });
                borderless    = true;
                borderless_fs = true;
                // Override to full monitor area (client requested reduced size due to _NET_WM_STRUT).
                ev->x         = (int16_t)mon.x;
                ev->y         = (int16_t)mon.y;
                ev->width     = (uint16_t)mon.width;
                ev->height    = (uint16_t)mon.height;
                m            |= XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y
                    | XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT;
                runtime.emit(core, event::RaiseDocks{});
            }
        }
    }

    if (floating || borderless || static_gravity) {
        // WM-pinned windows (fullscreen, WM-placed borderless): reject geometry requests.
        bool pinned = (window->fullscreen && !borderless_fs) ||
            (window->borderless && (window->wm_no_decorations || window->wm_fixed_size) &&
                !borderless_fs && !window->self_managed);
        if (pinned) {
            send_synthetic_configure_notify(xconn, ev->window,
                window->x, window->y,
                window->width, window->height,
                window->border_width);
            return;
        }

        int32_t  nx = window->x;
        int32_t  ny = window->y;
        uint32_t nw = window->width;
        uint32_t nh = window->height;

        if (m & XCB_CONFIG_WINDOW_X)      nx = ev->x;
        if (m & XCB_CONFIG_WINDOW_Y)      ny = ev->y;
        if (m & XCB_CONFIG_WINDOW_WIDTH)  nw = ev->width;
        if (m & XCB_CONFIG_WINDOW_HEIGHT) nh = ev->height;

        // ICCCM §4.1.2.3 StaticGravity: client provides inner-origin coords
        // (content position), but the WM stores outer-corner coords.
        // Subtract border_width to convert to outer-corner.
        uint32_t out_bw = (m & XCB_CONFIG_WINDOW_BORDER_WIDTH)
            ? (uint32_t)ev->border_width : window->border_width;
        if (static_gravity && out_bw > 0) {
            nx -= (int32_t)out_bw;
            ny -= (int32_t)out_bw;
        }

        // Fixed-size windows: clamp position to keep them on their monitor.
        // Skip for static_gravity/borderless — client may intentionally position off-edge.
        if (!borderless_fs && window->wm_fixed_size && !window->preserve_position) {
            int         mon_idx = core.monitor_of_workspace(core.workspace_of_window(ev->window));
            const auto& mons    = core.monitor_states();
            if (mon_idx >= 0 && mon_idx < (int)mons.size()) {
                const auto& mon   = mons[(size_t)mon_idx];
                int         max_x = mon.x + mon.width  - (int)nw;
                int         max_y = mon.y + mon.height - (int)nh;
                nx = std::clamp(nx, mon.x, std::max(mon.x, max_x));
                ny = std::clamp(ny, mon.y, std::max(mon.y, max_y));
            }
        }

        (void)core.dispatch(command::SetWindowGeometry{ ev->window, nx, ny, nw, nh });
        if (borderless_fs)
            (void)core.dispatch(command::ReconcileNow{});

        // ICCCM §4.1.5: send synthetic ConfigureNotify after accepting a request.
        // For StaticGravity, report inner-origin coords (outer + bw) back to client.
        {
            int32_t rx = nx;
            int32_t ry = ny;
            if (static_gravity && out_bw > 0) {
                rx += (int32_t)out_bw;
                ry += (int32_t)out_bw;
            }
            send_synthetic_configure_notify(xconn, ev->window, rx, ry, nw, nh, out_bw);
        }
    } else {
        send_synthetic_configure_notify(xconn, ev->window,
            window->x, window->y,
            window->width, window->height,
            window->border_width);
    }

    LOG_DEBUG("ConfigureRequest(%d): x:%d y:%d w:%d h:%d bw:%d",
        ev->window, ev->x, ev->y, ev->width, ev->height, ev->border_width);
}

void X11Backend::handle_configure_notify(xcb_configure_notify_event_t* ev) {
    // Always emit — tray needs ConfigureNotify to track icon resize.
    runtime.emit(core, event::ConfigureNotify{ ev->window, ev->x, ev->y, ev->width, ev->height });

    auto window = core.window_state_any(ev->window);
    if (!window || ev->override_redirect)
        return;

    (void)core.dispatch(command::SyncWindowFromConfigureNotify{
        ev->window,
        ev->x, ev->y,
        ev->width, ev->height,
        ev->border_width
    });

    LOG_DEBUG("ConfigureNotify(%d): x:%d y:%d w:%d h:%d bw:%d",
        ev->window, ev->x, ev->y, ev->width, ev->height, ev->border_width);
}

void X11Backend::handle_key_event(xcb_key_press_event_t* ev) {
    // Sync focused monitor to pointer so workspace bindings act on the right monitor.
    core.focus_monitor_at_point(ev->root_x, ev->root_y);

    if ((ev->response_type & ~0x80) == XCB_KEY_RELEASE) {
        xcb_generic_event_t* next = xconn.poll();
        if (next) {
            uint8_t next_type = next->response_type & ~0x80;
            bool    is_repeat = (next_type == XCB_KEY_PRESS) &&
                (reinterpret_cast<xcb_key_press_event_t*>(next)->detail == ev->detail) &&
                (reinterpret_cast<xcb_key_press_event_t*>(next)->time  == ev->time);

            if (!is_repeat)
                key_down[ev->detail] = false;

            // Continue normal event processing (repeat keypress will be ignored
            // because key_down is still true when is_repeat == true).
            handle_generic_event(next);
            free(next);
            return;
        }

        key_down[ev->detail] = false;
        return;
    }

    // Fire keybindings once per physical press. Repeats are filtered by:
    // - keeping key_down[code] set between initial press and real release
    // - swallowing synthetic repeat release+press pairs in KEY_RELEASE handler
    if (key_down[ev->detail])
        return;

    key_down[ev->detail] = true;
    uint32_t keysym = 0;
    if (auto* syms = key_symbols())
        keysym = xcb_key_symbols_get_keysym(syms, ev->detail, 0);
    runtime.emit(core, event::KeyPressEv{ ev->state, ev->detail, keysym });
}

void X11Backend::handle_focus_event(xcb_focus_in_event_t* ev) {
    uint8_t type = ev->response_type & ~0x80;
    if (type == XCB_FOCUS_OUT) {
        // Defensive reset: if we lose focus/grab during a chord, release events
        // may be missed and keys could stay "stuck" in key_down.
        key_down.fill(false);
        return;
    }

    if (ev->event == root_window)
        return;

    // dwm-style focusin: if a window stole focus from our selection via an
    // indirect/synthetic route (NotifyWhileGrabbed, NotifyPointerRoot, etc.),
    // reassert focus. Only act on NotifyNormal/NotifyWhileGrabbed and only
    // when the event is not from a pointer crossing (detail != NotifyInferior).
    // Do NOT reassert for NotifyPointer/NotifyVirtual — those are legitimate
    // focus changes initiated by us or the user.
    auto sel = core.focused_window_state();
    if (sel && ev->event != sel->id &&
        (ev->detail != XCB_NOTIFY_DETAIL_POINTER &&
        ev->detail != XCB_NOTIFY_DETAIL_POINTER_ROOT &&
        ev->detail != XCB_NOTIFY_DETAIL_NONE) &&
        ev->mode == XCB_NOTIFY_MODE_WHILE_GRABBED) {
        xconn.focus_window(sel->id);
        return;
    }

    auto window = core.window_state_any(ev->event);
    if (!window || !window->is_visible())
        return;

    // Sync internal focus state only — do NOT call xconn.focus_window() here.
    // Calling xcb_set_input_focus in response to a FocusIn event creates a
    // ping-pong loop: our set_input_focus → X sends FocusOut(A)+FocusIn(B) →
    // we call set_input_focus again → FocusOut(B)+FocusIn(A) → ... This
    // causes thousands of FocusIn/FocusOut events per second and makes the
    // focused application (e.g. VSCode with multiple managed child windows)
    // freeze: it keeps receiving FocusIn/FocusOut and cannot process input.
    // The actual X focus has already been set by whichever path triggered this
    // FocusIn (EnterNotify, button press, EWMH, keybinding). Here we only need
    // to keep the WM's internal focused-window pointer in sync.
    (void)core.dispatch(command::FocusWindow{ ev->event });
}

void X11Backend::handle_button_event(xcb_button_press_event_t* ev) {
    if ((ev->response_type & ~0x80) == XCB_BUTTON_RELEASE) {
        runtime.emit(core, make_button_ev(ev, true));
        return;
    }

    core.focus_monitor_at_point(ev->root_x, ev->root_y);
    runtime.emit(core, make_button_ev(ev, false));
}

void X11Backend::handle_motion_notify(xcb_motion_notify_event_t* ev) {
    runtime.emit(core, event::MotionEv{ ev->event, ev->root_x, ev->root_y, ev->state });
}

void X11Backend::handle_client_message(xcb_client_message_event_t* ev) {
    event::ClientMessageEv msg {
        .window = ev->window,
        .type   = ev->type,
        .format = ev->format,
        .data   = {
            ev->data.data32[0], ev->data.data32[1], ev->data.data32[2],
            ev->data.data32[3], ev->data.data32[4],
        },
    };
    if (handle(msg))
        return;
    bool handled = runtime.emit_until_handled(core, msg);
    if (!handled)
        LOG_DEBUG("ClientMessage(%d): unhandled type=%u", ev->window, ev->type);
}

void X11Backend::handle_expose(xcb_expose_event_t* ev) {
    runtime.emit(core, event::ExposeWindow{ ev->window });
}

void X11Backend::handle_no_exposure(xcb_no_exposure_event_t*) {}

void X11Backend::handle_graphics_exposure(xcb_graphics_exposure_event_t*) {}

void X11Backend::handle_create_notify(xcb_create_notify_event_t*) {}

void X11Backend::handle_ge_generic(xcb_ge_generic_event_t* ev) {
    int base = runtime.get_backend_extension_event_base();
    if (base < 0)
        return;

    uint8_t type = ev->response_type & ~0x80;
    if (type == base + XCB_RANDR_SCREEN_CHANGE_NOTIFY)
        runtime.dispatch_display_change();
}

void X11Backend::handle_enter_notify(xcb_enter_notify_event_t* ev) {
    if ((ev->response_type & ~0x80) == XCB_LEAVE_NOTIFY)
        return;

    // Filter only NotifyInferior: pointer moved into a child — not a real
    // window-to-window crossing, would cause spurious re-focus on the same window.
    // All other detail values (Normal, Nonlinear, Virtual, NonlinearVirtual) are
    // treated as real crossings:
    // - Virtual/NonlinearVirtual: delivered for the managed parent when the pointer
    //   enters a window with unmanaged children (Electron apps like VSCode/Vesktop)
    //   or crosses monitor boundaries. These are the only events we get for such
    //   windows — filtering them breaks focus-follows-mouse entirely for them.
    // The old ping-pong risk is gone — handle_focus_event no longer calls
    // xconn.focus_window(), so there is no re-entrant focus loop from Virtual events.
    if (ev->mode != XCB_NOTIFY_MODE_NORMAL ||
        ev->detail == XCB_NOTIFY_DETAIL_INFERIOR)
        return;

    last_event_time_ = ev->time;

    // Use window_state_any so that windows on the second monitor's active
    // workspace are found even when focused_monitor hasn't been updated yet
    // (focused_monitor is only updated on button press / motion, not on enter).
    auto window = core.window_state_any(ev->event);
    if (!window || !window->is_visible())
        return;

    // Keep focused_monitor in sync so subsequent workspace/layout ops target
    // the correct monitor without requiring a click first.
    core.focus_monitor_at_point(ev->root_x, ev->root_y);

    (void)core.dispatch(command::FocusWindow{ ev->event });
    // Defer X focus + FocusChanged until after apply_core_backend_effects() so
    // stale backend effects cannot overwrite pointer-driven focus or _NET_WM_STATE_FOCUSED.
    pending_enter_focus_ = ev->event;
}

void X11Backend::handle_generic_event(xcb_generic_event_t* ev) {
    uint8_t type = ev->response_type & ~0x80;

    if (type == 0) {
        auto* err = reinterpret_cast<xcb_generic_error_t*>(ev);
        LOG_WARN("X11 error: code=%u major=%u minor=%u resource=%u seq=%u",
            err->error_code, err->major_code, err->minor_code,
            err->resource_id, err->sequence);
        return;
    }

    switch (type) {
        case XCB_MAP_REQUEST:      handle_map_request((xcb_map_request_event_t*)ev); break;
        case XCB_MAP_NOTIFY:       handle_map_notify((xcb_map_notify_event_t*)ev); break;
        case XCB_REPARENT_NOTIFY:  handle_reparent_notify((xcb_reparent_notify_event_t*)ev); break;
        case XCB_UNMAP_NOTIFY:     handle_unmap_notify((xcb_unmap_notify_event_t*)ev); break;
        case XCB_DESTROY_NOTIFY:   handle_destroy_notify((xcb_destroy_notify_event_t*)ev); break;
        case XCB_CONFIGURE_REQUEST: handle_configure_request((xcb_configure_request_event_t*)ev); break;
        case XCB_CONFIGURE_NOTIFY:  handle_configure_notify((xcb_configure_notify_event_t*)ev); break;
        case XCB_KEY_PRESS:
        case XCB_KEY_RELEASE:      handle_key_event((xcb_key_press_event_t*)ev); break;
        case XCB_FOCUS_IN:
        case XCB_FOCUS_OUT:        handle_focus_event((xcb_focus_in_event_t*)ev); break;
        case XCB_BUTTON_PRESS:
        case XCB_BUTTON_RELEASE:   handle_button_event((xcb_button_press_event_t*)ev); break;
        case XCB_MOTION_NOTIFY:    handle_motion_notify((xcb_motion_notify_event_t*)ev); break;
        case XCB_CLIENT_MESSAGE:   handle_client_message((xcb_client_message_event_t*)ev); break;
        case XCB_PROPERTY_NOTIFY:  handle_property_notify((xcb_property_notify_event_t*)ev); break;
        case XCB_EXPOSE:           handle_expose((xcb_expose_event_t*)ev); break;
        case XCB_NO_EXPOSURE:      handle_no_exposure((xcb_no_exposure_event_t*)ev); break;
        case XCB_GRAPHICS_EXPOSURE: handle_graphics_exposure((xcb_graphics_exposure_event_t*)ev); break;
        case XCB_CREATE_NOTIFY:    handle_create_notify((xcb_create_notify_event_t*)ev); break;
        case XCB_GE_GENERIC:       handle_ge_generic((xcb_ge_generic_event_t*)ev); break;
        case XCB_ENTER_NOTIFY:
        case XCB_LEAVE_NOTIFY:     handle_enter_notify((xcb_enter_notify_event_t*)ev); break;

        default: {
            int randr_base = runtime.get_backend_extension_event_base();
            if (randr_base >= 0 && (type == randr_base + XCB_RANDR_SCREEN_CHANGE_NOTIFY ||
                type == randr_base + XCB_RANDR_NOTIFY)) {
                runtime.dispatch_display_change();
                break;
            }
            int xkb_base = xconn.xkb_event_type();
            if (xkb_base >= 0 && type == (uint8_t)xkb_base) {
                // XKB state notify — group (layout) may have changed.
                auto* kp = keyboard_port_impl.get();
                if (kp) {
                    std::string layout = kp->current_layout();
                    runtime.emit(core, event::KeyboardLayoutChanged{ layout });
                }
                break;
            }
            LOG_DEBUG("No case for %d", type);
            break;
        }
    }
}