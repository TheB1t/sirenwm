#pragma once

// ---------------------------------------------------------------------------
// Backend-agnostic input grab/warp capability interface.
// Feature modules (keybindings) consume this instead of xcb headers.
// X11Backend provides the concrete implementation.
// ---------------------------------------------------------------------------

#include <backend/events.hpp>
#include <cstdint>
#include <string>
#include <support/vec.hpp>

namespace backend {

// ---------------------------------------------------------------------------
// Modifier bitmask constants — X11 protocol values, kept here so domain
// modules don't embed platform magic numbers directly.
// ---------------------------------------------------------------------------
inline constexpr uint16_t MOD_SHIFT   = 1;
inline constexpr uint16_t MOD_LOCK    = 2;   // CapsLock
inline constexpr uint16_t MOD_CONTROL = 4;
inline constexpr uint16_t MOD_1       = 8;   // Alt
inline constexpr uint16_t MOD_2       = 16;  // Numlock
inline constexpr uint16_t MOD_3       = 32;
inline constexpr uint16_t MOD_4       = 64;  // Super
inline constexpr uint16_t MOD_5       = 128;

// Bits to strip from event state before matching (numlock + capslock).
inline constexpr uint16_t MOD_STRIP_MASK = MOD_2 | MOD_LOCK;

// Convert a modifier name ("mod4", "shift", "alt", …) to its bitmask.
// Returns 0 for unknown names.
inline uint16_t parse_modifier_name(const std::string& name) {
    // clang-format off
    if (name == "shift"   || name == "Shift")   return MOD_SHIFT;
    if (name == "ctrl"    || name == "control"
        || name == "Control")  return MOD_CONTROL;
    if (name == "alt"     || name == "mod1"
        || name == "Alt")      return MOD_1;
    if (name == "mod2")                          return MOD_2;
    if (name == "mod3")                          return MOD_3;
    if (name == "mod4"    || name == "super"
        || name == "win"
        || name == "Super")    return MOD_4;
    if (name == "mod5")                          return MOD_5;
    // clang-format on
    return 0;
}

class InputPort {
    public:
        virtual ~InputPort() = default;

        // Implementation handles keysym->keycode resolution and numlock/capslock variants.
        virtual void grab_key(uint32_t keysym, uint16_t mods) = 0;
        virtual void ungrab_all_keys()                        = 0;

        // Implementation handles numlock/capslock variants.
        virtual void grab_button(WindowId window, uint8_t button, uint16_t mods) = 0;
        virtual void ungrab_all_buttons(WindowId window)                         = 0;

        virtual void grab_pointer()                             = 0;
        virtual void ungrab_pointer()                           = 0;
        virtual void warp_pointer(WindowId window, Vec2i16 pos) = 0;
        // Warp to absolute root-screen coordinates.
        virtual void warp_pointer_abs(Vec2i16 pos) = 0;
        virtual void flush()                       = 0;
};

// Convert a keysym name (e.g. "Return", "a", "F1") to its keysym value.
// Returns 0 if the name is not recognized.
// Implemented in the backend — calls xkb_keysym_from_name internally.
uint32_t keysym_from_name(const std::string& name);

} // namespace backend
