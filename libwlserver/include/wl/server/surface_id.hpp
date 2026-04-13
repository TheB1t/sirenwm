#pragma once

#include <cstdint>
#include <functional>

namespace wl::server {

class SurfaceId {
public:
    constexpr SurfaceId() noexcept = default;
    constexpr explicit SurfaceId(uint32_t v) noexcept : value_(v) {}

    constexpr uint32_t value() const noexcept { return value_; }
    constexpr explicit operator bool() const noexcept { return value_ != 0; }

    constexpr bool operator==(const SurfaceId&) const noexcept = default;

private:
    uint32_t value_ = 0;
};

} // namespace wl::server

template<>
struct std::hash<wl::server::SurfaceId> {
    std::size_t operator()(wl::server::SurfaceId id) const noexcept {
        return std::hash<uint32_t>{}(id.value());
    }
};
