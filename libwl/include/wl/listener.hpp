#pragma once

extern "C" {
#include <wayland-server-core.h>
}

#include <functional>
#include <utility>

namespace wl {

class Listener {
public:
    using Callback = std::function<void(void* data)>;

    Listener() noexcept {
        wl_list_init(&listener_.link);
        listener_.notify = nullptr;
    }

    explicit Listener(Callback cb)
        : callback_(std::move(cb)) {
        wl_list_init(&listener_.link);
        listener_.notify = &Listener::dispatch;
    }

    ~Listener() { remove(); }

    Listener(const Listener&)            = delete;
    Listener& operator=(const Listener&) = delete;

    Listener(Listener&& other) noexcept
        : callback_(std::move(other.callback_)) {
        wl_list_init(&listener_.link);
        if (!wl_list_empty(&other.listener_.link)) {
            wl_list_insert(other.listener_.link.prev, &listener_.link);
            wl_list_remove(&other.listener_.link);
            wl_list_init(&other.listener_.link);
        }
        listener_.notify = &Listener::dispatch;
        other.listener_.notify = nullptr;
    }

    Listener& operator=(Listener&& other) noexcept {
        if (this != &other) {
            remove();
            callback_ = std::move(other.callback_);
            wl_list_init(&listener_.link);
            if (!wl_list_empty(&other.listener_.link)) {
                wl_list_insert(other.listener_.link.prev, &listener_.link);
                wl_list_remove(&other.listener_.link);
                wl_list_init(&other.listener_.link);
            }
            listener_.notify = &Listener::dispatch;
            other.listener_.notify = nullptr;
        }
        return *this;
    }

    void connect(wl_signal& signal) {
        remove();
        listener_.notify = &Listener::dispatch;
        wl_signal_add(&signal, &listener_);
    }

    void remove() noexcept {
        if (!wl_list_empty(&listener_.link)) {
            wl_list_remove(&listener_.link);
            wl_list_init(&listener_.link);
        }
    }

    bool connected() const noexcept { return !wl_list_empty(&listener_.link); }
    wl_listener* raw() noexcept { return &listener_; }

private:
    wl_listener listener_ = {};
    Callback    callback_;

    static void dispatch(wl_listener* listener, void* data) {
        Listener* self = wl_container_of(listener, self, listener_);
        if (self->callback_)
            self->callback_(data);
    }
};

} // namespace wl
