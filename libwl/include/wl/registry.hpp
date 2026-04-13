#pragma once

extern "C" {
#include <wayland-client.h>
}

#include <wl/client_display.hpp>
#include <wl/proxy.hpp>

#include <cstring>

namespace wl {

// CRTP base for type-safe wl_registry handling.
//
// Derived must implement:
//   void on_global(uint32_t name, const char* interface, uint32_t version)
//   void on_global_remove(uint32_t name)            [optional — default no-op]
//
// Usage:
//   class MyRegistry : public wl::Registry<MyRegistry> {
//   public:
//       using wl::Registry<MyRegistry>::Registry;
//       void on_global(uint32_t name, const char* iface, uint32_t ver) {
//           if (match(iface, my_interface))
//               proxy_ = bind<my_type>(name, my_interface, ver);
//       }
//   };
template<typename Derived>
class Registry {
public:
    explicit Registry(ClientDisplay& display)
        : registry_(wl_display_get_registry(display.raw()), false)
    {
        static constexpr wl_registry_listener listener = {
            .global        = global_handler,
            .global_remove = global_remove_handler,
        };
        wl_registry_add_listener(registry_.raw(), &listener, static_cast<Derived*>(this));
    }

    static bool match(const char* iface, const wl_interface& target) {
        return strcmp(iface, target.name) == 0;
    }

    template<typename T>
    Proxy<T> bind(uint32_t name, const wl_interface& iface, uint32_t version) {
        return Proxy<T>(static_cast<T*>(
            wl_registry_bind(registry_.raw(), name, &iface, version)), false);
    }

    void on_global_remove(uint32_t) {}

    explicit operator bool() const noexcept { return static_cast<bool>(registry_); }

private:
    Proxy<wl_registry> registry_;

    Derived&       self()       { return static_cast<Derived&>(*this); }
    const Derived& self() const { return static_cast<const Derived&>(*this); }

    static void global_handler(void* data, wl_registry*, uint32_t name,
                               const char* iface, uint32_t version) {
        static_cast<Derived*>(data)->on_global(name, iface, version);
    }

    static void global_remove_handler(void* data, wl_registry*, uint32_t name) {
        static_cast<Derived*>(data)->on_global_remove(name);
    }
};

} // namespace wl
