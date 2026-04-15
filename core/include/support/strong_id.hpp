#pragma once

#include <cstddef>
#include <functional>
#include <ostream>
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

        friend std::ostream& operator<<(std::ostream& os, StrongId id) {
            return os << id.value_;
        }
};

// StrongIdCastable — StrongId variant with implicit two-way conversion to/from
// its underlying type. Intended for IDs that cross a C-library boundary where
// the underlying type is threaded through hundreds of call sites (e.g. WindowId
// ↔ xcb_window_t). Cross-tag mixing is still rejected; the relaxation is only
// between the ID and its own Underlying. Prefer plain StrongId when this escape
// hatch isn't required.
template <typename Tag, typename Underlying>
class StrongIdCastable : public StrongId<Tag, Underlying> {
        using Base = StrongId<Tag, Underlying>;

    public:
        using Base::Base;
        constexpr StrongIdCastable() = default;
        constexpr StrongIdCastable(Underlying v) : Base(v) {}
        constexpr StrongIdCastable(const Base& b) : Base(b) {}
        constexpr operator Underlying() const { return this->get(); }

        constexpr StrongIdCastable& operator++() {
            *this = StrongIdCastable{ static_cast<Underlying>(this->get() + 1) };
            return *this;
        }
        constexpr StrongIdCastable operator++(int) {
            StrongIdCastable tmp = *this;
            ++(*this);
            return tmp;
        }
};

} // namespace swm

namespace std {
template <typename Tag, typename U>
struct hash<swm::StrongId<Tag, U>> {
    size_t operator()(swm::StrongId<Tag, U> id) const noexcept {
        return std::hash<U>{}(id.get());
    }
};
template <typename Tag, typename U>
struct hash<swm::StrongIdCastable<Tag, U>> {
    size_t operator()(swm::StrongIdCastable<Tag, U> id) const noexcept {
        return std::hash<U>{}(id.get());
    }
};
} // namespace std

