#pragma once

// ---------------------------------------------------------------------------
// WlListener — type-safe wrapper around wl_listener.
//
// Usage:
//   WlListener on_new_output_;
//   on_new_output_.connect(&backend->events.new_output, [this](void* data) {
//       handle_new_output(static_cast<wlr_output*>(data));
//   });
//
// The listener is automatically disconnected on destruction.
// WlListener is non-copyable; it may be moved (connection transfers).
// ---------------------------------------------------------------------------

#include <functional>
#include <wayland-server-core.h>

class WlListener {
public:
    WlListener() { wl_list_init(&listener_.link); }

    ~WlListener() { disconnect(); }

    WlListener(const WlListener&)            = delete;
    WlListener& operator=(const WlListener&) = delete;

    WlListener(WlListener&& o) noexcept {
        listener_ = o.listener_;
        fn_       = std::move(o.fn_);
        if (fn_) {
            listener_.notify = o.listener_.notify;
            wl_list_init(&o.listener_.link);
        }
    }

    void connect(wl_signal* signal, std::function<void(void*)> fn) {
        disconnect();
        fn_               = std::move(fn);
        listener_.notify  = &WlListener::dispatch;
        wl_signal_add(signal, &listener_);
    }

    void disconnect() {
        if (listener_.link.prev && listener_.link.prev != &listener_.link)
            wl_list_remove(&listener_.link);
        wl_list_init(&listener_.link);
    }

    bool connected() const {
        return listener_.link.prev && listener_.link.prev != &listener_.link;
    }

private:
    wl_listener                listener_{};
    std::function<void(void*)> fn_;

    static void dispatch(wl_listener* l, void* data) {
        // listener_ is the first member → pointer identity holds for standard-layout.
        auto* self = reinterpret_cast<WlListener*>(l);
        if (self->fn_)
            self->fn_(data);
    }
};
