#include <x11_backend.hpp>

#include <xcb/atom.hpp>
#include <xcb/property.hpp>
#include <core.hpp>
#include <xconn.hpp>
#include <log.hpp>
#include <runtime.hpp>

#include <string>
#include <vector>
#include <algorithm>

namespace {

bool has_atom(const std::vector<xcb_atom_t>& atoms, xcb_atom_t needle) {
    if (needle == XCB_ATOM_NONE)
        return false;
    return std::find(atoms.begin(), atoms.end(), needle) != atoms.end();
}

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

static uint32_t parse_color_hex(const std::string& s) {
    // Accepts "#rrggbb" only.
    if (s.size() == 7 && s[0] == '#') {
        char*    end = nullptr;
        uint32_t rgb = (uint32_t)strtoul(s.c_str() + 1, &end, 16);
        if (end == s.c_str() + 7)
            return rgb;
    }
    return 0;
}

} // namespace

void X11Backend::reload_border_colors() {
    const auto& theme     = core.current_settings().theme;
    const auto& focused   = theme.border_focused.empty()   ? theme.accent : theme.border_focused;
    const auto& unfocused = theme.border_unfocused.empty() ?
        (theme.alt_bg.empty() ? theme.bg : theme.alt_bg) : theme.border_unfocused;
    border_focused_pixel   = parse_color_hex(focused);
    border_unfocused_pixel = parse_color_hex(unfocused);
}

