#pragma once

#include <xcb/xcb.h>
#include <xcb/atom.hpp>

#include <array>
#include <cstddef>
#include <type_traits>

namespace xcb {

// Compile-time mapping (enum value -> atom name -> interned xcb_atom_t).
//
// The enum must be a scoped enum with contiguous values starting at 0 and a
// trailing _Count sentinel; the name table is supplied as a static array
// parallel to the enum. See ewmh_atoms.hpp for an X-macro-driven example.
//
// The enum->atom lookup is a direct array index, so there is no hashing cost
// at the call site; resolve() does one batched round-trip.
template <typename EnumT, std::size_t N>
class AtomRegistry {
    static_assert(std::is_enum_v<EnumT>, "AtomRegistry EnumT must be an enum type");

    std::array<const char*, N> names_;
    std::array<xcb_atom_t, N>  atoms_{};
    bool resolved_ = false;

    public:
        constexpr explicit AtomRegistry(const std::array<const char*, N>& names) : names_(names) {}

        // Send all intern_atom requests as a single batch and cache results.
        // Idempotent: calling resolve() twice is a no-op after the first.
        void resolve(xcb_connection_t* conn) {
            if (resolved_)
                return;
            auto m = intern_batch(conn, names_.data(), N);
            for (std::size_t i = 0; i < N; ++i) {
                auto it = m.find(names_[i]);
                atoms_[i] = (it != m.end()) ? it->second : (xcb_atom_t)XCB_ATOM_NONE;
            }
            resolved_ = true;
        }

        bool resolved() const { return resolved_; }

        xcb_atom_t operator[](EnumT e) const {
            return atoms_[static_cast<std::size_t>(e)];
        }

        const char* name_of(EnumT e) const {
            return names_[static_cast<std::size_t>(e)];
        }
};

} // namespace xcb
