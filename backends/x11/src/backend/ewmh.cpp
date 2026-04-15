#include <x11_backend.hpp>
#include <x11/x11_atoms.hpp>

#include <xcb/atom.hpp>
#include <xcb/property.hpp>
#include <domain/core.hpp>
#include <x11/xconn.hpp>
#include <support/log.hpp>
#include <runtime/runtime.hpp>

#include <string>
#include <vector>
#include <algorithm>

namespace {

bool has_xembed_info_property(XConnection& xconn, xcb_window_t win, xcb_atom_t xembed_info_atom) {
    return xconn.has_property_32(win, xembed_info_atom, 2);
}

std::pair<int, int> window_size(XConnection& xconn, xcb_window_t win) {
    auto geo = xconn.get_window_geometry(win);
    if (!geo) return { 0, 0 };
    return { (int)geo->width, (int)geo->height };
}

bool should_apply_fullscreen_now(Core& core, WindowId win) {
    auto w = core.window_state_any(win);
    if (!w) return false;
    if (w->hidden_by_workspace) return false;
    int ws_id = core.workspace_of_window(win);
    if (ws_id >= 0 && !core.is_workspace_visible(ws_id)) return false;
    return true;
}

} // namespace

void X11Backend::reload_border_colors() {
    const auto& theme     = core.current_settings().theme;
    const auto& focused   = theme.border_focused.empty()   ? theme.accent : theme.border_focused;
    const auto& unfocused = theme.border_unfocused.empty() ?
        (theme.alt_bg.empty() ? theme.bg : theme.alt_bg) : theme.border_unfocused;
    border_focused_pixel   = XConnection::parse_color_hex(focused);
    border_unfocused_pixel = XConnection::parse_color_hex(unfocused);
}

void X11Backend::set_border_color(WindowId win, uint32_t pixel) {
    if (auto* xw = x11_window(win))
        xw->set_border_color(pixel);
}

void X11Backend::restore_visible_focus() {
    if (auto focused = core.focused_window_state(); focused && focused->is_visible()) {
        request_focus(focused->id, kFocusWorkspace);
    } else {
        // No visible focused window — fall back to root immediately (no arbiter needed).
        xconn.focus_window(root_window);
        core.emit_focus_changed(NO_WINDOW);
    }
}

void X11Backend::ewmh_intern_atoms() {
    ewmh_atoms_.resolve(xconn.raw());
}

bool X11Backend::ewmh_supports_delete(WindowId win) {
    if (auto* xw = x11_window(win))
        return xw->supports_delete();
    return false;
}

void X11Backend::ewmh_close_with_message(WindowId win) {
    if (auto* xw = x11_window(win))
        xw->send_delete_message();
}

void X11Backend::focus_window(WindowId win) {
    auto* xw = x11_window(win);
    if (!xw)
        return;
    xw->focus(last_event_time_);
    if (!xw->no_input_focus)
        xconn.set_property(root_window, ewmh_atoms_[EwmhAtom::NetActiveWindow], XCB_ATOM_WINDOW, win);
}

void X11Backend::ewmh_update_client_list() {
    auto wins = core.all_window_ids();
    xconn.set_property(root_window, ewmh_atoms_[EwmhAtom::NetClientList], wins.data(), (int)wins.size());
    // _NET_CLIENT_LIST_STACKING: EWMH requires bottom-to-top z-order.
    // We don't track stacking order, so mirror _NET_CLIENT_LIST (oldest first).
    // Compliant compositors handle this gracefully.
    xconn.set_property(root_window, ewmh_atoms_[EwmhAtom::NetClientListStacking], wins.data(), (int)wins.size());
}

bool X11Backend::ewmh_has_fullscreen_state(WindowId win) {
    if (auto* xw = x11_window(win))
        return xw->has_fullscreen_state();
    return false;
}

void X11Backend::ewmh_set_wm_state_atom(WindowId win, xcb_atom_t atom, bool enabled) {
    if (auto* xw = x11_window(win))
        xw->set_wm_state_atom(atom, enabled);
}

void X11Backend::ewmh_set_fullscreen_state_property(WindowId win, bool enabled) {
    if (auto* xw = x11_window(win))
        xw->set_fullscreen_state(enabled);
}

void X11Backend::ewmh_set_frame_extents(WindowId win, uint32_t bw) {
    if (auto* xw = x11_window(win))
        xw->set_frame_extents(bw);
}

void X11Backend::set_wm_state_normal(WindowId win) {
    if (auto* xw = x11_window(win))
        xw->set_wm_state_normal();
}