void X11Backend::set_border_color(WindowId win, uint32_t pixel) {
    if (win == NO_WINDOW || win == root_window)
        return;
    xconn.change_window_attributes(win, XCB_CW_BORDER_PIXEL, &pixel);
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
    auto atoms = xconn.intern_atoms({
        "_NET_SUPPORTED", "_NET_WM_NAME", "_NET_WM_STATE",
        "_NET_WM_STATE_FULLSCREEN", "_NET_WM_STATE_HIDDEN", "_NET_WM_STATE_FOCUSED",
        "_NET_FRAME_EXTENTS", "_NET_ACTIVE_WINDOW", "_NET_CLIENT_LIST", "_NET_CLIENT_LIST_STACKING",
        "_NET_SUPPORTING_WM_CHECK", "_NET_WM_WINDOW_TYPE",
        "_NET_WM_WINDOW_TYPE_DOCK", "_NET_WM_WINDOW_TYPE_DIALOG",
        "_NET_WM_WINDOW_TYPE_DESKTOP", "_NET_WM_WINDOW_TYPE_NOTIFICATION",
        "_NET_WM_WINDOW_TYPE_TOOLTIP", "_NET_WM_WINDOW_TYPE_DND",
        "_NET_WM_WINDOW_TYPE_DROPDOWN_MENU", "_NET_WM_WINDOW_TYPE_POPUP_MENU",
        "_NET_WM_WINDOW_TYPE_MENU",
        "_NET_CLOSE_WINDOW", "_NET_NUMBER_OF_DESKTOPS",
        "_NET_CURRENT_DESKTOP", "_NET_DESKTOP_NAMES",
        "_NET_DESKTOP_GEOMETRY", "_NET_DESKTOP_VIEWPORT", "_NET_WORKAREA",
        "UTF8_STRING", "WM_PROTOCOLS", "WM_DELETE_WINDOW", "WM_TAKE_FOCUS", "WM_STATE", "_XEMBED_INFO",
        "_NET_WM_DESKTOP",
    });

    NET_SUPPORTED                    = atoms["_NET_SUPPORTED"];
    NET_WM_NAME                      = atoms["_NET_WM_NAME"];
    NET_WM_STATE                     = atoms["_NET_WM_STATE"];
    NET_WM_STATE_FULLSCREEN          = atoms["_NET_WM_STATE_FULLSCREEN"];
    NET_WM_STATE_HIDDEN              = atoms["_NET_WM_STATE_HIDDEN"];
    NET_WM_STATE_FOCUSED             = atoms["_NET_WM_STATE_FOCUSED"];
    NET_FRAME_EXTENTS                = atoms["_NET_FRAME_EXTENTS"];
    NET_ACTIVE_WINDOW                = atoms["_NET_ACTIVE_WINDOW"];
    NET_CLIENT_LIST                  = atoms["_NET_CLIENT_LIST"];
    NET_CLIENT_LIST_STACKING         = atoms["_NET_CLIENT_LIST_STACKING"];
    NET_SUPPORTING_WM_CHECK          = atoms["_NET_SUPPORTING_WM_CHECK"];
    NET_WM_WINDOW_TYPE               = atoms["_NET_WM_WINDOW_TYPE"];
    NET_WM_WINDOW_TYPE_DOCK          = atoms["_NET_WM_WINDOW_TYPE_DOCK"];
    NET_WM_WINDOW_TYPE_DIALOG        = atoms["_NET_WM_WINDOW_TYPE_DIALOG"];
    NET_WM_WINDOW_TYPE_DESKTOP       = atoms["_NET_WM_WINDOW_TYPE_DESKTOP"];
    NET_WM_WINDOW_TYPE_NOTIFICATION  = atoms["_NET_WM_WINDOW_TYPE_NOTIFICATION"];
    NET_WM_WINDOW_TYPE_TOOLTIP       = atoms["_NET_WM_WINDOW_TYPE_TOOLTIP"];
    NET_WM_WINDOW_TYPE_DND           = atoms["_NET_WM_WINDOW_TYPE_DND"];
    NET_WM_WINDOW_TYPE_DROPDOWN_MENU = atoms["_NET_WM_WINDOW_TYPE_DROPDOWN_MENU"];
    NET_WM_WINDOW_TYPE_POPUP_MENU    = atoms["_NET_WM_WINDOW_TYPE_POPUP_MENU"];
    NET_WM_WINDOW_TYPE_MENU          = atoms["_NET_WM_WINDOW_TYPE_MENU"];
    NET_CLOSE_WINDOW                 = atoms["_NET_CLOSE_WINDOW"];
    NET_NUMBER_OF_DESKTOPS           = atoms["_NET_NUMBER_OF_DESKTOPS"];
    NET_CURRENT_DESKTOP              = atoms["_NET_CURRENT_DESKTOP"];
    NET_DESKTOP_NAMES                = atoms["_NET_DESKTOP_NAMES"];
    NET_DESKTOP_GEOMETRY             = atoms["_NET_DESKTOP_GEOMETRY"];
    NET_DESKTOP_VIEWPORT             = atoms["_NET_DESKTOP_VIEWPORT"];
    NET_WORKAREA                     = atoms["_NET_WORKAREA"];
    UTF8_STRING_ATOM                 = atoms["UTF8_STRING"];
    WM_PROTOCOLS                     = atoms["WM_PROTOCOLS"];
    WM_DELETE_WINDOW                 = atoms["WM_DELETE_WINDOW"];
    WM_TAKE_FOCUS                    = atoms["WM_TAKE_FOCUS"];
    WM_STATE = atoms["WM_STATE"];
    XEMBED_INFO                      = atoms["_XEMBED_INFO"];
    NET_WM_DESKTOP                   = atoms["_NET_WM_DESKTOP"];
}

bool X11Backend::ewmh_supports_delete(WindowId win) {
    for (auto atom : xconn.get_atom_list_property(win, WM_PROTOCOLS))
        if (atom == WM_DELETE_WINDOW) return true;
    return false;
}

void X11Backend::ewmh_close_with_message(WindowId win) {
    xcb_client_message_event_t ev = {};
    ev.response_type  = XCB_CLIENT_MESSAGE;
    ev.window         = win;
    ev.format         = 32;
    ev.type           = WM_PROTOCOLS;
    ev.data.data32[0] = WM_DELETE_WINDOW;
    ev.data.data32[1] = XCB_CURRENT_TIME;
    xconn.send_event(win, XCB_EVENT_MASK_NO_EVENT, (char*)&ev);
}

