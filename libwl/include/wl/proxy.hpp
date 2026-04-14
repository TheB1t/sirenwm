#pragma once

extern "C" {
#include <wayland-client-core.h>
}

#include <functional>
#include <utility>

namespace wl {

// Type-safe RAII wrapper around a client-side wl_proxy.
//
// T is a tag type for type safety (e.g. the generated
// wl_registry, wl_compositor, or custom protocol struct).
// The actual proxy is a wl_proxy* underneath.
template<typename T>
class Proxy {
    public:
        Proxy() noexcept = default;

        explicit Proxy(T* raw, bool owned = true) noexcept
            : proxy_(raw), owned_(owned) {}

        ~Proxy() {
            reset();
        }

        Proxy(const Proxy&)            = delete;
        Proxy& operator=(const Proxy&) = delete;

        Proxy(Proxy&& other) noexcept
            : proxy_(std::exchange(other.proxy_, nullptr))
              , owned_(std::exchange(other.owned_, false)) {}

        Proxy& operator=(Proxy&& other) noexcept {
            if (this != &other) {
                reset();
                proxy_ = std::exchange(other.proxy_, nullptr);
                owned_ = std::exchange(other.owned_, false);
            }
            return *this;
        }

        void reset() noexcept {
            if (proxy_ && owned_) {
                wl_proxy_destroy(reinterpret_cast<wl_proxy*>(proxy_));
            }
            proxy_ = nullptr;
            owned_ = false;
        }

        explicit operator bool() const noexcept { return proxy_ != nullptr; }

        T* raw() noexcept { return proxy_; }
        const T* raw() const noexcept { return proxy_; }

        T* release() noexcept {
            owned_ = false;
            return std::exchange(proxy_, nullptr);
        }

    private:
        T*   proxy_ = nullptr;
        bool owned_ = false;
};

} // namespace wl