void X11Backend::ewmh_apply_fullscreen(WindowId win, bool enabled) {
    (void)core.dispatch(command::atom::SetWindowFullscreen{ win, enabled });
    ewmh_set_fullscreen_state_property(win, enabled);
}

void X11Backend::ewmh_update_desktop_props() {
    uint32_t n_ws = (uint32_t)core.workspace_count();
    if (n_ws == 0)
        return;

    xconn.set_property(root_window, ewmh_atoms_[EwmhAtom::NetNumberOfDesktops], XCB_ATOM_CARDINAL, n_ws);

    std::string names;
    for (const auto& ws : core.workspace_states()) {
        names += ws.name;
        names += '\0';
    }
    xconn.set_property(root_window, ewmh_atoms_[EwmhAtom::NetDesktopNames], ewmh_atoms_[EwmhAtom::Utf8String], names);

    // _NET_DESKTOP_GEOMETRY — one [width, height] for all desktops (no large desktop / panning)
    auto     geo      = xconn.get_window_geometry(root_window);
    uint32_t screen_w = geo ? (uint32_t)geo->width  : 1280u;
    uint32_t screen_h = geo ? (uint32_t)geo->height : 720u;
    uint32_t geom[2]  = { screen_w, screen_h };
    xconn.set_property(root_window, ewmh_atoms_[EwmhAtom::NetDesktopGeometry], XCB_ATOM_CARDINAL,
        (const xcb_atom_t*)geom, 2);

    // _NET_DESKTOP_VIEWPORT — [x, y] per workspace; all zero (no virtual desktops)
    std::vector<uint32_t> vp(n_ws * 2, 0u);
    xconn.set_property(root_window, ewmh_atoms_[EwmhAtom::NetDesktopViewport], XCB_ATOM_CARDINAL,
        (const xcb_atom_t*)vp.data(), (int)vp.size());

    // _NET_WORKAREA — [x, y, w, h] per workspace; full screen area (bars handle their own inset)
    std::vector<uint32_t> wa;
    wa.reserve(n_ws * 4);
    for (uint32_t i = 0; i < n_ws; ++i) {
        wa.push_back(0u);
        wa.push_back(0u);
        wa.push_back(screen_w);
        wa.push_back(screen_h);
    }
    xconn.set_property(root_window, ewmh_atoms_[EwmhAtom::NetWorkarea], XCB_ATOM_CARDINAL,
        (const xcb_atom_t*)wa.data(), (int)wa.size());
}

void X11Backend::ewmh_init() {
    ewmh_intern_atoms();

    ewmh_wm_window = xconn.generate_id();
    xconn.create_window(ewmh_wm_window, root_window, -1, -1, 1, 1,
        XCB_WINDOW_CLASS_INPUT_ONLY);

    xconn.set_property(ewmh_wm_window, ewmh_atoms_[EwmhAtom::NetSupportingWmCheck], &ewmh_wm_window, 1);
    xconn.set_property(ewmh_wm_window, ewmh_atoms_[EwmhAtom::NetWmName],
        ewmh_atoms_[EwmhAtom::Utf8String], std::string("SirenWM"));
    xconn.set_property(root_window, ewmh_atoms_[EwmhAtom::NetSupportingWmCheck], &ewmh_wm_window, 1);

    const xcb_atom_t supported[] = {
        ewmh_atoms_[EwmhAtom::NetSupported],
        ewmh_atoms_[EwmhAtom::NetWmName],
        ewmh_atoms_[EwmhAtom::NetActiveWindow],
        ewmh_atoms_[EwmhAtom::NetClientList],
        ewmh_atoms_[EwmhAtom::NetClientListStacking],
        ewmh_atoms_[EwmhAtom::NetSupportingWmCheck],
        ewmh_atoms_[EwmhAtom::NetWmState],
        ewmh_atoms_[EwmhAtom::NetWmStateFullscreen],
        ewmh_atoms_[EwmhAtom::NetWmStateHidden],
        ewmh_atoms_[EwmhAtom::NetWmStateFocused],
        ewmh_atoms_[EwmhAtom::NetFrameExtents],
        ewmh_atoms_[EwmhAtom::NetWmWindowType],
        ewmh_atoms_[EwmhAtom::NetWmWindowTypeDock],
        ewmh_atoms_[EwmhAtom::NetWmWindowTypeDialog],
        ewmh_atoms_[EwmhAtom::NetCloseWindow],
        ewmh_atoms_[EwmhAtom::NetNumberOfDesktops],
        ewmh_atoms_[EwmhAtom::NetCurrentDesktop],
        ewmh_atoms_[EwmhAtom::NetDesktopNames],
        ewmh_atoms_[EwmhAtom::NetDesktopGeometry],
        ewmh_atoms_[EwmhAtom::NetDesktopViewport],
        ewmh_atoms_[EwmhAtom::NetWorkarea],
        ewmh_atoms_[EwmhAtom::NetWmDesktop],
    };
    xconn.set_property(root_window, ewmh_atoms_[EwmhAtom::NetSupported], XCB_ATOM_ATOM,
        supported, (int)(sizeof(supported) / sizeof(supported[0])));

    xconn.set_property(root_window, ewmh_atoms_[EwmhAtom::NetCurrentDesktop], XCB_ATOM_CARDINAL, 0u);
    ewmh_update_desktop_props();
    ewmh_update_client_list();

    xcb_window_t none = XCB_WINDOW_NONE;
    xconn.set_property(root_window, ewmh_atoms_[EwmhAtom::NetActiveWindow], &none, 1);

    xconn.flush();
    LOG_INFO("EWMH: initialized, wm_window=%d", ewmh_wm_window);
}

