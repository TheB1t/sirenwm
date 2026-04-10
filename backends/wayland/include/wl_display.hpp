#pragma once

// ---------------------------------------------------------------------------
// WlDisplay — RAII owner of wl_display and the Wayland socket.
//
// Responsibilities:
//   - Create/destroy wl_display (and implicitly the event loop)
//   - Create or inherit the Wayland socket (exec-restart aware)
//   - Expose event_fd() for the poll loop
//   - flush_clients() / dispatch_events() for the run loop
//   - prepare_exec_restart(): drop O_CLOEXEC, set env vars for child
// ---------------------------------------------------------------------------

#include <string>

extern "C" {
#include <wayland-server-core.h>
}

class WlDisplay {
public:
    WlDisplay();
    ~WlDisplay();

    WlDisplay(const WlDisplay&)            = delete;
    WlDisplay& operator=(const WlDisplay&) = delete;

    wl_display*    get()      const noexcept { return display_; }
    wl_event_loop* ev_loop()  const noexcept { return ev_loop_; }

    // fd to poll for Wayland client activity.
    int event_fd() const noexcept;

    // Socket name (e.g. "wayland-1").
    const std::string& socket_name() const noexcept { return socket_name_; }

    // Flush pending client events.
    void flush_clients() noexcept;

    // Dispatch queued events; non-blocking (timeout = 0).
    void dispatch_events() noexcept;

    // Call before execv() on exec-restart: drop O_CLOEXEC on socket fd
    // and export SIRENWM_WL_SOCKET_FD / SIRENWM_WL_SOCKET_NAME so the
    // new process can inherit the socket without clients noticing.
    void prepare_exec_restart() noexcept;

private:
    wl_display*    display_     = nullptr;
    wl_event_loop* ev_loop_     = nullptr;
    int            socket_fd_   = -1;
    std::string    socket_name_;

    void init_socket();
};
