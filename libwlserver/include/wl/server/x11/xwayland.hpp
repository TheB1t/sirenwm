#pragma once

#include <wl/display.hpp>
#include <wl/server/protocol/compositor.hpp>
#include <wl/server/x11/xwm.hpp>

#include <cstdint>
#include <memory>
#include <string>

extern "C" {
#include <wayland-server-core.h>
}

class XWaylandShell;
class XWindowManager;

class XWayland {
    public:
        XWayland(wl::Display& display, wl::server::Compositor& compositor,
            XwmSurfaceSink& sink, int output_width, int output_height);
        ~XWayland();

        XWayland(const XWayland&)            = delete;
        XWayland& operator=(const XWayland&) = delete;

        bool valid() const { return ready_; }
        const std::string& display_name() const { return display_name_; }

        void configure_window(uint32_t surface_id, int32_t x, int32_t y, int32_t w, int32_t h);
        void close_window(uint32_t surface_id);
        void activate_window(uint32_t surface_id, bool activated);

        int  xcb_fd() const;
        void dispatch();

        bool is_xwayland_surface(uint32_t surface_id) const;

    private:
        wl::Display& display_;
        wl::server::Compositor& compositor_;
        XwmSurfaceSink&         sink_;

        pid_t            xwl_pid_     = -1;
        int              wl_fd_[2]    = {-1, -1};
        int              wm_fd_[2]    = {-1, -1};
        int              x_fd_[2]     = {-1, -1};
        int              display_num_ = -1;
        std::string      display_name_;
        wl_client*       xwl_client_   = nullptr;
        wl_event_source* ready_source_ = nullptr;
        bool             ready_        = false;

        std::unique_ptr<XWaylandShell>  shell_;
        std::unique_ptr<XWindowManager> xwm_;

        int output_width_, output_height_;

        bool       open_display_sockets();
        void       unlink_display_sockets();
        bool       launch();
        void       on_ready();
        void       cleanup_dead_child();

        static int ready_callback(int fd, uint32_t mask, void* data);
};
