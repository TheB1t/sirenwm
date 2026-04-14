#pragma once

#include <wl/display.hpp>

#include <utility>

extern "C" {
#include <wayland-server-core.h>
}

namespace wl {

template<typename Owner>
class Global {
    public:
        Global() noexcept = default;

        Global(Display& display, Owner* owner)
            : owner_(owner)
              , global_(wl_global_create(
                  display.raw(),
                  Owner::interface(),
                  Owner::version(),
                  this,
                  &Global::thunk)) {}

        ~Global() {
            if (global_) wl_global_destroy(global_);
        }

        Global(const Global&)            = delete;
        Global& operator=(const Global&) = delete;

        Global(Global&& other) noexcept
            : owner_(other.owner_)
              , global_(std::exchange(other.global_, nullptr)) {
            update_user_data();
        }

        Global& operator=(Global&& other) noexcept {
            if (this != &other) {
                if (global_) wl_global_destroy(global_);
                owner_  = other.owner_;
                global_ = std::exchange(other.global_, nullptr);
                update_user_data();
            }
            return *this;
        }

        explicit operator bool() const noexcept { return global_ != nullptr; }

    private:
        Owner*     owner_  = nullptr;
        wl_global* global_ = nullptr;

        void update_user_data() {
            if (global_) wl_global_set_user_data(global_, this);
        }

        static void thunk(wl_client* client, void* data, uint32_t version, uint32_t id) {
            auto* self = static_cast<Global*>(data);
            self->owner_->bind(client, version, id);
        }
};

} // namespace wl
