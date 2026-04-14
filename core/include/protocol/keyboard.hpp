#pragma once

#include <cstddef>
#include <cstring>
#include <string_view>

#include <support/message.hpp>

// ---------------------------------------------------------------------------
// Keyboard protocol.
//
// Contract between a keyboard-observing backend and any module that wants
// to react to keyboard layout changes (bar indicators, lua event hooks).
// The core itself is ignorant of XKB or input semantics.
//
// The layout name is a short XKB-style token ("us", "ru", "de", "fr(bepo)")
// carried inline as a fixed-size null-terminated char array so the payload
// stays trivially copyable and SBO-friendly.
// ---------------------------------------------------------------------------

namespace protocol::keyboard {

inline constexpr std::size_t kLayoutNameMax = 24;

struct LayoutChanged {
    static constexpr uint32_t kTag = fnv1a("keyboard:layout_changed");
    char                      name[kLayoutNameMax];

    static LayoutChanged from(std::string_view sv) {
        LayoutChanged     out{};
        const std::size_t n = sv.size() < kLayoutNameMax - 1
                              ? sv.size()
                              : kLayoutNameMax - 1;
        std::memcpy(out.name, sv.data(), n);
        out.name[n] = '\0';
        return out;
    }
};
static_assert(Message<LayoutChanged>);

} // namespace protocol::keyboard
