#include <x11_backend.hpp>
#include <x11_ports.hpp>

#include <domain/core.hpp>
#include <support/log.hpp>
#include <runtime/runtime.hpp>

#include <cstdlib>
#include <ctime>

// Include Xfixes after all project headers to avoid Xlib macro pollution.
#include <X11/extensions/Xfixes.h>

X11Backend::X11Backend(Core& core_ref, Runtime& runtime_ref)
    : core(core_ref), runtime(runtime_ref) {
    root_window         = xconn.root_window();
    render_port_impl    = backend::x11::create_render_port(xconn);
    input_port_impl     = backend::x11::create_input_port(xconn, key_syms);
    monitor_port_impl   = backend::x11::create_monitor_port(xconn, runtime_ref);
    keyboard_port_impl  = backend::x11::create_keyboard_port(xconn);
    tray_host_port_impl = backend::x11::create_tray_host_port(xconn, core_ref);
#ifdef SIRENWM_DEBUG_UI
    gl_port_impl = backend::x11::create_gl_port();
#endif
    uint32_t root_event_mask = XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY
        | XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_POINTER_MOTION
        | XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW
        | XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_PROPERTY_CHANGE
        | XCB_EVENT_MASK_KEY_PRESS;

    xcb_cursor_t cursor     = xconn.create_left_ptr_cursor();
    uint32_t     cursor_val = cursor;
    xconn.change_window_attributes(root_window, XCB_CW_CURSOR, &cursor_val);
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

    // Seed focused_monitor from the actual pointer position so that after an
    // exec-restart the WM doesn't assume monitor 0 when the cursor is elsewhere.
    // Without this, adopt_existing_windows → SwitchWorkspace → sync_current_focus
    // gives X focus to the game on monitor 0 even if the pointer is on monitor 1.
    auto ptr = xconn.query_pointer();
    if (ptr.valid)
        core.focus_monitor_at_point(ptr.x, ptr.y);
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

    xconn.change_property(root_window, XCB_ATOM_RESOURCE_MANAGER,
        XCB_ATOM_STRING, 8, (uint32_t)res.size(), res.c_str());
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
    ewmh_on_workspace_switched(ev);
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
            bool already_on_mon = w->size().x() > 0 && w->size().y() > 0 &&
                mon.contains(w->pos());
            if (!already_on_mon) {
                int nx, ny;
                if (w->size().x() >= mon.size().x() || w->size().y() >= mon.size().y()) {
                    nx = mon.pos().x();
                    ny = mon.pos().y();
                } else {
                    nx = mon.pos().x() + (mon.size().x() - w->size().x()) / 2;
                    ny = mon.pos().y() + (mon.size().y() - w->size().y()) / 2;
                }
                (void)core.dispatch(command::atom::SetWindowGeometry{
                    ev.window, { nx, ny }, w->size() });
            }
        }
    }

    ewmh_on_window_assigned_to_workspace(ev);
}

void X11Backend::on(event::WindowAdopted ev) {
    ewmh_on_window_mapped(event::WindowMapped{ ev.window });
    if (!ev.currently_visible)
        ewmh_on_window_unmapped(event::WindowUnmapped{ ev.window, false });
}

X11Window* X11Backend::x11_window(WindowId win) {
    if (win == NO_WINDOW) return nullptr;
    auto w = core.window_mut(win);
    if (!w) return nullptr;
    return static_cast<X11Window*>(w.get());
}

void X11Backend::clear_pointer_barriers() {
    if (barrier_window_ == NO_WINDOW)
        return;
    Display* dpy = xconn.xlib_display();
    for (auto& b : barriers_) {
        if (b) {
            XFixesDestroyPointerBarrier(dpy, b);
            b = 0;
        }
    }
    barrier_window_  = NO_WINDOW;
    barrier_mon_idx_ = -1;
    LOG_DEBUG("pointer barriers cleared");
}

void X11Backend::set_pointer_barriers(WindowId win, int mon_idx) {
    if (barrier_window_ == win && barrier_mon_idx_ == mon_idx)
        return;
    clear_pointer_barriers();

    const auto& mons = core.monitor_states();
    if (mon_idx < 0 || mon_idx >= (int)mons.size())
        return;

    auto [phy_pos, phy_size] = mons[(size_t)mon_idx].physical();
    int      x1 = phy_pos.x();
    int      y1 = phy_pos.y();
    int      x2 = phy_pos.x() + phy_size.x();
    int      y2 = phy_pos.y() + phy_size.y();

    Display* dpy  = xconn.xlib_display();
    Window   root = (Window)root_window;

    barriers_[0] = XFixesCreatePointerBarrier(dpy, root, x1, y1, x1, y2, BarrierPositiveX, 0, nullptr);
    barriers_[1] = XFixesCreatePointerBarrier(dpy, root, x2, y1, x2, y2, BarrierNegativeX, 0, nullptr);
    barriers_[2] = XFixesCreatePointerBarrier(dpy, root, x1, y1, x2, y1, BarrierPositiveY, 0, nullptr);
    barriers_[3] = XFixesCreatePointerBarrier(dpy, root, x1, y2, x2, y2, BarrierNegativeY, 0, nullptr);

    barrier_window_  = win;
    barrier_mon_idx_ = mon_idx;
    LOG_DEBUG("pointer barriers set for window %d: [%d,%d %dx%d]",
        win, x1, y1, x2 - x1, y2 - y1);
}

void X11Backend::shutdown() {
    xconn.shutdown();
}

backend::BackendPorts X11Backend::ports() {
    return backend::BackendPorts{
        .input    = *input_port_impl,
        .monitor  = *monitor_port_impl,
        .render   = *render_port_impl,
        .keyboard = *keyboard_port_impl,
#ifdef SIRENWM_DEBUG_UI
        .gl = gl_port_impl.get(),
#else
        .gl = nullptr,
#endif
        .tray_host = tray_host_port_impl.get(),
    };
}

std::shared_ptr<swm::Window> X11Backend::create_window(WindowId id) {
    auto w = std::make_shared<X11Window>(xconn, ewmh_atoms_);
    w->id = id;
    return w;
}
