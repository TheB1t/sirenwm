#pragma once

// ---------------------------------------------------------------------------
// WlListener<T> — typed wrapper around wl_listener.
//
// T is the event data type emitted by the wl_signal.  The cast from void*
// is performed once, inside dispatch(), so callsites receive T* directly.
//
// For signals that carry no meaningful data use WlListener<void> (or the
// WlVoidListener alias below) — the callback receives void*.
//
// Usage:
//   WlListener<wlr_output> on_new_output_;
//   on_new_output_.connect(&backend->events.new_output,
//       [this](wlr_output* o) { handle_new_output(o); });
//
//   WlVoidListener on_frame_;
//   on_frame_.connect(&output->events.frame,
//       [this](void*) { handle_frame(); });
//
// The listener is automatically disconnected on destruction.
// WlListener is non-copyable; moving transfers the connection safely.
// ---------------------------------------------------------------------------

#include <functional>
#include <wayland-server-core.h>

template<typename T>
class WlListener {
public:
    WlListener() noexcept { wl_list_init(&raw_.link); }

    ~WlListener() { disconnect(); }

    WlListener(const WlListener&)            = delete;
    WlListener& operator=(const WlListener&) = delete;

    WlListener(WlListener&& o) noexcept {
        fn_ = std::move(o.fn_);
        wl_list_init(&raw_.link);
        if (o.connected()) {
            raw_.notify = &dispatch;
            // Atomically transplant: insert self before o, then remove o.
            wl_list_insert(o.raw_.link.prev, &raw_.link);
            wl_list_remove(&o.raw_.link);
            wl_list_init(&o.raw_.link);
        }
    }

    WlListener& operator=(WlListener&& o) noexcept {
        if (this != &o) {
            disconnect();
            fn_ = std::move(o.fn_);
            if (o.connected()) {
                raw_.notify = &dispatch;
                wl_list_insert(o.raw_.link.prev, &raw_.link);
                wl_list_remove(&o.raw_.link);
                wl_list_init(&o.raw_.link);
            }
        }
        return *this;
    }

    void connect(wl_signal* signal, std::function<void(T*)> fn) {
        disconnect();
        fn_          = std::move(fn);
        raw_.notify  = &dispatch;
        wl_signal_add(signal, &raw_);
    }

    void disconnect() noexcept {
        if (connected()) {
            wl_list_remove(&raw_.link);
            wl_list_init(&raw_.link);
        }
    }

    bool connected() const noexcept {
        return raw_.link.prev != nullptr && raw_.link.prev != &raw_.link;
    }

private:
    // raw_ MUST be the first member: dispatch() recovers `this` via
    // reinterpret_cast, which is valid only as long as WlListener is
    // standard-layout and raw_ is at offset 0.
    wl_listener raw_{};
    std::function<void(T*)> fn_;

    static void dispatch(wl_listener* l, void* data) noexcept {
        auto* self = reinterpret_cast<WlListener*>(l);
        if (self->fn_)
            self->fn_(static_cast<T*>(data));
    }
};

// Specialisation for void: the callback receives the raw void* pointer.
template<>
class WlListener<void> {
public:
    WlListener() noexcept { wl_list_init(&raw_.link); }

    ~WlListener() { disconnect(); }

    WlListener(const WlListener&)            = delete;
    WlListener& operator=(const WlListener&) = delete;

    WlListener(WlListener&& o) noexcept {
        fn_ = std::move(o.fn_);
        wl_list_init(&raw_.link);
        if (o.connected()) {
            raw_.notify = &dispatch;
            wl_list_insert(o.raw_.link.prev, &raw_.link);
            wl_list_remove(&o.raw_.link);
            wl_list_init(&o.raw_.link);
        }
    }

    WlListener& operator=(WlListener&& o) noexcept {
        if (this != &o) {
            disconnect();
            fn_ = std::move(o.fn_);
            if (o.connected()) {
                raw_.notify = &dispatch;
                wl_list_insert(o.raw_.link.prev, &raw_.link);
                wl_list_remove(&o.raw_.link);
                wl_list_init(&o.raw_.link);
            }
        }
        return *this;
    }

    void connect(wl_signal* signal, std::function<void(void*)> fn) {
        disconnect();
        fn_         = std::move(fn);
        raw_.notify = &dispatch;
        wl_signal_add(signal, &raw_);
    }

    void disconnect() noexcept {
        if (connected()) {
            wl_list_remove(&raw_.link);
            wl_list_init(&raw_.link);
        }
    }

    bool connected() const noexcept {
        return raw_.link.prev != nullptr && raw_.link.prev != &raw_.link;
    }

private:
    wl_listener raw_{};
    std::function<void(void*)> fn_;

    static void dispatch(wl_listener* l, void* data) noexcept {
        auto* self = reinterpret_cast<WlListener<void>*>(l);
        if (self->fn_)
            self->fn_(data);
    }
};

// Convenience alias for signals that carry no data.
using WlVoidListener = WlListener<void>;
