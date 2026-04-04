#pragma once

#include <string>
#include <vector>

namespace backend {

// Read-only access to the X keyboard layout state.
// The implementation is backend-specific (X11: Xlib XKB).
class KeyboardPort {
    public:
        virtual ~KeyboardPort() = default;

        // Returns the name of the currently active layout group (e.g. "us", "ru").
        virtual std::string current_layout() const = 0;

        // Returns all configured layout names in group order.
        virtual std::vector<std::string> layout_names() const = 0;

        // Applies the given layouts and options to the X server.
        // Saves the previous state so it can be restored later.
        virtual void apply(const std::vector<std::string>& layouts,
            const std::string& options) = 0;

        // Restores the layout state that was active before the first apply() call.
        virtual void restore() = 0;
};

} // namespace backend