void X11Backend::on_hook(hook::ShouldManageWindow& h) {
    WindowId win = h.window;
    if (ewmh_atoms_[EwmhAtom::NetWmWindowType] == XCB_ATOM_NONE)
        return;

    auto types     = xconn.get_atom_list_property(win, ewmh_atoms_[EwmhAtom::NetWmWindowType]);
    bool skip_type =
        has_atom(types, ewmh_atoms_[EwmhAtom::NetWmWindowTypeDock]) ||
        has_atom(types, ewmh_atoms_[EwmhAtom::NetWmWindowTypeDesktop]) ||
        has_atom(types, ewmh_atoms_[EwmhAtom::NetWmWindowTypeNotification]) ||
        has_atom(types, ewmh_atoms_[EwmhAtom::NetWmWindowTypeTooltip]) ||
        has_atom(types, ewmh_atoms_[EwmhAtom::NetWmWindowTypeDnd]) ||
        has_atom(types, ewmh_atoms_[EwmhAtom::NetWmWindowTypeDropdownMenu]) ||
        has_atom(types, ewmh_atoms_[EwmhAtom::NetWmWindowTypePopupMenu]) ||
        has_atom(types, ewmh_atoms_[EwmhAtom::NetWmWindowTypeMenu]);
    if (skip_type) {
        LOG_DEBUG("EWMH: skipping utility window %d by _NET_WM_WINDOW_TYPE", win);
        h.manage = false;
        return;
    }

    if (has_xembed_info_property(xconn, win, ewmh_atoms_[EwmhAtom::XembedInfo])) {
        auto [instance, cls] = xconn.get_wm_class(win);
        auto title = xconn.get_text_property(win, ewmh_atoms_[EwmhAtom::NetWmName], ewmh_atoms_[EwmhAtom::Utf8String]);
        auto [w, hh] = window_size(xconn, win);

        bool identifiable   = !instance.empty() || !cls.empty() || !title.empty();
        bool tray_like_size = (w > 0 && hh > 0 && w <= 128 && hh <= 128);

        if (!identifiable || tray_like_size) {
            LOG_DEBUG("EWMH: skipping XEMBED tray-like window %d (class='%s' instance='%s' size=%dx%d)",
                win, cls.c_str(), instance.c_str(), w, hh);
            h.manage = false;
            return;
        }

        LOG_DEBUG("EWMH: allowing XEMBED toplevel window %d (class='%s' instance='%s' size=%dx%d)",
            win, cls.c_str(), instance.c_str(), w, hh);
    }
}

void X11Backend::on_hook(hook::CloseWindow& h) {
    if (auto* xw = x11_window(h.window)) {
        if (xw->supports_delete())
            xw->send_delete_message();
        else
            xw->kill();
    }
    xconn.flush();
    h.handled = true;
}

