#pragma once

// ---------------------------------------------------------------------------
// Backend-agnostic input grab/warp capability interface.
// Feature modules (keybindings) consume this instead of xcb headers.
// X11Backend provides the concrete implementation.
// ---------------------------------------------------------------------------

#include <backend/events.hpp>
#include <cstdint>

namespace backend {

class InputPort {
    public:
        virtual ~InputPort() = default;

        // Implementation handles keysym->keycode resolution and numlock/capslock variants.
        virtual void grab_key(uint32_t keysym, uint16_t mods) = 0;
        virtual void ungrab_all_keys() = 0;

        // Implementation handles numlock/capslock variants.
        virtual void grab_button(WindowId window, uint8_t button, uint16_t mods) = 0;
        virtual void ungrab_all_buttons(WindowId window) = 0;

        virtual void grab_pointer()   = 0;
        virtual void ungrab_pointer() = 0;
        virtual void warp_pointer(WindowId window, int16_t x, int16_t y) = 0;
        // Warp to absolute root-screen coordinates.
        virtual void warp_pointer_abs(int16_t x, int16_t y)              = 0;
        virtual void focus_window(WindowId window) = 0;
        virtual void flush() = 0;
};

} // namespace backend