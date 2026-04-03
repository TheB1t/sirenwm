#include <x11_backend.hpp>
#include <x11_ports.hpp>

#include <core.hpp>
#include <log.hpp>
#include <runtime.hpp>

#include <cstdlib>
#include <ctime>

X11Backend::X11Backend(Core& core_ref, Runtime& runtime_ref)
    : core(core_ref), runtime(runtime_ref) {
    root_window       = xconn.root_window();
    render_port_impl   = backend::x11::create_render_port(xconn);
    input_port_impl    = backend::x11::create_input_port(xconn, key_syms);
    monitor_port_impl  = backend::x11::create_monitor_port(xconn, runtime_ref);
    keyboard_port_impl = backend::x11::create_keyboard_port(xconn);
    auto atoms = xconn.intern_atoms({ "_NET_WM_NAME", "UTF8_STRING", "_NET_WM_PID" });
    net_wm_name       = atoms["_NET_WM_NAME"];
    utf8_string       = atoms["UTF8_STRING"];
    net_wm_pid        = atoms["_NET_WM_PID"];
    uint32_t root_event_mask = XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY
        | XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_POINTER_MOTION
        | XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW
        | XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_PROPERTY_CHANGE
        | XCB_EVENT_MASK_KEY_PRESS;

    xcb_cursor_t cursor     = xconn.create_left_ptr_cursor();
    uint32_t     cursor_val = cursor;
    xconn.call(xcb_change_window_attributes, root_window, XCB_CW_CURSOR, &cursor_val);
    xconn.free_cursor(cursor);
    xconn.flush();

    // Retry loop: on exec-restart the previous process released SubstructureRedirect
    // via execv but the X server may not have processed it yet. Retry briefly.
    xcb_generic_error_t* err = nullptr;
    for (int attempt = 0; attempt < 20; ++attempt) {
        err = xconn.change_window_attributes_checked(
            root_window, XCB_CW_EVENT_MASK, &root_event_mask);
        if (!err) break;
        free(err);
        err = nullptr;
        struct timespec ts = { 0, 100'000'000 }; // 100ms
        nanosleep(&ts, nullptr);
    }
    if (err) {
        LOG_ERR("Cannot set SubstructureRedirect on root (error %d) — another WM running?", err->error_code);
        free(err);
    }

    ewmh_init();
}

X11Backend::~X11Backend() {
    if (key_syms)
        xcb_key_symbols_free(key_syms);
}

void X11Backend::on_start(Core& core) {
    ewmh_update_desktop_props();
    apply_xresources(core);
    reload_border_colors();
}

void X11Backend::apply_xresources(Core& core) {
    const auto& theme = core.current_settings().theme;
    if (theme.dpi == 0 && theme.cursor_size == 0 && theme.cursor_theme.empty())
        return;

    std::string res;
    if (theme.dpi > 0)
        res += "Xft.dpi: " + std::to_string(theme.dpi) + "\n";
    if (theme.cursor_size > 0)
        res += "Xcursor.size: " + std::to_string(theme.cursor_size) + "\n";
    if (!theme.cursor_theme.empty())
        res += "Xcursor.theme: " + theme.cursor_theme + "\n";

    xconn.call(xcb_change_property,
        XCB_PROP_MODE_REPLACE, root_window,
        XCB_ATOM_RESOURCE_MANAGER, XCB_ATOM_STRING, 8,
        (uint32_t)res.size(), res.c_str());
    xconn.flush();
    LOG_INFO("theme: set RESOURCE_MANAGER: %s", res.c_str());
}

void X11Backend::on(event::FocusChanged ev) {
    update_focus(ev);
}

void X11Backend::on(event::WorkspaceSwitched ev) {
    // Release any active pointer grab (e.g. Wine/Proton clip_window) so cursor
    // events stop routing to the hidden workspace after switching.
    xconn.ungrab_pointer();
    xconn.flush();
    notify(ev);
}

void X11Backend::on(event::WindowAssignedToWorkspace ev) {
    // Release any pointer grab the window holds (e.g. Wine/Proton raw input)
    // when it leaves a visible workspace — otherwise the grab persists after unmap.
    if (!core.is_workspace_visible(ev.workspace_id)) {
        xconn.ungrab_pointer();
        xconn.flush();
    }

    // Relocate floating windows to their new monitor when moved to a workspace
    // on a different monitor; tiled windows are handled by arrange().
    auto w = core.window_state_any(ev.window);
    if (w && w->floating) {
        int         mon_idx = core.monitor_of_workspace(ev.workspace_id);
        const auto& mons    = core.monitor_states();
        if (mon_idx >= 0 && mon_idx < (int)mons.size()) {
            const auto& mon = mons[(size_t)mon_idx];
            // Skip centering if the window already sits within the target monitor
            // (e.g. exec-restart: geometry was seeded from X at startup).
            bool already_on_mon = w->width > 0 && w->height > 0 &&
                w->x >= mon.x && w->x < mon.x + mon.width &&
                w->y >= mon.y && w->y < mon.y + mon.height;
            if (!already_on_mon) {
                int nx, ny;
                if ((int)w->width >= mon.width || (int)w->height >= mon.height) {
                    nx = mon.x;
                    ny = mon.y;
                } else {
                    nx = mon.x + (mon.width  - (int)w->width)  / 2;
                    ny = mon.y + (mon.height - (int)w->height) / 2;
                }
                (void)core.dispatch(command::SetWindowGeometry{
                    ev.window, nx, ny, w->width, w->height });
            }
        }
    }

    notify(ev);
}

void X11Backend::on(event::WindowAdopted ev) {
    notify(event::WindowMapped{ ev.window });
    if (!ev.currently_visible)
        notify(event::WindowUnmapped{ ev.window, false });
}

bool X11Backend::close_window(WindowId window) {
    return handle(event::CloseWindowRequest{ window });
}

void X11Backend::shutdown() {
    xconn.shutdown();
}

backend::MonitorPort* X11Backend::monitor_port() {
    return monitor_port_impl.get();
}

backend::KeyboardPort* X11Backend::keyboard_port() {
    return keyboard_port_impl.get();
}

backend::RenderPort* X11Backend::render_port() {
    return render_port_impl.get();
}

std::unique_ptr<backend::TrayHost>
X11Backend::create_tray_host(WindowId owner_bar_window, int bar_x, int bar_y, int bar_h, bool own_selection) {
    uint32_t bg = xconn.screen_black_pixel();
    return backend::x11::create_tray_host(xconn, owner_bar_window, bar_x, bar_y, bar_h, bg, own_selection);
}

std::string X11Backend::window_title(WindowId window) const {
    auto title = xconn.get_text_property(window, net_wm_name, utf8_string);
    if (title.empty())
        title = xconn.get_text_property(window, XCB_ATOM_WM_NAME, XCB_ATOM_STRING);
    return title;
}

uint32_t X11Backend::window_pid(WindowId window) const {
    if (net_wm_pid == XCB_ATOM_NONE)
        return 0;
    auto* conn   = const_cast<xcb_connection_t*>(xconn.raw_conn());
    auto  cookie = xcb_get_property(conn, 0, window, net_wm_pid, XCB_ATOM_CARDINAL, 0, 1);
    auto* reply  = xcb_get_property_reply(conn, cookie, nullptr);
    if (!reply)
        return 0;
    uint32_t pid = 0;
    if (reply->format == 32 && reply->value_len >= 1) {
        auto* data = (uint32_t*)xcb_get_property_value(reply);
        if (data)
            pid = data[0];
    }
    free(reply);
    return pid;
}