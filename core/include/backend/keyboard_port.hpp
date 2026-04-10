#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace backend {

// Keyboard layout port — layout enumeration, application, and group switching.
// The implementation is backend-specific (X11: Xlib XKB, Wayland: xkbcommon).
class KeyboardPort {
    public:
        virtual ~KeyboardPort() = default;

        // Returns the name of the currently active layout group (e.g. "us", "ru").
        virtual std::string current_layout() const = 0;

        // Returns all configured layout names in group order.
        virtual std::vector<std::string> layout_names() const = 0;

        // Applies the given layouts and options to the keyboard.
        // Saves the previous state so it can be restored later.
        virtual void apply(const std::vector<std::string>& layouts,
            const std::string& options) = 0;

        // Restores the layout state that was active before the first apply() call.
        virtual void restore() = 0;

        // Returns the index of the currently active layout group (0-based).
        virtual uint32_t get_group() const = 0;

        // Switches the active layout group to the given index.
        // No-op if index is out of range.
        virtual void set_group(uint32_t group) = 0;
};

} // namespace backend
