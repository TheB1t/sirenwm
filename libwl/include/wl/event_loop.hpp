#pragma once

extern "C" {
#include <wayland-server-core.h>
}

namespace wl {

class EventLoop {
public:
    EventLoop() noexcept = default;
    ~EventLoop();

    EventLoop(const EventLoop&)            = delete;
    EventLoop& operator=(const EventLoop&) = delete;
    EventLoop(EventLoop&&) noexcept;
    EventLoop& operator=(EventLoop&&) noexcept;

    int fd() const noexcept;
    int dispatch(int timeout_ms) noexcept;
    void dispatch_idle() noexcept;

    explicit operator bool() const noexcept;

    wl_event_loop* raw() const noexcept { return loop_; }

private:
    wl_event_loop* loop_  = nullptr;
    bool           owned_ = false;

    friend class Display;
    EventLoop(wl_event_loop* loop, bool owned) noexcept;
};

} // namespace wl
