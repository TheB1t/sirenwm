#include <wl/server/runtime/display_server.hpp>

#include <swm/ipc/endpoint.hpp>
#include <wl/display.hpp>
#include <wl/server/runtime/display_state.hpp>
#include <wl/server/ipc/backend_control_server.hpp>
#include <wl/server/protocol/compositor.hpp>
#include <wl/server/x11/output_x11.hpp>
#include <wl/server/runtime/overlay_manager.hpp>
#include <wl/server/protocol/output.hpp>
#include <wl/server/protocol/seat.hpp>
#include <wl/server/protocol/shm.hpp>
#include <wl/server/x11/xwayland.hpp>
#include <wl/server/protocol/xdg_decoration.hpp>
#include <wl/server/protocol/xdg_shell.hpp>

#include <cstdio>
#include <cstdlib>
#include <array>
#include <poll.h>
#include <signal.h>

namespace wl::server {

namespace {

struct DisplayStateSurfaceSink final : XwmSurfaceSink {
    explicit DisplayStateSurfaceSink(DisplayState& state_ref) : state(state_ref) {}

    uint32_t add_surface(const std::string& app_id, const std::string& title, uint32_t pid) override {
        return state.add_surface(app_id, title, pid);
    }

    void surface_mapped(uint32_t id) override {
        state.surface_mapped(id);
    }

    void surface_destroyed(uint32_t id) override {
        state.surface_destroyed(id);
    }

    void set_wl_surface_id(uint32_t id, wl::server::SurfaceId sid) override {
        state.set_wl_surface_id(id, sid);
    }

    void surface_title_changed(uint32_t id, const std::string& title) override {
        state.surface_title_changed(id, title);
    }

    void surface_app_id_changed(uint32_t id, const std::string& app_id) override {
        state.surface_app_id_changed(id, app_id);
    }

    void surface_committed(uint32_t id, int32_t width, int32_t height) override {
        state.surface_committed(id, width, height);
    }

    DisplayState& state;
};

struct DisplayServerListener final : swm::ipc::BackendCommandHandler {
    using swm::ipc::BackendCommandHandler::on;

    wl::server::XdgShell& xdg_shell;
    DisplayState&         state;
    wl::server::Seat&     seat;
    OutputX11&            output;
    XWayland&             xwayland;

    DisplayServerListener(wl::server::XdgShell& x, DisplayState& state_ref, wl::server::Seat& s,
        OutputX11& o, XWayland& xwl)
        : xdg_shell(x), state(state_ref), seat(s), output(o), xwayland(xwl) {}

