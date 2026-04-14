#include <x11_backend.hpp>
#include <x11_atoms.hpp>

#include <core.hpp>
#include <log.hpp>
#include <protocol/keyboard.hpp>
#include <protocol/system_tray.hpp>
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

static event::ButtonEv make_button_ev(xcb_button_press_event_t* ev, bool release) {
    return {
        .window    = ev->event,
        .root      = ev->root,
        .root_pos  = { ev->root_x, ev->root_y },
        .event_pos = { ev->event_x, ev->event_y },
        .time      = ev->time,
        .button    = ev->detail,
        .state     = ev->state,
        .release   = release,
    };
}

static bool is_override_redirect_window(XConnection& xconn, xcb_window_t win) {
    auto attrs = xconn.get_window_attributes(win);
    return attrs.valid && attrs.override_redirect;
}

struct WindowMetadata {
    std::string wm_instance;
    std::string wm_class;
    WindowType  type              = WindowType::Normal;
    bool        wm_fixed_size     = false;
    bool        wm_never_focus    = false;
    bool        wm_urgent         = false;
    bool        wm_static_gravity = false;
    Vec2i       size_min, size_max, size_inc, size_base;
    bool        wm_no_decorations = false;
    // Geometry facts for intent classification in core.
    bool        covers_monitor       = false;
    bool        pre_fullscreen_state = false;
    bool        is_xembed            = false;
    // Relationship facts.
    WindowId    transient_for = NO_WINDOW;
    std::string title;
    uint32_t    pid = 0;
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
    auto sz_hints = xconn.get_size_hints(window);
    out.wm_fixed_size = sz_hints.fixed;
    out.size_min      = { sz_hints.min_w, sz_hints.min_h };
    out.size_max      = { sz_hints.max_w, sz_hints.max_h };
    out.size_inc      = { sz_hints.inc_w, sz_hints.inc_h };
    out.size_base     = { sz_hints.base_w, sz_hints.base_h };
    auto wm_hints = xconn.get_wm_hints(window);
    out.wm_never_focus    = wm_hints.no_input;
    out.wm_urgent         = wm_hints.urgent;
    out.wm_static_gravity = xconn.has_static_gravity(window);
    out.wm_no_decorations = xconn.motif_no_decorations(window);

    static const auto named       = xconn.intern_atoms({ "_NET_WM_NAME", "UTF8_STRING", "_NET_WM_PID" });
    static xcb_atom_t net_wm_name = named.at("_NET_WM_NAME");
    static xcb_atom_t utf8_string = named.at("UTF8_STRING");
    static xcb_atom_t net_wm_pid  = named.at("_NET_WM_PID");
    out.title = xconn.get_text_property(window, net_wm_name, utf8_string);
    if (out.title.empty())
        out.title = xconn.get_text_property(window, XCB_ATOM_WM_NAME, XCB_ATOM_STRING);
    if (net_wm_pid != XCB_ATOM_NONE) {
        auto vals = xconn.get_property_u32(window, net_wm_pid, 1);
        if (!vals.empty())
            out.pid = vals[0];
    }
    return out;
}

