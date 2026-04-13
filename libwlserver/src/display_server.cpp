#include <wl/server/display_server.hpp>

#include <wl/display.hpp>
#include <wl/server/admin.hpp>
#include <wl/server/compositor.hpp>
#include <wl/server/output_x11.hpp>
#include <wl/server/overlay_manager.hpp>
#include <wl/server/output.hpp>
#include <wl/server/seat.hpp>
#include <wl/server/shm.hpp>
#include <wl/server/xwayland.hpp>
#include <wl/server/xdg_decoration.hpp>
#include <wl/server/xdg_shell.hpp>

#include <cstdio>
#include <cstdlib>
#include <poll.h>

namespace wl::server {

namespace {

struct AdminSurfaceSink final : XwmSurfaceSink {
    explicit AdminSurfaceSink(Admin& admin_ref) : admin(admin_ref) {}

    uint32_t add_surface(const std::string& app_id, const std::string& title, uint32_t pid) override {
        return admin.add_surface(app_id, title, pid);
    }

    void surface_mapped(uint32_t id) override {
        admin.surface_mapped(id);
    }

    void surface_destroyed(uint32_t id) override {
        admin.surface_destroyed(id);
    }

    void set_surface_wl_id(uint32_t id, wl::server::SurfaceId sid) override {
        admin.set_surface_wl_id(id, sid);
    }

    void surface_title_changed(uint32_t id, const std::string& title) override {
        admin.surface_title_changed(id, title);
    }

    void surface_app_id_changed(uint32_t id, const std::string& app_id) override {
        admin.surface_app_id_changed(id, app_id);
    }

    void surface_committed(uint32_t id, int32_t width, int32_t height) override {
        admin.surface_committed(id, width, height);
    }

    Admin& admin;
};

struct MainAdminListener final : AdminListener {
    wl::server::XdgShell& xdg_shell;
    Admin&                admin;
    wl::server::Seat&     seat;
    OutputX11&            output;
    XWayland&             xwayland;

    MainAdminListener(wl::server::XdgShell& x, Admin& a, wl::server::Seat& s,
        OutputX11& o, XWayland& xwl)
        : xdg_shell(x), admin(a), seat(s), output(o), xwayland(xwl) {}

    void on_configure(uint32_t id, int32_t x, int32_t y, int32_t w, int32_t h) override {
        auto* surf = admin.surface(id);
        if (surf && surf->toplevel_id != 0)
            xdg_shell.configure_toplevel(surf->toplevel_id, w, h);
        else if (xwayland.is_xwayland_surface(id))
            xwayland.configure_window(id, x, y, w, h);
        output.request_repaint();
    }
    void on_close(uint32_t id) override {
        auto* surf = admin.surface(id);
        if (surf && surf->toplevel_id != 0)
            xdg_shell.close_toplevel(surf->toplevel_id);
        else if (xwayland.is_xwayland_surface(id))
            xwayland.close_window(id);
    }
    void on_activate(uint32_t id, bool activated) override {
        if (xwayland.is_xwayland_surface(id)) {
            xwayland.activate_window(id, activated);
            return;
        }
        auto* surf = admin.surface(id);
        if (!surf || surf->toplevel_id == 0) return;
        auto* tl = xdg_shell.toplevel(surf->toplevel_id);
        if (!tl || !tl->surface_id) return;
        if (activated)
            seat.send_keyboard_enter(tl->surface_id);
        else
            seat.send_keyboard_leave(tl->surface_id);
    }
    void on_visibility(uint32_t, bool) override { output.request_repaint(); }
    void on_stacking(uint32_t, uint32_t) override { output.request_repaint(); }
    void on_border(uint32_t, uint32_t, uint32_t) override { output.request_repaint(); }
    void on_grab_pointer() override { output.set_pointer_grabbed(true); }
    void on_ungrab_pointer() override { output.set_pointer_grabbed(false); }
};

} // namespace

int run_display_server(const DisplayServerOptions& options) {
    const int width  = options.width;
    const int height = options.height;

    wl::Display display;
    const auto& socket = display.add_socket_auto();

    wl::server::Shm           shm(display);
    wl::server::Compositor    compositor(display, shm);
    wl::server::Seat          seat(display, compositor);
    wl::server::Subcompositor subcompositor(display);
    wl::server::Output        wl_output(display, width, height, 60000);

    OverlayManager            overlay_mgr;
    Admin                     admin(display, overlay_mgr);
    wl::server::XdgShell      xdg_shell(display, compositor);
    wl::server::XdgDecoration xdg_decoration(display);
    xdg_shell.set_listener(&admin);
    AdminSurfaceSink          xwm_sink(admin);

    XWayland xwayland(display, compositor, xwm_sink, width, height);

    OutputX11 output(width, height);
    if (!output.valid()) {
        std::fprintf(stderr, "sirenwm-wayland(display-server): failed to create X11 output\n");
        return EXIT_FAILURE;
    }

    overlay_mgr.set_on_changed([&output]() { output.request_repaint(); });

    MainAdminListener listener(xdg_shell, admin, seat, output, xwayland);
    admin.set_listener(&listener);

    admin.output_added(1, "X11-1", 0, 0, width, height, 60000);

    std::fprintf(stdout, "WAYLAND_DISPLAY=%s\n", socket.c_str());
    if (xwayland.valid()) {
        std::fprintf(stdout, "DISPLAY=%s\n", xwayland.display_name().c_str());
        setenv("DISPLAY", xwayland.display_name().c_str(), 1);
        std::fprintf(stderr, "sirenwm-wayland(display-server): XWayland on DISPLAY=%s\n",
            xwayland.display_name().c_str());
    } else {
        unsetenv("DISPLAY");
    }
    std::fflush(stdout);
    std::fprintf(stderr, "sirenwm-wayland(display-server): listening on %s (%dx%d)\n",
        socket.c_str(), width, height);

    auto wl_loop = display.event_loop();

    bool running = true;
    while (running) {
        display.flush_clients();

        struct pollfd fds[3];
        int           nfds = 2;
        fds[0].fd          = wl_loop.fd();
        fds[0].events      = POLLIN;
        fds[1].fd          = output.fd();
        fds[1].events      = POLLIN;

        int xcb_fd = xwayland.xcb_fd();
        if (xcb_fd >= 0) {
            fds[2].fd     = xcb_fd;
            fds[2].events = POLLIN;
            nfds = 3;
        }

        int ret = poll(fds, static_cast<nfds_t>(nfds), 16);
        if (ret < 0) break;

        if (fds[0].revents & POLLIN) {
            wl_loop.dispatch(0);
            output.request_repaint();
        }

        if (fds[1].revents & POLLIN) {
            if (!output.pump_events(admin, seat, xdg_shell)) {
                running = false;
                break;
            }
        }

        if (nfds > 2 && (fds[2].revents & POLLIN)) {
            xwayland.dispatch();
            output.request_repaint();
        }

        output.repaint(compositor, xdg_shell, admin);
        wl_loop.dispatch_idle();
    }

    display.terminate();
    return EXIT_SUCCESS;
}

} // namespace wl::server
