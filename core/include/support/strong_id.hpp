#pragma once

#include <cstddef>
#include <functional>
#include <type_traits>

namespace swm {

// Phantom-typed wrapper around an integral ID to prevent mixing IDs of
// different domains (e.g. WindowId vs MonitorId) at compile time.
//
// Usage:
//   struct WindowTag;
//   using WindowId = StrongId<WindowTag, uint32_t>;
//   WindowId w{42};
//   uint32_t raw = w.get();
//
// Construction is explicit; comparisons are only defined between IDs sharing
// the same Tag. Hash specialization is provided so StrongId is usable as a key
// in unordered containers.
template <typename Tag, typename Underlying>
class StrongId {
    static_assert(std::is_integral_v<Underlying>, "StrongId Underlying must be integral");

    Underlying value_{};

    public:
        using underlying_type = Underlying;

        constexpr StrongId() = default;
        explicit constexpr StrongId(Underlying v) : value_(v) {}

        constexpr Underlying get() const { return value_; }

        friend constexpr bool operator==(StrongId a, StrongId b) { return a.value_ == b.value_; }
        friend constexpr bool operator!=(StrongId a, StrongId b) { return a.value_ != b.value_; }
        friend constexpr bool operator<(StrongId a, StrongId b)  { return a.value_ <  b.value_; }
        friend constexpr bool operator<=(StrongId a, StrongId b) { return a.value_ <= b.value_; }
        friend constexpr bool operator>(StrongId a, StrongId b)  { return a.value_ >  b.value_; }
        friend constexpr bool operator>=(StrongId a, StrongId b) { return a.value_ >= b.value_; }
};

} // namespace swm

namespace std {
template <typename Tag, typename U>
struct hash<swm::StrongId<Tag, U>> {
    size_t operator()(swm::StrongId<Tag, U> id) const noexcept {
        return std::hash<U>{}(id.get());
    }
};
} // namespace std