static void apply_window_metadata(Core& core, WindowId window, WindowMetadata meta) {
    (void)core.dispatch(command::atom::SetWindowMetadata{
            .window             = window,
            .wm_instance        = std::move(meta.wm_instance),
            .wm_class           = std::move(meta.wm_class),
            .title              = std::move(meta.title),
            .pid                = meta.pid,
            .type               = meta.type,
            .hints              = {
                .no_decorations = meta.wm_no_decorations,
                .fixed_size     = meta.wm_fixed_size,
                .never_focus    = meta.wm_never_focus,
                .urgent         = meta.wm_urgent,
                .static_gravity = meta.wm_static_gravity,
                .covers_monitor = meta.covers_monitor,
                .pre_fullscreen = meta.pre_fullscreen_state,
                .is_xembed      = meta.is_xembed,
                .size_min       = meta.size_min,
                .size_max       = meta.size_max,
                .size_inc       = meta.size_inc,
                .size_base      = meta.size_base,
            },
            .transient_for = meta.transient_for,
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
    if (window == NO_WINDOW || mon.size().x() <= 0 || mon.size().y() <= 0)
        return;

    auto geo = xconn.get_window_geometry(window);
    if (!geo)
        return;

    int gx = geo->x;
    int gy = geo->y;
    int gw = std::max<int>(1, geo->width);
    int gh = std::max<int>(1, geo->height);

    int nw = std::min(gw, mon.size().x());
    int nh = std::min(gh, mon.size().y());

    int nx = gx;
    int ny = gy;
    if (prefer_center) {
        nx = mon.pos().x() + (mon.size().x() - nw) / 2;
        ny = mon.pos().y() + (mon.size().y() - nh) / 2;
    } else {
        nx = std::clamp(nx, mon.pos().x(), mon.pos().x() + mon.size().x() - nw);
        ny = std::clamp(ny, mon.pos().y(), mon.pos().y() + mon.size().y() - nh);
    }

    bool crosses = (gx < mon.pos().x()) || (gy < mon.pos().y()) ||
        (gx + gw > mon.pos().x() + mon.size().x()) ||
        (gy + gh > mon.pos().y() + mon.size().y());
    bool resized = (nw != gw) || (nh != gh);
    bool moved   = (nx != gx) || (ny != gy);
    if (!crosses && !resized && !moved)
        return;

    (void)core.dispatch(command::atom::SetWindowGeometry{ window, { nx, ny }, { nw, nh } });
    core.update_window(window);
}

} // namespace

void X11Backend::handle_map_request(xcb_map_request_event_t* ev) {
    if (is_override_redirect_window(xconn, ev->window)) {
        xconn.map_window(ev->window);
        LOG_DEBUG("MapRequest(%d): override-redirect, mapping unmanaged", ev->window);
        return;
    }

    if (!runtime.invoke_hook(hook::ShouldManageWindow{ ev->window }).manage) {
        xconn.map_window(ev->window);
        LOG_DEBUG("MapRequest(%d): unmanaged, mapping as-is", ev->window);
        return;
    }

    {
        auto meta = read_window_metadata(xconn, ev->window);

        // Collect geometry facts for intent classification in core.
        auto geo = xconn.get_window_geometry(ev->window);
        if (geo) {
            const auto& mons    = core.monitor_states();
            int         outer_w = (int)(geo->width  + 2 * geo->border_width);
            int         outer_h = (int)(geo->height + 2 * geo->border_width);
            for (const auto& mon : mons) {
                int top = std::max(0, mon.top_inset());
                int bot = std::max(0, mon.bottom_inset());
                if (outer_w >= mon.size().x() && outer_h >= mon.size().y() - top - bot) {
                    meta.covers_monitor = true;
                    break;
                }
            }
        }
        // Pre-manage: no X11Window exists yet, query X properties directly.
        {
            auto states = xconn.get_atom_list_property(ev->window, NET_WM_STATE);
            if (std::find(states.begin(), states.end(), NET_WM_STATE_FULLSCREEN) != states.end()) {
                meta.pre_fullscreen_state = true;
                xcb_atom_t xembed_atom = xconn.intern_atom_reply(
                    xconn.intern_atom_async("_XEMBED_INFO", sizeof("_XEMBED_INFO") - 1));
                meta.is_xembed = xconn.has_property_32(ev->window, xembed_atom, 2);
            }
        }

        meta.transient_for = xconn.get_transient_for(ev->window).value_or(XCB_WINDOW_NONE);

        // For fullscreen/borderless windows, use the first ConfigureRequest position
        // to determine the target workspace. Wine/Proton (and some native games) send
        // real monitor coordinates in the first ConfigureRequest before MapRequest;
        // later ConfigureRequests may carry wrong values.
        // Applies to any window that covers a monitor (pre_fullscreen or covers_monitor),
        // not only Wine self-managed ones.
        int ws_id_hint = -1;
        if (meta.pre_fullscreen_state || meta.covers_monitor) {
            auto it = first_configure_pos_.find(ev->window);
            if (it != first_configure_pos_.end()) {
                int x = it->second.x(), y = it->second.y();
                // Use the window center for workspace lookup instead of
                // the top-left corner.  Wine/Proton subtract border_width
                // from the origin (NorthWest gravity), which can push the
                // corner 1px into an adjacent monitor.  The center of a
                // fullscreen window always lands inside the correct monitor.
                if (geo) {
                    x += (int)(geo->width  / 2);
                    y += (int)(geo->height / 2);
                }
                if (!(x == -32000 && y == -32000))
                    ws_id_hint = core.active_workspace_at_point(x, y);
                LOG_DEBUG("MapRequest(%d): first_cfg_pos=%d,%d ws_hint=%d", ev->window, x, y, ws_id_hint);
            }
        }
        first_configure_pos_.erase(ev->window);

        (void)core.dispatch(command::atom::EnsureWindow{
            .window       = ev->window,
            .workspace_id = ws_id_hint,
        });

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
    xcb_window_t transient_for     = xconn.get_transient_for(ev->window).value_or(XCB_WINDOW_NONE);
    bool         managed_transient = (transient_for != XCB_WINDOW_NONE) &&
        (core.workspace_of_window(transient_for) >= 0);
    if (!managed_transient) {
        // Rules must run before we read mapped_window->floating / borderless
        // below: the Lua handler toggles those flags via siren.win.set_floating()
        // etc., and we need the updated state to decide placement. Hooks are
        // synchronous by design — subscribers run inline and the caller sees
        // the mutated core state on return.
        runtime.invoke_hook(hook::WindowRules{ ev->window });
    }

    auto mapped_window = core.window_state_any(ev->window);
    int  ws_id         = core.workspace_of_window(ev->window);
    bool ws_visible    = (ws_id >= 0) && core.is_workspace_visible(ws_id);

    // Core classified this window as needing borderless treatment at map time.
    if (mapped_window && mapped_window->promote_to_borderless &&
        !mapped_window->borderless && !mapped_window->floating) {
        int         mon_idx = core.monitor_of_workspace(ws_id);
        const auto& mons    = core.monitor_states();
        if (mon_idx >= 0 && mon_idx < (int)mons.size()) {
            auto [phy_pos, phy_size] = mons[(size_t)mon_idx].physical();
            LOG_INFO("MapRequest(%d): %s, promoting to borderless at %d,%d %dx%d",
                ev->window,
                mapped_window->self_managed ? "self-managed (Wine/Proton)" : "borderless",
                phy_pos.x(), phy_pos.y(), phy_size.x(), phy_size.y());
            (void)core.dispatch(command::atom::SetWindowBorderless{ ev->window, true });
            if (!mapped_window->self_managed) {
                // Non-self-managed: WM controls geometry — zero the border and
                // pin to physical monitor bounds.
                (void)core.dispatch(command::atom::SetWindowBorderWidth{ ev->window, 0 });
                if (auto* xw_bw = x11_window(ev->window)) {
                    uint32_t zero_bw = 0;
                    xw_bw->configure(XCB_CONFIG_WINDOW_BORDER_WIDTH, &zero_bw);
                }
                (void)core.dispatch(command::atom::SetWindowGeometry{
                    ev->window, phy_pos, phy_size });
            }
            // Self-managed (Wine/Proton): do NOT touch the X border.  Wine
            // positions its render child at (outer + border_width); zeroing the
            // border shifts the inner origin and causes a 1px render offset.
            mapped_window = core.window_state_any(ev->window);
        }
    } else if (mapped_window && mapped_window->borderless &&
        !mapped_window->self_managed) {
        // Re-map of an already-borderless window: repin geometry to prevent drift.
        int         mon_idx = core.monitor_of_workspace(ws_id);
        const auto& mons    = core.monitor_states();
        if (mon_idx >= 0 && mon_idx < (int)mons.size()) {
            auto [phy_pos, phy_size] = mons[(size_t)mon_idx].physical();
            LOG_DEBUG("MapRequest(%d): re-map of borderless, repinning geometry at %d,%d %dx%d",
                ev->window, phy_pos.x(), phy_pos.y(), phy_size.x(), phy_size.y());
            (void)core.dispatch(command::atom::SetWindowGeometry{
                ev->window, phy_pos, phy_size });
            mapped_window = core.window_state_any(ev->window);
        }
    }

    if (mapped_window && mapped_window->floating) {
        int target_mon = -1;

        // Use transient parent's monitor for placement if available.
        xcb_window_t transient_for = xconn.get_transient_for(ev->window)
                .value_or(XCB_WINDOW_NONE);
        if (transient_for != XCB_WINDOW_NONE) {
            int parent_ws = core.workspace_of_window(transient_for);
            target_mon = monitor_for_visible_workspace(core, parent_ws);
        }

        // For windows on invisible workspaces, use the owning monitor.
        if (target_mon < 0 && !ws_visible) {
            target_mon = core.monitor_of_workspace(ws_id);
        }

        if (target_mon < 0)
            target_mon = monitor_for_visible_workspace(core, ws_id);

        if (target_mon < 0)
            target_mon = core.focused_monitor_index();

        const auto& mons = core.monitor_states();
        if (target_mon >= 0 && target_mon < (int)mons.size()) {
            bool center = (mapped_window && mapped_window->is_dialog()) || (mapped_window && mapped_window->size_locked);
            place_window_on_monitor(core, xconn, ev->window, mons[target_mon], center);
        }
    }

    if (ws_visible) {
        (void)core.dispatch(command::atom::SetWindowHiddenByWorkspace{ ev->window, false });
        (void)core.dispatch(command::atom::MapWindow{ ev->window });
    } else {
        // Map briefly so the client receives MapNotify and initialises its
        // renderer, then set IconicState and unmap.  Without the initial map,
        // GPU clients (Steam, Electron) never start and appear frozen.
        // Move off-screen first to avoid a visible flash on the current workspace.
        if (auto* xw = x11_window(ev->window)) {
            uint32_t off[1] = { (uint32_t)-32000 };
            xw->configure(XCB_CONFIG_WINDOW_X, off);
            xw->map();
            ewmh_on_window_unmapped(event::WindowUnmapped{ ev->window, /*withdrawn=*/ false }); // WM_STATE → IconicState
            xw->note_wm_unmap();
            xw->unmap();
        }
        (void)core.dispatch(command::atom::SetWindowHiddenByWorkspace{ ev->window, true });
        (void)core.dispatch(command::atom::SetWindowMapped{ ev->window, false });
    }

    ewmh_on_window_mapped(event::WindowMapped{ ev->window });
    runtime.post_event(event::WindowMapped{ ev->window });

    (void)core.dispatch(command::atom::ReconcileNow{});

    bool suppress_focus = core.consume_window_suppress_focus_once(ev->window);

    if (!suppress_focus &&
        ws_visible &&
        mapped_window &&
        mapped_window->is_visible()) {
        (void)core.dispatch(command::atom::FocusWindow{ ev->window });
        request_focus(ev->window, kFocusEWMH);
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
    auto* xw               = x11_window(ev->window);
    bool  pending_wm_unmap = xw && xw->pending_wm_unmaps > 0;

    (void)core.dispatch(command::atom::SetWindowMapped{ ev->window, !pending_wm_unmap });
    if (!pending_wm_unmap) {
        // Only clear hidden_by_workspace if the window's workspace is currently active.
        // If the window mapped on an inactive workspace (e.g. rule-routed), the WM will
        // immediately unmap it again; clearing here would leave mapped=true,
        // hidden_by_workspace=false with no xcb_map_window pending for the next switch.
        int  ws_id     = core.workspace_of_window(ev->window);
        bool ws_active = ws_id >= 0 && core.is_workspace_visible(ws_id);
        if (ws_active)
            (void)core.dispatch(command::atom::SetWindowHiddenByWorkspace{ ev->window, false });
    }

    // Override-redirect windows (menus, tooltips) must appear above bars, not below.
    if (!ev->override_redirect)
        runtime.post_event(event::RaiseDocks{});

    LOG_DEBUG("MapNotify(%d): parent %d%s", ev->window, ev->event,
        pending_wm_unmap ? " [pending unmap, keeping hidden]" : "");
}

void X11Backend::handle_reparent_notify(xcb_reparent_notify_event_t* ev) {
    if (ev->window == root_window)
        return;

    if (!core.window_state_any(ev->window)) {
        xcb_atom_t xembed_info_atom = xconn.intern_atom_reply(
            xconn.intern_atom_async("_XEMBED_INFO", sizeof("_XEMBED_INFO") - 1));
        bool       has_xembed = xconn.has_property_32(ev->window, xembed_info_atom, 2);
        // Publish via protocol only when reparented to root (MANAGER broadcast).
        // Reparents to non-root are either our own transfers or initial docks.
        if (has_xembed && ev->parent == root_window) {
            LOG_DEBUG("ReparentNotify(%u): xembed icon returned to root, re-adopting", ev->window);
            runtime.post_event(event::CustomEvent{
                MessageEnvelope::pack(protocol::system_tray::IconDocked{ ev->window })
            });
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
        ewmh_on_window_unmapped(event::WindowUnmapped{ ev->window, /*withdrawn=*/ true });
        runtime.post_event(event::WindowUnmapped{ ev->window, /*withdrawn=*/ true });
        return;
    }

    // WM-initiated unmap (workspace visibility sync): not a client withdrawal.
    auto* xw_unmap = x11_window(ev->window);
    if (xw_unmap && xw_unmap->consume_wm_unmap()) {
        LOG_DEBUG("UnmapNotify(%d): WM-initiated, ignoring", ev->window);
        ewmh_on_window_unmapped(event::WindowUnmapped{ ev->window, /*withdrawn=*/ false });
        return;
    }

    auto win_state       = core.window_state_any(ev->window);
    bool ws_hidden_unmap = win_state && win_state->hidden_by_workspace;
    bool was_borderless  = win_state && win_state->borderless;
    bool was_fullscreen  = win_state && win_state->fullscreen;
    (void)core.dispatch(command::atom::SetWindowMapped{ ev->window, false });

    // Borderless/fullscreen windows (games, media viewers) that unmap themselves
    // are fully withdrawn from WM management. On the next MapRequest the WM will
    // re-adopt them cleanly, preventing stale workspace state from remapping a
    // client-closed overlay on the next workspace switch.
    if (was_borderless || was_fullscreen) {
        first_configure_pos_.erase(ev->window);
        (void)core.dispatch(command::atom::RemoveWindowFromAllWorkspaces{ ev->window });
        ewmh_on_window_unmapped(event::WindowUnmapped{ ev->window, /*withdrawn=*/ true });
        runtime.post_event(event::WindowUnmapped{ ev->window, /*withdrawn=*/ true });
        if (ws_visible) {
            (void)core.dispatch(command::atom::ReconcileNow{});
            restore_visible_focus();
        }
        LOG_DEBUG("UnmapNotify(%d): borderless client withdrawal, unmanaging", ev->window);
        return;
    }

    if (ws_hidden_unmap) {
        ewmh_on_window_unmapped(event::WindowUnmapped{ ev->window, /*withdrawn=*/ false });
        runtime.post_event(event::WindowUnmapped{ ev->window, /*withdrawn=*/ false });
        return;
    }

    // Client-initiated unmap on a visible workspace — full withdrawal.
    first_configure_pos_.erase(ev->window);
    (void)core.dispatch(command::atom::RemoveWindowFromAllWorkspaces{ ev->window });
    ewmh_on_window_unmapped(event::WindowUnmapped{ ev->window, /*withdrawn=*/ true });
    runtime.post_event(event::WindowUnmapped{ ev->window, /*withdrawn=*/ true });
    if (ws_visible) {
        (void)core.dispatch(command::atom::ReconcileNow{});
        restore_visible_focus();
    }

    LOG_DEBUG("UnmapNotify(%d): client withdrawal, unmanaging", ev->window);
}

void X11Backend::handle_destroy_notify(xcb_destroy_notify_event_t* ev) {
    first_configure_pos_.erase(ev->window);

    // Clear border cache so update_focus won't paint a dead window.
    if (border_painted_focused_ == ev->window)
        border_painted_focused_ = NO_WINDOW;

    if (!core.window_state_any(ev->window)) {
        runtime.post_event(event::DestroyNotify{ ev->window });
        LOG_DEBUG("DestroyNotify(%d): unmanaged", ev->window);
        return;
    }

    int  ws_id      = core.workspace_of_window(ev->window);
    bool ws_visible = (ws_id >= 0) && core.is_workspace_visible(ws_id);

    runtime.post_event(event::DestroyNotify{ ev->window });
    // Remove from core FIRST — this destroys the X11Window object,
    // so no further backend operations can target this window.
    (void)core.dispatch(command::atom::RemoveWindowFromAllWorkspaces{ ev->window });

    // Skip EWMH property updates on the destroyed window — any ChangeProperty
    // would produce a BadWindow error. Only update the client list on root.
    runtime.post_event(event::WindowUnmapped{ ev->window, /*withdrawn=*/ true });
    ewmh_update_client_list();

    if (ws_visible) {
        (void)core.dispatch(command::atom::ReconcileNow{});
        restore_visible_focus();
    }

    LOG_DEBUG("DestroyNotify(%d)", ev->window);
}

void X11Backend::handle_property_notify(xcb_property_notify_event_t* ev) {
    auto window = core.window_state_any(ev->window);
    if (window) {
        const auto&       atoms            = window_type_atoms(xconn);
        static xcb_atom_t motif_atom       = xconn.intern_atoms({"_MOTIF_WM_HINTS"})["_MOTIF_WM_HINTS"];
        static xcb_atom_t net_wm_name_prop =
            xconn.intern_atoms({"_NET_WM_NAME"})["_NET_WM_NAME"];
        bool              refresh_meta =
            ev->atom == XCB_ATOM_WM_CLASS ||
            ev->atom == atoms.net_wm_window_type ||
            ev->atom == XCB_ATOM_WM_NORMAL_HINTS ||
            ev->atom == XCB_ATOM_WM_HINTS ||
            ev->atom == motif_atom ||
            ev->atom == XCB_ATOM_WM_NAME ||
            ev->atom == net_wm_name_prop;
        if (refresh_meta) {
            auto meta = read_window_metadata(xconn, ev->window);
            // Geometry facts are set once at MapRequest; reconstruct from current state on refresh.
            meta.pre_fullscreen_state = window->self_managed;
            meta.covers_monitor       = window->self_managed || window->borderless;
            // (pre_fullscreen_state and covers_monitor are repurposed proxies here, not literal
            //  X11 facts — they preserve the classification outcome from the original MapRequest)
            if (ev->atom == motif_atom) {
                LOG_INFO("PropertyNotify(%d): _MOTIF_WM_HINTS changed, no_decos=%d",
                    ev->window, (int)meta.wm_no_decorations);
                // Promote to borderless if MOTIF says no decorations and we haven't yet.
                if (meta.wm_no_decorations && !window->borderless) {
                    if (window->self_managed) {
                        // Self-managed: mark borderless only, do not override geometry.
                        LOG_INFO("PropertyNotify(%d): MOTIF no-decorations (self-managed), marking borderless",
                            ev->window);
                        (void)core.dispatch(command::atom::SetWindowBorderless{ ev->window, true });
                        (void)core.dispatch(command::atom::SetWindowBorderWidth{ ev->window, 0 });
                        runtime.post_event(event::RaiseDocks{});
                        (void)core.dispatch(command::atom::ReconcileNow{});
                    } else {
                        int         mon_idx = core.monitor_of_workspace(core.workspace_of_window(ev->window));
                        const auto& mons    = core.monitor_states();
                        if (mon_idx >= 0 && mon_idx < (int)mons.size()) {
                            auto [phy_pos, phy_size] = mons[(size_t)mon_idx].physical();
                            LOG_INFO("PropertyNotify(%d): MOTIF no-decorations, promoting to borderless at %d,%d %dx%d",
                                ev->window, phy_pos.x(), phy_pos.y(), phy_size.x(), phy_size.y());
                            (void)core.dispatch(command::atom::SetWindowBorderless{ ev->window, true });
                            (void)core.dispatch(command::atom::SetWindowBorderWidth{ ev->window, 0 });
                            (void)core.dispatch(command::atom::SetWindowGeometry{
                                ev->window, phy_pos,
                                phy_size });
                            runtime.post_event(event::RaiseDocks{});
                            (void)core.dispatch(command::atom::ReconcileNow{});
                        }
                    }
                }
            }
            apply_window_metadata(core, ev->window, std::move(meta));
        }

        if (ev->atom == XCB_ATOM_WM_TRANSIENT_FOR && !window->floating) {
            xcb_window_t trans = xconn.get_transient_for(ev->window).value_or(XCB_WINDOW_NONE);
            if (trans != XCB_WINDOW_NONE && core.window_state_any(trans)) {
                int parent_ws = core.workspace_of_window(trans);
                if (parent_ws >= 0) {
                    (void)core.dispatch(command::atom::SetWindowSuppressFocusOnce{ ev->window, true });
                    (void)core.dispatch(command::atom::MoveWindowToWorkspace{ ev->window, parent_ws });
                }
                (void)core.dispatch(command::atom::SetWindowFloating{ ev->window, true });

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
                (void)core.dispatch(command::atom::ReconcileNow{});
            }
        }

    }
    runtime.post_event(event::PropertyNotify{ ev->window, ev->atom });
}

void X11Backend::handle_configure_request(xcb_configure_request_event_t* ev) {
    auto window = core.window_state_any(ev->window);

    if (!window) {
        int16_t cfg_x = ev->x;
        int16_t cfg_y = ev->y;

        // Steer unknown windows toward the cursor's monitor so Wine/Proton
        // service windows appear on the right screen.  Only steer when the
        // request includes a size component (width or height); position-only
        // requests (mask 0x3) come from fullscreen Wine windows that already
        // embed the correct border-compensated origin — overwriting it with
        // the raw monitor origin causes a 1px render offset.
        bool has_size = ev->value_mask & (XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT);
        if (has_size && (ev->value_mask & (XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y))) {
            const auto& mons        = core.monitor_states();
            int         ptr_mon_idx = -1;
            for (int i = 0; i < (int)mons.size(); i++) {
                if (mons[i].contains(last_pointer_)) {
                    ptr_mon_idx = i;
                    break;
                }
            }
            if (ptr_mon_idx >= 0) {
                const auto& m = mons[ptr_mon_idx];
                auto [tgt_pos, tgt_size] = m.physical();
                cfg_x                    = (int16_t)tgt_pos.x();
                cfg_y                    = (int16_t)tgt_pos.y();

                // If the requested size covers the source monitor (fullscreen
                // game requesting primary resolution), scale to the target
                // monitor so covers_monitor stays true after steering.
                if (ev->value_mask & (XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT)) {
                    int rw = (ev->value_mask & XCB_CONFIG_WINDOW_WIDTH)  ? ev->width  : 0;
                    int rh = (ev->value_mask & XCB_CONFIG_WINDOW_HEIGHT) ? ev->height : 0;
                    for (int i = 0; i < (int)mons.size(); i++) {
                        auto [src_pos, src_size] = mons[i].physical();
                        if (rw == src_size.x() && rh == src_size.y()) {
                            ev->width       = (uint16_t)tgt_size.x();
                            ev->height      = (uint16_t)tgt_size.y();
                            ev->value_mask |= XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT;
                            LOG_DEBUG("ConfigureRequest(%d): scaling size %dx%d -> %dx%d (source mon %d)",
                                ev->window, rw, rh, tgt_size.x(), tgt_size.y(), i);
                            break;
                        }
                    }
                }
                LOG_DEBUG("ConfigureRequest(%d): steering pos %d,%d -> %d,%d (cursor monitor %d)",
                    ev->window, ev->x, ev->y, cfg_x, cfg_y, ptr_mon_idx);
            }

            if (first_configure_pos_.find(ev->window) == first_configure_pos_.end())
                first_configure_pos_[ev->window] = { cfg_x, cfg_y };
        }

        // Build the patch with potentially overridden x/y.
        ConfigureRequestPatch msg;
        uint16_t              m = ev->value_mask;
        auto                  put = [&](uint16_t flag, uint32_t value) {
                if (!(m & flag)) return;
                msg.mask |= flag;
                msg.values.push_back(value);
            };
        put(XCB_CONFIG_WINDOW_X,            static_cast<uint32_t>(cfg_x));
        put(XCB_CONFIG_WINDOW_Y,            static_cast<uint32_t>(cfg_y));
        put(XCB_CONFIG_WINDOW_WIDTH,        static_cast<uint32_t>(ev->width));
        put(XCB_CONFIG_WINDOW_HEIGHT,       static_cast<uint32_t>(ev->height));
        put(XCB_CONFIG_WINDOW_BORDER_WIDTH, static_cast<uint32_t>(ev->border_width));
        put(XCB_CONFIG_WINDOW_SIBLING,      static_cast<uint32_t>(ev->sibling));
        put(XCB_CONFIG_WINDOW_STACK_MODE,   static_cast<uint32_t>(ev->stack_mode));

        LOG_DEBUG("ConfigureRequest from unknown window %d, redirecting, pos=%d,%d mask=0x%x",
            ev->window, cfg_x, cfg_y, ev->value_mask);
        xconn.configure_window(ev->window, msg.mask, msg.values.data());
        return;
    }

    uint16_t m = ev->value_mask;

    // Floating and borderless clients honour their own ConfigureRequest geometry.
    // StaticGravity clients self-position (inner-origin coords, border_width pre-subtracted).
    // Tiled clients receive a synthetic ConfigureNotify with current WM-assigned geometry.
    bool floating       = window->floating;
    bool borderless     = window->borderless;
    bool static_gravity = window->preserve_position;

    if ((m & XCB_CONFIG_WINDOW_BORDER_WIDTH) && !borderless && !window->is_self_managed())
        (void)core.dispatch(command::atom::SetWindowBorderWidth{ ev->window, ev->border_width });
    // Reject restack requests from fullscreen windows — raise docks instead.
    // Restack (sibling/stack_mode) is X11-specific and applied directly without routing through core.
    if (m & XCB_CONFIG_WINDOW_STACK_MODE) {
        if (window->fullscreen) {
            runtime.post_event(event::RaiseDocks{});
        } else if (auto* xw = x11_window(ev->window)) {
            uint16_t restack_mask    = XCB_CONFIG_WINDOW_STACK_MODE;
            uint32_t restack_vals[2] = {};
            int      ri              = 0;
            if (m & XCB_CONFIG_WINDOW_SIBLING) {
                restack_mask      |= XCB_CONFIG_WINDOW_SIBLING;
                restack_vals[ri++] = ev->sibling;
            }
            restack_vals[ri] = ev->stack_mode;
            xw->configure(restack_mask, restack_vals);
        }
    }

    // A tiled window requesting monitor-covering geometry is going fullscreen
    // without EWMH. Core decides whether this should promote to borderless.
    bool borderless_fs = false;
    if (!floating && !borderless && !static_gravity &&
        (m & (XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT))) {
        Vec2i req_size{ static_cast<int>(ev->width), static_cast<int>(ev->height) };
        auto  decision = core.evaluate_fullscreen_like_request(ev->window, req_size, true);
        if (decision.promote) {
            (void)core.dispatch(command::atom::SetWindowBorderless{ ev->window, true });
            borderless    = true;
            borderless_fs = true;
            ev->x         = static_cast<int16_t>(std::clamp(decision.pos.x(), -32768, 32767));
            ev->y         = static_cast<int16_t>(std::clamp(decision.pos.y(), -32768, 32767));
            ev->width     = static_cast<uint16_t>(std::min(decision.size.x(), 65535));
            ev->height    = static_cast<uint16_t>(std::min(decision.size.y(), 65535));
            m            |= XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y
                | XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT;
            runtime.post_event(event::RaiseDocks{});
        }
    }

    if (floating || borderless || static_gravity) {
        // WM-pinned windows (fullscreen, WM-placed borderless): reject geometry requests.
        bool pinned = (window->fullscreen && !borderless_fs) ||
            (window->borderless && !borderless_fs && !window->self_managed);
        if (pinned) {
            if (auto* xw = x11_window(ev->window))
                xw->send_configure_notify(window->pos().x(), window->pos().y(),
                    window->size().x(), window->size().y(), window->border_width);
            return;
        }

        int nx = window->pos().x();
        int ny = window->pos().y();
        int nw = window->size().x();
        int nh = window->size().y();

        if (m & XCB_CONFIG_WINDOW_X)      nx = ev->x;
        if (m & XCB_CONFIG_WINDOW_Y)      ny = ev->y;
        if (m & XCB_CONFIG_WINDOW_WIDTH)  nw = ev->width;
        if (m & XCB_CONFIG_WINDOW_HEIGHT) nh = ev->height;

        // ICCCM §4.1.2.3 StaticGravity: client provides inner-origin coords
        // (content position), but the WM stores outer-corner coords.
        // Subtract border_width to convert to outer-corner.
        int out_bw = (m & XCB_CONFIG_WINDOW_BORDER_WIDTH)
            ? (int)ev->border_width : (int)window->border_width;
        if (static_gravity && out_bw > 0) {
            nx -= out_bw;
            ny -= out_bw;
        }

        // Fixed-size windows: clamp position to keep them on their monitor.
        // Skip for static_gravity/borderless — client may intentionally position off-edge.
        if (!borderless_fs && window->size_locked && !window->preserve_position) {
            int         mon_idx = core.monitor_of_workspace(core.workspace_of_window(ev->window));
            const auto& mons    = core.monitor_states();
            if (mon_idx >= 0 && mon_idx < (int)mons.size()) {
                const auto& mon   = mons[(size_t)mon_idx];
                int         max_x = mon.pos().x() + mon.size().x() - nw;
                int         max_y = mon.pos().y() + mon.size().y() - nh;
                nx = std::clamp(nx, mon.pos().x(), std::max(mon.pos().x(), max_x));
                ny = std::clamp(ny, mon.pos().y(), std::max(mon.pos().y(), max_y));
            }
        }

        (void)core.dispatch(command::atom::SetWindowGeometry{ ev->window, { nx, ny }, { nw, nh } });
        if (borderless_fs)
            (void)core.dispatch(command::atom::ReconcileNow{});

        // ICCCM §4.1.5: send synthetic ConfigureNotify after accepting a request.
        // For StaticGravity, report inner-origin coords (outer + bw) back to client.
        if (auto* xw = x11_window(ev->window)) {
            int32_t rx = nx;
            int32_t ry = ny;
            if (static_gravity && out_bw > 0) {
                rx += (int32_t)out_bw;
                ry += (int32_t)out_bw;
            }
            xw->send_configure_notify(rx, ry, nw, nh, out_bw);
        }
    } else {
        if (auto* xw = x11_window(ev->window))
            xw->send_configure_notify(window->pos().x(), window->pos().y(),
                window->size().x(), window->size().y(), window->border_width);
    }

    LOG_DEBUG("ConfigureRequest(%d): x:%d y:%d w:%d h:%d bw:%d",
        ev->window, ev->x, ev->y, ev->width, ev->height, ev->border_width);
}

void X11Backend::handle_configure_notify(xcb_configure_notify_event_t* ev) {
    // Always emit — tray needs ConfigureNotify to track icon resize.
    runtime.post_event(event::ConfigureNotify{ ev->window, { ev->x, ev->y }, { ev->width, ev->height } });

    auto window = core.window_state_any(ev->window);
    if (!window || ev->override_redirect)
        return;

    (void)core.dispatch(command::atom::SyncWindowFromConfigureNotify{
        ev->window,
        { ev->x, ev->y },
        { ev->width, ev->height },
        ev->border_width
    });

    LOG_DEBUG("ConfigureNotify(%d): x:%d y:%d w:%d h:%d bw:%d",
        ev->window, ev->x, ev->y, ev->width, ev->height, ev->border_width);
}

void X11Backend::handle_key_event(xcb_key_press_event_t* ev) {
    // Sync focused monitor to pointer so workspace bindings act on the right monitor.
    core.focus_monitor_at_point(ev->root_x, ev->root_y);

    if ((ev->response_type & ~0x80) == XCB_KEY_RELEASE) {
        xcb_generic_event_t* next = xconn.poll_event();
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
    runtime.post_event(event::KeyPressEv{ ev->state, ev->detail, keysym });
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
    (void)core.dispatch(command::atom::FocusWindow{ ev->event });
}

void X11Backend::handle_button_event(xcb_button_press_event_t* ev) {
    last_event_time_ = ev->time;
    last_pointer_    = { ev->root_x, ev->root_y };

    bool release = (ev->response_type & ~0x80) == XCB_BUTTON_RELEASE;
    if (!release)
        core.focus_monitor_at_point(ev->root_x, ev->root_y);

    runtime.post_event(make_button_ev(ev, release));
}

void X11Backend::handle_motion_notify(xcb_motion_notify_event_t* ev) {
    last_pointer_ = { ev->root_x, ev->root_y };
    runtime.post_event(event::MotionEv{ ev->event, { ev->root_x, ev->root_y }, ev->state });
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
    bool handled = runtime.emit_until_handled(msg);
    if (!handled)
        LOG_DEBUG("ClientMessage(%d): unhandled type=%u", ev->window, ev->type);
}

void X11Backend::handle_expose(xcb_expose_event_t* ev) {
    runtime.post_event(event::ExposeWindow{ ev->window });
}

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
    last_pointer_    = { ev->root_x, ev->root_y };

    // Use window_state_any so that windows on the second monitor's active
    // workspace are found even when focused_monitor hasn't been updated yet
    // (focused_monitor is only updated on button press / motion, not on enter).
    auto window = core.window_state_any(ev->event);
    if (!window || !window->is_visible())
        return;

    // Keep focused_monitor in sync so subsequent workspace/layout ops target
    // the correct monitor without requiring a click first.
    core.focus_monitor_at_point(ev->root_x, ev->root_y);

    (void)core.dispatch(command::atom::FocusWindow{ ev->event });
    // Request focus at kPointer priority — applied after apply_core_backend_effects()
    // so pointer always wins over stale workspace-switch FocusWindow effects.
    request_focus(ev->event, kFocusPointer);
}

void X11Backend::handle_generic_event(xcb_generic_event_t* ev) {
    uint8_t type = ev->response_type & ~0x80;

    if (type == 0) {
        auto* err = reinterpret_cast<xcb_generic_error_t*>(ev);
        // BadWindow (code=3) errors are expected when async XCB requests
        // race with client-initiated window destruction. Log at debug level.
        if (err->error_code == 3) {
            LOG_DEBUG("X11 BadWindow: major=%u minor=%u resource=%u seq=%u",
                err->major_code, err->minor_code, err->resource_id, err->sequence);
            return;
        }
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
        case XCB_GRAPHICS_EXPOSURE:
        case XCB_NO_EXPOSURE:
            // Benign render-side notifications (often produced by CopyArea).
            // They do not require WM-side state changes.
            break;
        case XCB_VISIBILITY_NOTIFY:
        case XCB_CREATE_NOTIFY:
            // Purely informative; no WM state changes required.
            break;
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
                    runtime.post_event(event::CustomEvent{
                        MessageEnvelope::pack(protocol::keyboard::LayoutChanged::from(layout))
                    });
                }
                break;
            }
            LOG_DEBUG("No case for %d", type);
            break;
        }
    }
}