void X11Backend::ewmh_on_window_mapped(event::WindowMapped ev) {
    auto* xw = x11_window(ev.window);
    if (!xw) return;

    // Paint initial border; skip if already focused (color set by update_focus).
    if (ev.window != border_painted_focused_)
        xw->set_border_color(border_unfocused_pixel);

    // ICCCM §4.1.3: set WM_STATE to NormalState when managing a visible window.
    // Windows mapped onto an invisible workspace stay in IconicState.
    {
        auto ws = core.window_state_any(ev.window);
        if (ws && !ws->hidden_by_workspace)
            xw->set_wm_state_normal();
    }
    // _NET_WM_DESKTOP: expose workspace index to panels and pagers.
    {
        int ws = core.workspace_of_window(ev.window);
        if (ws >= 0)
            xw->set_desktop((uint32_t)ws);
    }
    ewmh_update_client_list();
    // We no longer set _NET_WM_STATE_HIDDEN during workspace switches,
    // but clear it on map anyway: the property may linger from a previous
    // WM session (restart) or from an explicit HideWindow command.
    if (ewmh_atoms_[EwmhAtom::NetWmStateHidden] != XCB_ATOM_NONE)
        ewmh_set_wm_state_atom(ev.window, ewmh_atoms_[EwmhAtom::NetWmStateHidden], false);
    {
        auto w = core.window_state_any(ev.window);
        // Self-managed windows already know their position; pinning to mon.x/mon.y
        // breaks render child placement. WM-pinned borderless windows are already
        // fullscreen-equivalent — applying fullscreen on top would cause bars/tray to lower.
        bool skip_fullscreen = w && w->borderless && !w->self_managed;
        bool self_managed    = w && w->is_self_managed();
        if (!skip_fullscreen && ewmh_has_fullscreen_state(ev.window) && should_apply_fullscreen_now(core, ev.window)) {
            if (self_managed)
                (void)core.dispatch(command::atom::SetWindowFullscreen{
                    ev.window, true, /*preserve_geometry=*/ true });
            else
                ewmh_apply_fullscreen(ev.window, true);
        }
    }

    // _NET_FRAME_EXTENTS: this is a non-reparenting WM — we add no decorative
    // frame around windows. The X border_width is the window's own attribute,
    // not WM-added decoration. Report [0,0,0,0] so clients (Wine/Proton) do
    // not offset their render children to compensate for a non-existent frame.
    ewmh_set_frame_extents(ev.window, 0u);

    // xcb_map_window puts the window on top of the stack.
    // Borderless and fullscreen windows cover the entire monitor — they should
    // stay above the bar, so do NOT raise docks over them.
}

void X11Backend::ewmh_on_window_unmapped(event::WindowUnmapped ev) {
    if (auto* xw = x11_window(ev.window)) {
        // ICCCM §4.1.4: WithdrawnState when unmanaged; IconicState when hidden.
        uint32_t state = ev.withdrawn ? 0u /* WithdrawnState */ : 3u /* IconicState */;
        xw->set_wm_state(state);
        // _NET_WM_STATE_HIDDEN: only clear on withdraw (unmanage).
        // Do NOT set on workspace hide — Qt/GTK treat it as an additional
        // "minimized" signal and may aggressively suspend rendering.
        if (ev.withdrawn)
            xw->set_wm_state_atom(ewmh_atoms_[EwmhAtom::NetWmStateHidden], false);
    }
    ewmh_update_client_list();
}

void X11Backend::update_focus(event::FocusChanged ev) {
    xconn.set_property(root_window, ewmh_atoms_[EwmhAtom::NetActiveWindow], XCB_ATOM_WINDOW, ev.window);
    if (ewmh_atoms_[EwmhAtom::NetWmStateFocused] != XCB_ATOM_NONE) {
        if (border_painted_focused_ != NO_WINDOW && border_painted_focused_ != ev.window)
            ewmh_set_wm_state_atom(border_painted_focused_, ewmh_atoms_[EwmhAtom::NetWmStateFocused], false);
        if (ev.window != NO_WINDOW)
            ewmh_set_wm_state_atom(ev.window, ewmh_atoms_[EwmhAtom::NetWmStateFocused], true);
    }
    if (border_painted_focused_ != NO_WINDOW && border_painted_focused_ != ev.window)
        set_border_color(border_painted_focused_, border_unfocused_pixel);
    if (ev.window != NO_WINDOW)
        set_border_color(ev.window, border_focused_pixel);
    border_painted_focused_ = ev.window;

    // Sync pointer barriers: barriers follow focus. Single source of truth.
    auto w = (ev.window != NO_WINDOW) ? core.window_state_any(ev.window) : nullptr;
    if (w && w->borderless) {
        int ws_id   = core.workspace_of_window(ev.window);
        int mon_idx = core.monitor_of_workspace(ws_id);
        if (mon_idx >= 0)
            set_pointer_barriers(ev.window, mon_idx);
    } else if (barrier_window_ != NO_WINDOW) {
        // Only drop grab/barriers when leaving a previously barriered
        // (borderless) window. Unconditional ungrab here would cancel our
        // own drag/resize pointer grab on every focus change.
        xconn.ungrab_pointer();
        clear_pointer_barriers();
    }
}