void X11Backend::focus_window(WindowId win) {
    auto window = core.window_state_any(win);

    // Respect WM_HINTS.input: if explicitly False, skip xcb_set_input_focus.
    if (!window || !window->wm_never_focus) {
        xcb_set_input_focus(xconn.raw_conn(),
            XCB_INPUT_FOCUS_POINTER_ROOT, win, last_event_time_);
        xconn.set_property(root_window, NET_ACTIVE_WINDOW, XCB_ATOM_WINDOW, win);
    }

    // Send WM_TAKE_FOCUS ClientMessage if the window supports it.
    if (WM_TAKE_FOCUS != XCB_ATOM_NONE) {
        for (auto atom : xconn.get_atom_list_property(win, WM_PROTOCOLS)) {
            if (atom != WM_TAKE_FOCUS)
                continue;
            xcb_client_message_event_t ev = {};
            ev.response_type  = XCB_CLIENT_MESSAGE;
            ev.window         = win;
            ev.format         = 32;
            ev.type           = WM_PROTOCOLS;
            ev.data.data32[0] = WM_TAKE_FOCUS;
            ev.data.data32[1] = XCB_CURRENT_TIME;
            xconn.send_event(win, XCB_EVENT_MASK_NO_EVENT, (char*)&ev);
            break;
        }
    }
}

void X11Backend::ewmh_update_client_list() {
    auto wins = core.all_window_ids();
    xconn.set_property(root_window, NET_CLIENT_LIST, wins.data(), (int)wins.size());
    // _NET_CLIENT_LIST_STACKING: EWMH requires bottom-to-top z-order.
    // We don't track stacking order, so mirror _NET_CLIENT_LIST (oldest first).
    // Compliant compositors handle this gracefully.
    xconn.set_property(root_window, NET_CLIENT_LIST_STACKING, wins.data(), (int)wins.size());
}

bool X11Backend::ewmh_has_fullscreen_state(WindowId win) {
    auto states = xconn.get_atom_list_property(win, NET_WM_STATE);
    return std::find(states.begin(), states.end(), NET_WM_STATE_FULLSCREEN) != states.end();
}

void X11Backend::ewmh_set_wm_state_atom(WindowId win, xcb_atom_t atom, bool enabled) {
    auto states = xconn.get_atom_list_property(win, NET_WM_STATE);
    states.erase(std::remove(states.begin(), states.end(), atom), states.end());
    if (enabled)
        states.push_back(atom);
    const xcb_atom_t* data = states.empty() ? nullptr : states.data();
    xconn.set_property(win, NET_WM_STATE, XCB_ATOM_ATOM, data, (int)states.size());
}

void X11Backend::ewmh_set_fullscreen_state_property(WindowId win, bool enabled) {
    ewmh_set_wm_state_atom(win, NET_WM_STATE_FULLSCREEN, enabled);
}

void X11Backend::ewmh_set_frame_extents(WindowId win, uint32_t bw) {
    if (NET_FRAME_EXTENTS == XCB_ATOM_NONE || win == NO_WINDOW)
        return;
    uint32_t extents[4] = { bw, bw, bw, bw }; // left, right, top, bottom
    xconn.set_property(win, NET_FRAME_EXTENTS, XCB_ATOM_CARDINAL, extents, 4);
}

void X11Backend::ewmh_apply_fullscreen(WindowId win, bool enabled) {
    (void)core.dispatch(command::SetWindowFullscreen{ win, enabled });
    ewmh_set_fullscreen_state_property(win, enabled);
}

