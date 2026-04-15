#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

// ---------------------------------------------------------------------------
// Vec<T, N> — fixed-size arithmetic vector.
//
// Named accessors x(), y(), z(), w() are available when N is large enough.
// All accessors are constexpr inline — zero overhead.
// ---------------------------------------------------------------------------

template <typename T, std::size_t N>
struct Vec {
    T data[N] = {};

    constexpr Vec() = default;

    // Converting constructor from Vec with a different element type.
    template <typename U, typename = std::enable_if_t<!std::is_same<T, U>::value>>
    constexpr Vec(const Vec<U, N>& other) { for (std::size_t i = 0; i < N; ++i) data[i] = static_cast<T>(other.data[i]); }

    // Per-component constructors (SFINAE-guarded).
    template <std::size_t M = N, typename = std::enable_if_t<M == 2>>
    constexpr Vec(T a, T b) : data{ a, b } {}

    template <std::size_t M = N, typename = std::enable_if_t<M == 3>>
    constexpr Vec(T a, T b, T c) : data{ a, b, c } {}

    template <std::size_t M = N, typename = std::enable_if_t<M == 4>>
    constexpr Vec(T a, T b, T c, T d) : data{ a, b, c, d } {}

    // Named accessors.
    template <std::size_t M = N> constexpr std::enable_if_t<(M >= 1), T&>       x()       { return data[0]; }
    template <std::size_t M = N> constexpr std::enable_if_t<(M >= 1), const T&> x() const { return data[0]; }
    template <std::size_t M = N> constexpr std::enable_if_t<(M >= 2), T&>       y()       { return data[1]; }
    template <std::size_t M = N> constexpr std::enable_if_t<(M >= 2), const T&> y() const { return data[1]; }
    template <std::size_t M = N> constexpr std::enable_if_t<(M >= 3), T&>       z()       { return data[2]; }
    template <std::size_t M = N> constexpr std::enable_if_t<(M >= 3), const T&> z() const { return data[2]; }
    template <std::size_t M = N> constexpr std::enable_if_t<(M >= 4), T&>       w()       { return data[3]; }
    template <std::size_t M = N> constexpr std::enable_if_t<(M >= 4), const T&> w() const { return data[3]; }

    constexpr T&       operator[](std::size_t i)       { return data[i]; }
    constexpr const T& operator[](std::size_t i) const { return data[i]; }

    // Arithmetic — element-wise, mixed-type.
    template <typename U> constexpr auto operator+(Vec<U,N> rhs) const { using R = std::common_type_t<T,U>; Vec<R,N> r; for (std::size_t i = 0; i < N; ++i) r.data[i] = static_cast<R>(data[i] + rhs.data[i]); return r; }
    template <typename U> constexpr auto operator-(Vec<U,N> rhs) const { using R = std::common_type_t<T,U>; Vec<R,N> r; for (std::size_t i = 0; i < N; ++i) r.data[i] = static_cast<R>(data[i] - rhs.data[i]); return r; }
    constexpr Vec operator*(T s) const { Vec r; for (std::size_t i = 0; i < N; ++i) r.data[i] = static_cast<T>(data[i] * s); return r; }
    constexpr Vec operator/(T s) const { Vec r; for (std::size_t i = 0; i < N; ++i) r.data[i] = static_cast<T>(data[i] / s); return r; }

    constexpr Vec& operator+=(Vec rhs) { for (std::size_t i = 0; i < N; ++i) data[i] += rhs.data[i]; return *this; }
    constexpr Vec& operator-=(Vec rhs) { for (std::size_t i = 0; i < N; ++i) data[i] -= rhs.data[i]; return *this; }
    constexpr Vec& operator*=(T s)     { for (std::size_t i = 0; i < N; ++i) data[i] *= s; return *this; }
    constexpr Vec& operator/=(T s)     { for (std::size_t i = 0; i < N; ++i) data[i] /= s; return *this; }

    constexpr bool operator==(Vec rhs) const { for (std::size_t i = 0; i < N; ++i) if (data[i] != rhs.data[i]) return false; return true; }
    constexpr bool operator!=(Vec rhs) const { return !(*this == rhs); }
};

// ---------------------------------------------------------------------------
// Convenience aliases
// ---------------------------------------------------------------------------

template <typename T> using Vec2 = Vec<T, 2>;
template <typename T> using Vec3 = Vec<T, 3>;
template <typename T> using Vec4 = Vec<T, 4>;

using Vec2i   = Vec2<int>;
using Vec2i16 = Vec2<int16_t>;
using Vec2i32 = Vec2<int32_t>;
using Vec2u   = Vec2<uint32_t>;
using Vec2f   = Vec2<float>;

using Vec3i = Vec3<int>;
using Vec3f = Vec3<float>;

using Vec4i = Vec4<int>;
using Vec4f = Vec4<float>;
