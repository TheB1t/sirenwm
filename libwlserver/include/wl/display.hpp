#pragma once

#include <wl/event_loop.hpp>

#include <cstdlib>
#include <string>

extern "C" {
#include <wayland-server-core.h>
}

namespace wl {

class Display {
    public:
        Display();
        ~Display();

        Display(const Display&)                      = delete;
        Display&           operator=(const Display&) = delete;
        Display(Display&&) noexcept;
        Display&           operator=(Display&&) noexcept;

        const std::string& add_socket_auto();
        bool               add_socket(const char* name);
        bool               add_socket_fd(int fd);

        void               run();
        void               terminate();
        void               flush_clients();

        EventLoop          event_loop() const noexcept;
        const std::string& socket_name() const noexcept;

        wl_display* raw() const noexcept { return display_; }

    private:
        wl_display* display_ = nullptr;
        std::string socket_name_;
};

} // namespace wl