void X11Backend::ewmh_update_desktop_props() {
    uint32_t n_ws = (uint32_t)core.workspace_count();
    if (n_ws == 0)
        return;

    xconn.set_property(root_window, NET_NUMBER_OF_DESKTOPS, XCB_ATOM_CARDINAL, n_ws);

    std::string names;
    for (const auto& ws : core.workspace_states()) {
        names += ws.name;
        names += '\0';
    }
    xconn.set_property(root_window, NET_DESKTOP_NAMES, UTF8_STRING_ATOM, names);

    // _NET_DESKTOP_GEOMETRY — one [width, height] for all desktops (no large desktop / panning)
    auto     geo      = xconn.get_window_geometry(root_window);
    uint32_t screen_w = geo ? (uint32_t)geo->width  : 1280u;
    uint32_t screen_h = geo ? (uint32_t)geo->height : 720u;
    uint32_t geom[2]  = { screen_w, screen_h };
    xconn.set_property(root_window, NET_DESKTOP_GEOMETRY, XCB_ATOM_CARDINAL,
        (const xcb_atom_t*)geom, 2);

    // _NET_DESKTOP_VIEWPORT — [x, y] per workspace; all zero (no virtual desktops)
    std::vector<uint32_t> vp(n_ws * 2, 0u);
    xconn.set_property(root_window, NET_DESKTOP_VIEWPORT, XCB_ATOM_CARDINAL,
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
    xconn.set_property(root_window, NET_WORKAREA, XCB_ATOM_CARDINAL,
        (const xcb_atom_t*)wa.data(), (int)wa.size());
}

void X11Backend::ewmh_init() {
    ewmh_intern_atoms();

    ewmh_wm_window = xconn.generate_id();
    xconn.create_window(ewmh_wm_window, root_window, -1, -1, 1, 1,
        XCB_WINDOW_CLASS_INPUT_ONLY);

    xconn.set_property(ewmh_wm_window, NET_SUPPORTING_WM_CHECK, &ewmh_wm_window, 1);
    xconn.set_property(ewmh_wm_window, NET_WM_NAME, UTF8_STRING_ATOM, std::string("SirenWM"));
    xconn.set_property(root_window,    NET_SUPPORTING_WM_CHECK, &ewmh_wm_window, 1);

    const xcb_atom_t supported[] = {
        NET_SUPPORTED, NET_WM_NAME, NET_ACTIVE_WINDOW,
        NET_CLIENT_LIST, NET_CLIENT_LIST_STACKING, NET_SUPPORTING_WM_CHECK,
        NET_WM_STATE, NET_WM_STATE_FULLSCREEN, NET_WM_STATE_HIDDEN, NET_WM_STATE_FOCUSED,
        NET_FRAME_EXTENTS,
        NET_WM_WINDOW_TYPE, NET_WM_WINDOW_TYPE_DOCK, NET_WM_WINDOW_TYPE_DIALOG,
        NET_CLOSE_WINDOW, NET_NUMBER_OF_DESKTOPS, NET_CURRENT_DESKTOP, NET_DESKTOP_NAMES,
        NET_DESKTOP_GEOMETRY, NET_DESKTOP_VIEWPORT, NET_WORKAREA,
        NET_WM_DESKTOP,
    };
    xconn.set_property(root_window, NET_SUPPORTED, XCB_ATOM_ATOM,
        supported, (int)(sizeof(supported) / sizeof(supported[0])));

    xconn.set_property(root_window, NET_CURRENT_DESKTOP, XCB_ATOM_CARDINAL, 0u);
    ewmh_update_desktop_props();
    ewmh_update_client_list();

    xcb_window_t none = XCB_WINDOW_NONE;
    xconn.set_property(root_window, NET_ACTIVE_WINDOW, &none, 1);

    xconn.flush();
    LOG_INFO("EWMH: initialized, wm_window=%d", ewmh_wm_window);
}

void X11Backend::handle(event::ManageWindowQuery& ev) {
    WindowId win = ev.window;
    if (NET_WM_WINDOW_TYPE == XCB_ATOM_NONE)
        return;

    auto types     = xconn.get_atom_list_property(win, NET_WM_WINDOW_TYPE);
    bool skip_type =
        has_atom(types, NET_WM_WINDOW_TYPE_DOCK) ||
        has_atom(types, NET_WM_WINDOW_TYPE_DESKTOP) ||
        has_atom(types, NET_WM_WINDOW_TYPE_NOTIFICATION) ||
        has_atom(types, NET_WM_WINDOW_TYPE_TOOLTIP) ||
        has_atom(types, NET_WM_WINDOW_TYPE_DND) ||
        has_atom(types, NET_WM_WINDOW_TYPE_DROPDOWN_MENU) ||
        has_atom(types, NET_WM_WINDOW_TYPE_POPUP_MENU) ||
        has_atom(types, NET_WM_WINDOW_TYPE_MENU);
    if (skip_type) {
        LOG_DEBUG("EWMH: skipping utility window %d by _NET_WM_WINDOW_TYPE", win);
        ev.manage = false;
        return;
    }

    if (has_xembed_info_property(xconn, win, XEMBED_INFO)) {
        auto [instance, cls] = xconn.get_wm_class(win);
        auto title = xconn.get_text_property(win, NET_WM_NAME, UTF8_STRING_ATOM);
        auto [w, h]          = window_size(xconn, win);

        bool identifiable   = !instance.empty() || !cls.empty() || !title.empty();
        bool tray_like_size = (w > 0 && h > 0 && w <= 128 && h <= 128);

        if (!identifiable || tray_like_size) {
            LOG_DEBUG("EWMH: skipping XEMBED tray-like window %d (class='%s' instance='%s' size=%dx%d)",
                win, cls.c_str(), instance.c_str(), w, h);
            ev.manage = false;
            return;
        }

        LOG_DEBUG("EWMH: allowing XEMBED toplevel window %d (class='%s' instance='%s' size=%dx%d)",
            win, cls.c_str(), instance.c_str(), w, h);
    }
}

bool X11Backend::handle(event::CloseWindowRequest ev) {
    if (ewmh_supports_delete(ev.window))
        ewmh_close_with_message(ev.window);
    else
        xconn.kill_client(ev.window);
    xconn.flush();
    return true;
}

void X11Backend::notify(event::WindowMapped ev) {
    // Paint initial border; skip if already focused (color set by update_focus).
    if (ev.window != border_painted_focused_)
        set_border_color(ev.window, border_unfocused_pixel);

    // ICCCM §4.1.3: set WM_STATE to NormalState when managing a window.
    if (WM_STATE != XCB_ATOM_NONE) {
        uint32_t data[2] = { 1 /* NormalState */, XCB_WINDOW_NONE };
        xcb_change_property(xconn.raw_conn(), XCB_PROP_MODE_REPLACE, ev.window,
            WM_STATE, WM_STATE, 32, 2, data);
    }
    // _NET_WM_DESKTOP: expose workspace index to panels and pagers.
    if (NET_WM_DESKTOP != XCB_ATOM_NONE) {
        int ws = core.workspace_of_window(ev.window);
        if (ws >= 0)
            xconn.set_property(ev.window, NET_WM_DESKTOP, XCB_ATOM_CARDINAL, (uint32_t)ws);
    }
    ewmh_update_client_list();
    // _NET_WM_STATE_HIDDEN: clear on map (window is now visible on its workspace).
    if (NET_WM_STATE_HIDDEN != XCB_ATOM_NONE)
        ewmh_set_wm_state_atom(ev.window, NET_WM_STATE_HIDDEN, false);
    {
        auto w = core.window_state_any(ev.window);
        // Self-managed windows already know their position; pinning to mon.x/mon.y
        // breaks render child placement. WM-pinned borderless windows are already
        // fullscreen-equivalent — applying fullscreen on top would cause bars/tray to lower.
        bool skip_fullscreen = w && w->borderless && !w->self_managed;
        bool self_managed    = w && w->is_self_managed();
        if (!skip_fullscreen && ewmh_has_fullscreen_state(ev.window) && should_apply_fullscreen_now(core, ev.window)) {
            if (self_managed)
                (void)core.dispatch(command::SetWindowFullscreen{
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

    // xcb_map_window puts the window on top; re-raise bars so tray stays visible.
    {
        auto w = core.window_state_any(ev.window);
        if (w && (w->borderless || w->fullscreen))
            runtime.emit(core, event::RaiseDocks{});
    }
}

void X11Backend::notify(event::WindowUnmapped ev) {
    if (WM_STATE != XCB_ATOM_NONE && ev.window != NO_WINDOW) {
        // ICCCM §4.1.4: WithdrawnState when unmanaged; IconicState when hidden
        // by workspace switch so compositors keep tracking the window.
        uint32_t state   = ev.withdrawn ? 0u /* WithdrawnState */ : 3u /* IconicState */;
        uint32_t data[2] = { state, XCB_WINDOW_NONE };
        xcb_change_property(xconn.raw_conn(), XCB_PROP_MODE_REPLACE, ev.window,
            WM_STATE, WM_STATE, 32, 2, data);
    }
    // _NET_WM_STATE_HIDDEN: set when hidden by workspace switch, clear on withdraw.
    if (NET_WM_STATE_HIDDEN != XCB_ATOM_NONE && ev.window != NO_WINDOW)
        ewmh_set_wm_state_atom(ev.window, NET_WM_STATE_HIDDEN, !ev.withdrawn);
    ewmh_update_client_list();
}

void X11Backend::update_focus(event::FocusChanged ev) {
    xconn.set_property(root_window, NET_ACTIVE_WINDOW, XCB_ATOM_WINDOW, ev.window);
    if (NET_WM_STATE_FOCUSED != XCB_ATOM_NONE) {
        if (border_painted_focused_ != NO_WINDOW && border_painted_focused_ != ev.window)
            ewmh_set_wm_state_atom(border_painted_focused_, NET_WM_STATE_FOCUSED, false);
        if (ev.window != NO_WINDOW)
            ewmh_set_wm_state_atom(ev.window, NET_WM_STATE_FOCUSED, true);
    }
    if (border_painted_focused_ != NO_WINDOW && border_painted_focused_ != ev.window)
        set_border_color(border_painted_focused_, border_unfocused_pixel);
    if (ev.window != NO_WINDOW)
        set_border_color(ev.window, border_focused_pixel);
    border_painted_focused_ = ev.window;

    // Sync pointer barriers: barriers follow focus, not borderless-activation events.
    // This is the single source of truth for barrier state — on(BorderlessActivated/Deactivated)
    // no longer touch barriers to avoid races with FocusChanged.
    auto w = (ev.window != NO_WINDOW) ? core.window_state_any(ev.window) : nullptr;
    if (w && w->borderless) {
        int ws_id   = core.workspace_of_window(ev.window);
        int mon_idx = core.monitor_of_workspace(ws_id);
        if (mon_idx >= 0)
            set_pointer_barriers(ev.window, mon_idx);
    } else {
        xconn.ungrab_pointer();
        clear_pointer_barriers();
    }
}

void X11Backend::notify(event::WorkspaceSwitched ev) {
    ewmh_update_desktop_props();
    xconn.set_property(root_window, NET_CURRENT_DESKTOP,
        XCB_ATOM_CARDINAL, (uint32_t)ev.workspace_id);

    // A window may carry _NET_WM_STATE_FULLSCREEN while on a hidden workspace;
    // defer apply until the workspace becomes visible.
    auto ws = core.workspace_state(ev.workspace_id);
    if (!ws)
        return;

    for (const auto& w : ws->windows) {
        if (!w) continue;
        auto win = w->id;
        if (core.is_window_hidden_by_workspace(win)) continue;
        if (w->is_self_managed()) continue;                   // client owns geometry: skip fullscreen pin
        if (w->borderless && !w->self_managed) continue;     // WM-pinned borderless: fullscreen would lower bars/tray
        if (!core.is_window_fullscreen(win) && ewmh_has_fullscreen_state(win))
            ewmh_apply_fullscreen(win, true);
    }
}

void X11Backend::notify(event::WindowAssignedToWorkspace ev) {
    if (NET_WM_DESKTOP == XCB_ATOM_NONE)
        return;
    xconn.set_property(ev.window, NET_WM_DESKTOP, XCB_ATOM_CARDINAL, (uint32_t)ev.workspace_id);
}

bool X11Backend::handle(event::ClientMessageEv ev) {
    if (ev.type == NET_WM_STATE) {
        xcb_atom_t a1 = (xcb_atom_t)ev.data[1];
        xcb_atom_t a2 = (xcb_atom_t)ev.data[2];
        if (a1 != NET_WM_STATE_FULLSCREEN && a2 != NET_WM_STATE_FULLSCREEN)
            return true;

        auto window = core.window_state_any(ev.window);
        if (!window) return true;

        bool     enable = false;
        uint32_t action = ev.data[0];  // 0=remove, 1=add, 2=toggle
        if (action == 0) enable = false;
        else if (action == 1) enable = true;
        else if (action == 2) enable = !core.is_window_fullscreen(ev.window);
        else return true;

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

    if (ev.type == NET_ACTIVE_WINDOW) {
        auto window = core.window_state_any(ev.window);
        if (!window)
            return false;
        if (!window->is_visible())
            return true;

        int ws_id = core.workspace_of_window(ev.window);
        // Do not force workspace jumps on external focus requests.
        if (ws_id >= 0 && !core.is_workspace_visible(ws_id))
            return true;

        (void)core.dispatch(command::FocusWindow{ ev.window });
        request_focus(ev.window, kFocusEWMH);
        return true;
    }

    if (ev.type == NET_CURRENT_DESKTOP) {
        int ws_id = (int)ev.data[0];
        if (ws_id < 0 || ws_id >= core.workspace_count())
            return true;
        (void)core.dispatch(command::SwitchWorkspace{ ws_id, std::nullopt });
        return true;
    }

    if (ev.type == NET_CLOSE_WINDOW) {
        (void)handle(event::CloseWindowRequest{ ev.window });
        return true;
    }

    return false;
}