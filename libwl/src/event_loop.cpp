#include <wl/event_loop.hpp>

namespace wl {

EventLoop::EventLoop(wl_event_loop* loop, bool owned) noexcept
    : loop_(loop), owned_(owned) {}

EventLoop::~EventLoop() {
    if (owned_ && loop_) wl_event_loop_destroy(loop_);
}

EventLoop::EventLoop(EventLoop&& other) noexcept
    : loop_(other.loop_), owned_(other.owned_) {
    other.loop_  = nullptr;
    other.owned_ = false;
}

EventLoop& EventLoop::operator=(EventLoop&& other) noexcept {
    if (this != &other) {
        if (owned_ && loop_) wl_event_loop_destroy(loop_);
        loop_  = other.loop_;
        owned_ = other.owned_;
        other.loop_  = nullptr;
        other.owned_ = false;
    }
    return *this;
}

int EventLoop::fd() const noexcept {
    return loop_ ? wl_event_loop_get_fd(loop_) : -1;
}

int EventLoop::dispatch(int timeout_ms) noexcept {
    return loop_ ? wl_event_loop_dispatch(loop_, timeout_ms) : -1;
}

void EventLoop::dispatch_idle() noexcept {
    if (loop_) wl_event_loop_dispatch_idle(loop_);
}

EventLoop::operator bool() const noexcept {
    return loop_ != nullptr;
}

} // namespace wl