    void on(const swm::ipc::ConfigureSurface& msg) override {
        auto* surf = state.surface(msg.surface_id);
        if (surf && surf->toplevel_id != 0)
            xdg_shell.configure_toplevel(surf->toplevel_id, msg.width, msg.height);
        else if (xwayland.is_xwayland_surface(msg.surface_id))
            xwayland.configure_window(msg.surface_id, msg.x, msg.y, msg.width, msg.height);
        output.request_repaint();
    }
    void on(const swm::ipc::CloseSurface& msg) override {
        auto* surf = state.surface(msg.surface_id);
        if (surf && surf->toplevel_id != 0)
            xdg_shell.close_toplevel(surf->toplevel_id);
        else if (xwayland.is_xwayland_surface(msg.surface_id))
            xwayland.close_window(msg.surface_id);
    }
    void on(const swm::ipc::SetSurfaceActivated& msg) override {
        const bool activated = msg.activated != 0;
        if (xwayland.is_xwayland_surface(msg.surface_id)) {
            if (auto* surf = state.surface(msg.surface_id); surf && surf->wl_surface_id) {
                if (activated)
                    seat.send_keyboard_enter(surf->wl_surface_id);
                else
                    seat.send_keyboard_leave(surf->wl_surface_id);
            }
            xwayland.activate_window(msg.surface_id, activated);
            return;
        }
        auto* surf = state.surface(msg.surface_id);
        if (!surf || surf->toplevel_id == 0) return;
        auto* tl = xdg_shell.toplevel(surf->toplevel_id);
        if (!tl || !tl->surface_id) return;
        if (activated)
            seat.send_keyboard_enter(tl->surface_id);
        else
            seat.send_keyboard_leave(tl->surface_id);
    }
    void on(const swm::ipc::SetSurfaceVisible&) override { output.request_repaint(); }
    void on(const swm::ipc::SetSurfaceStacking&) override { output.request_repaint(); }
    void on(const swm::ipc::SetSurfaceBorder&) override { output.request_repaint(); }
    void on(const swm::ipc::GrabPointer&) override { output.set_pointer_grabbed(true); }
    void on(const swm::ipc::UngrabPointer&) override { output.set_pointer_grabbed(false); }
};

} // namespace

int run_display_server(const DisplayServerOptions& options) {
    // Xwayland can notify readiness via SIGUSR1; displayfd is authoritative for us.
    // Ignore SIGUSR1 so the display-server process does not terminate unexpectedly.
    signal(SIGUSR1, SIG_IGN);

    const int                 width  = options.width;
    const int                 height = options.height;

    wl::Display               display;
    const auto&               socket = display.add_socket_auto();

    wl::server::Shm           shm(display);
    wl::server::Compositor    compositor(display, shm);
    wl::server::Seat          seat(display, compositor);
    wl::server::Subcompositor subcompositor(display);
    wl::server::Output        wl_output(display, width, height, 60000);

    OverlayManager            overlay_mgr;
    DisplayState              display_state(overlay_mgr);
    wl::server::XdgShell      xdg_shell(display, compositor);
    wl::server::XdgDecoration xdg_decoration(display);
    xdg_shell.set_event_handler(&display_state);
    DisplayStateSurfaceSink   xwm_sink(display_state);

    XWayland                  xwayland(display, compositor, xwm_sink, width, height);
    const auto                control_socket = swm::ipc::backend_socket_path(socket);
    BackendControlServer      control_server(control_socket, display_state);

    OutputX11                 output(width, height);
    if (!output.valid() || !control_server.valid()) {
        std::fprintf(stderr, "sirenwm-wayland(display-server): failed to create X11 output\n");
        return EXIT_FAILURE;
    }

    seat.set_cursor_update_callback(
        [&output](SurfaceId sid, int32_t hotspot_x, int32_t hotspot_y) {
            output.set_cursor_surface(sid, hotspot_x, hotspot_y);
            output.request_repaint();
        });

    overlay_mgr.set_on_changed([&output]() {
            output.request_repaint();
        });

    DisplayServerListener listener(xdg_shell, display_state, seat, output, xwayland);
    display_state.set_command_handler(&listener);

    display_state.output_added(1, "X11-1", 0, 0, width, height, 60000);

    std::fprintf(stdout, "WAYLAND_DISPLAY=%s\n", socket.c_str());
    std::fprintf(stdout, "SIRENWM_IPC_SOCKET=%s\n", control_socket.c_str());
    setenv("SIRENWM_IPC_SOCKET", control_socket.c_str(), 1);
    if (!xwayland.display_name().empty()) {
        std::fprintf(stdout, "DISPLAY=%s\n", xwayland.display_name().c_str());
        setenv("DISPLAY", xwayland.display_name().c_str(), 1);
        std::fprintf(stderr, "sirenwm-wayland(display-server): XWayland on DISPLAY=%s\n",
            xwayland.display_name().c_str());
    } else {
        unsetenv("DISPLAY");
    }
    std::fflush(stdout);
    std::fprintf(stderr, "sirenwm-wayland(display-server): listening on %s (%dx%d), control=%s\n",
        socket.c_str(), width, height, control_socket.c_str());

    auto wl_loop = display.event_loop();

    bool running = true;
    while (running) {
        display.flush_clients();

        std::array<struct pollfd, 5> fds {};
        int                          nfds = 3;
        fds[0].fd     = wl_loop.fd();
        fds[0].events = POLLIN;
        fds[1].fd     = output.fd();
        fds[1].events = POLLIN;
        fds[2].fd     = control_server.listener_fd();
        fds[2].events = POLLIN;

        int xcb_fd            = xwayland.xcb_fd();
        int control_client_fd = control_server.client_fd();
        if (control_client_fd >= 0) {
            fds[nfds].fd     = control_client_fd;
            fds[nfds].events = POLLIN;
            ++nfds;
        }
        if (xcb_fd >= 0) {
            fds[nfds].fd     = xcb_fd;
            fds[nfds].events = POLLIN;
            ++nfds;
        }

        int ret = poll(fds.data(), static_cast<nfds_t>(nfds), 16);
        if (ret < 0) break;

        if (fds[0].revents & POLLIN) {
            wl_loop.dispatch(0);
            output.request_repaint();
        }

        if (fds[1].revents & POLLIN) {
            if (!output.pump_events(display_state, seat, xdg_shell)) {
                running = false;
                break;
            }
        }

        if (fds[2].revents & POLLIN) {
            control_server.accept_pending();
        }

        int poll_index = 3;
        if (control_client_fd >= 0 && nfds > poll_index && (fds[poll_index].revents & POLLIN)) {
            control_server.dispatch_pending();
            ++poll_index;
        }

        if (xcb_fd >= 0 && nfds > poll_index && (fds[poll_index].revents & POLLIN)) {
            xwayland.dispatch();
            output.request_repaint();
        }

        output.repaint(compositor, xdg_shell, display_state);
        wl_loop.dispatch_idle();
    }

    display.terminate();
    return EXIT_SUCCESS;
}

} // namespace wl::server
