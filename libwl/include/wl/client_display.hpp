#pragma once

extern "C" {
#include <wayland-client-core.h>
}

#include <string>
#include <utility>

namespace wl {

// RAII owner of a client-side wl_display connection.
class ClientDisplay {
    public:
        ClientDisplay() noexcept = default;

        // Connect to a named Wayland display.
        explicit ClientDisplay(const char* name)
            : display_(wl_display_connect(name)) {}

        // Connect via an existing fd (e.g. from socketpair).
        static ClientDisplay from_fd(int fd) {
            ClientDisplay cd;
            cd.display_ = wl_display_connect_to_fd(fd);
            return cd;
        }

        ~ClientDisplay() {
            if (display_)
                wl_display_disconnect(display_);
        }

        ClientDisplay(const ClientDisplay&)            = delete;
        ClientDisplay& operator=(const ClientDisplay&) = delete;

        ClientDisplay(ClientDisplay&& other) noexcept
            : display_(std::exchange(other.display_, nullptr)) {}

        ClientDisplay& operator=(ClientDisplay&& other) noexcept {
            if (this != &other) {
                if (display_)
                    wl_display_disconnect(display_);
                display_ = std::exchange(other.display_, nullptr);
            }
            return *this;
        }

        explicit operator bool() const noexcept { return display_ != nullptr; }

        int fd() const noexcept {
            return wl_display_get_fd(display_);
        }

        int dispatch() {
            return wl_display_dispatch(display_);
        }

        int dispatch_pending() {
            return wl_display_dispatch_pending(display_);
        }

        int roundtrip() {
            return wl_display_roundtrip(display_);
        }

        int flush() {
            return wl_display_flush(display_);
        }

        int prepare_read() {
            return wl_display_prepare_read(display_);
        }

        void cancel_read() {
            wl_display_cancel_read(display_);
        }

        int read_events() {
            return wl_display_read_events(display_);
        }

        wl_display* raw() noexcept { return display_; }

    private:
        wl_display* display_ = nullptr;
};

} // namespace wl