void X11Backend::ewmh_on_workspace_switched(event::WorkspaceSwitched ev) {
    ewmh_update_desktop_props();
    xconn.set_property(root_window, ewmh_atoms_[EwmhAtom::NetCurrentDesktop],
        XCB_ATOM_CARDINAL, (uint32_t)ev.workspace_id);

    // A window may carry _NET_WM_STATE_FULLSCREEN while on a hidden workspace;
    // defer apply until the workspace becomes visible.
    auto ws = core.workspace_state(ev.workspace_id);
    if (!ws)
        return;

    for (const auto& w : ws->windows) {
        if (!w) continue;
        auto win = w->id;
        if (w->hidden_by_workspace) continue;
        if (w->is_self_managed()) continue;                   // client owns geometry: skip fullscreen pin
        if (w->borderless && !w->self_managed) continue;     // WM-pinned borderless: fullscreen would lower bars/tray
        if (!w->fullscreen && ewmh_has_fullscreen_state(win))
            ewmh_apply_fullscreen(win, true);
    }
}

void X11Backend::ewmh_on_window_assigned_to_workspace(event::WindowAssignedToWorkspace ev) {
    if (auto* xw = x11_window(ev.window))
        xw->set_desktop((uint32_t)ev.workspace_id);
}

bool X11Backend::handle(event::ClientMessageEv ev) {
    if (ev.type == ewmh_atoms_[EwmhAtom::NetWmState]) {
        xcb_atom_t a1         = (xcb_atom_t)ev.data[1];
        xcb_atom_t a2         = (xcb_atom_t)ev.data[2];
        xcb_atom_t fullscreen = ewmh_atoms_[EwmhAtom::NetWmStateFullscreen];
        if (a1 != fullscreen && a2 != fullscreen)
            return true;

        auto window = core.window_state_any(ev.window);
        if (!window) {
            LOG_DEBUG("ClientMessage(%d): _NET_WM_STATE fullscreen but window not managed", ev.window);
            return true;
        }

        bool     enable = false;
        uint32_t action = ev.data[0];  // 0=remove, 1=add, 2=toggle
        if (action == 0) enable = false;
        else if (action == 1) enable = true;
        else if (action == 2) enable = !window->fullscreen;
        else return true;

        LOG_DEBUG("ClientMessage(%d): _NET_WM_STATE fullscreen action=%u enable=%d",
            ev.window, action, (int)enable);

        // Self-managed windows control their own position; pinning to mon.x/mon.y breaks them.
        // WM-pinned borderless windows are already fullscreen-equivalent — applying fullscreen
        // on top would lower bars/tray.
        bool self_managed = window->is_self_managed() || (window->borderless && !window->self_managed);
        if (enable) {
            if (!self_managed && should_apply_fullscreen_now(core, ev.window))
                ewmh_apply_fullscreen(ev.window, true);
            else
                ewmh_set_fullscreen_state_property(ev.window, true);
        } else {
            if (!self_managed)
                ewmh_apply_fullscreen(ev.window, false);
            else
                ewmh_set_fullscreen_state_property(ev.window, false);
        }
        return true;
    }

    if (ev.type == ewmh_atoms_[EwmhAtom::NetActiveWindow]) {
        auto window = core.window_state_any(ev.window);
        if (!window)
            return false;
        if (!window->is_visible())
            return true;

        int ws_id = core.workspace_of_window(ev.window);
        // Do not force workspace jumps on external focus requests.
        if (ws_id >= 0 && !core.is_workspace_visible(ws_id))
            return true;

        (void)core.dispatch(command::atom::FocusWindow{ ev.window });
        request_focus(ev.window, kFocusEWMH);
        return true;
    }

    if (ev.type == ewmh_atoms_[EwmhAtom::NetCurrentDesktop]) {
        int ws_id = (int)ev.data[0];
        if (ws_id < 0 || ws_id >= core.workspace_count())
            return true;
        (void)core.dispatch(command::atom::SwitchWorkspace{ ws_id, std::nullopt });
        return true;
    }

    if (ev.type == ewmh_atoms_[EwmhAtom::NetWmDesktop]) {
        int ws_id = (int)ev.data[0];
        if (ws_id < 0 || ws_id >= core.workspace_count())
            return true;
        auto w = core.window_state_any(ev.window);
        if (!w)
            return true;
        LOG_DEBUG("ClientMessage(%d): _NET_WM_DESKTOP -> workspace %d", ev.window, ws_id);
        (void)core.dispatch(command::atom::MoveWindowToWorkspace{ ev.window, ws_id });
        return true;
    }

    if (ev.type == ewmh_atoms_[EwmhAtom::NetCloseWindow]) {
        runtime.invoke_hook(hook::CloseWindow{ ev.window });
        return true;
    }

    return false;
}
