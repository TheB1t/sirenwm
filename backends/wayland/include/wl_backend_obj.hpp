#pragma once

// ---------------------------------------------------------------------------
// WlBackendObj — RAII owner of wlr_backend.
// ---------------------------------------------------------------------------

extern "C" {
#include <wayland-server-core.h>
#include <wlr/backend.h>
}

class WlBackendObj {
    public:
        explicit WlBackendObj(wl_event_loop* ev_loop);
        ~WlBackendObj();

        WlBackendObj(const WlBackendObj&)            = delete;
        WlBackendObj& operator=(const WlBackendObj&) = delete;

        wlr_backend* get() const noexcept { return backend_; }

        bool start();

        wl_signal& new_output_signal() noexcept { return backend_->events.new_output; }
        wl_signal& new_input_signal()  noexcept { return backend_->events.new_input;  }

    private:
        wlr_backend* backend_ = nullptr;
};
