#include <wl/display.hpp>

namespace wl {

Display::Display()
    : display_(wl_display_create()) {
    if (!display_) std::abort();
}

Display::~Display() {
    if (display_) wl_display_destroy(display_);
}

Display::Display(Display&& other) noexcept
    : display_(other.display_)
      , socket_name_(std::move(other.socket_name_)) {
    other.display_ = nullptr;
}

Display& Display::operator=(Display&& other) noexcept {
    if (this != &other) {
        if (display_) wl_display_destroy(display_);
        display_       = other.display_;
        socket_name_   = std::move(other.socket_name_);
        other.display_ = nullptr;
    }
    return *this;
}

const std::string& Display::add_socket_auto() {
    const char* name = wl_display_add_socket_auto(display_);
    if (!name) std::abort();
    socket_name_ = name;
    return socket_name_;
}

bool Display::add_socket(const char* name) {
    if (wl_display_add_socket(display_, name) != 0) return false;
    socket_name_ = name;
    return true;
}

bool Display::add_socket_fd(int fd) {
    return wl_display_add_socket_fd(display_, fd) == 0;
}

void Display::run()           { wl_display_run(display_); }
void Display::terminate()     { wl_display_terminate(display_); }
void Display::flush_clients() { wl_display_flush_clients(display_); }

EventLoop Display::event_loop() const noexcept {
    return EventLoop(wl_display_get_event_loop(display_), false);
}

const std::string& Display::socket_name() const noexcept {
    return socket_name_;
}

} // namespace wl
