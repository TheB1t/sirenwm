#include <backend/input_port.hpp>
#include <log.hpp>

#include <string>

extern "C" {
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_cursor.h>
#include <xkbcommon/xkbcommon.h>
}

// ---------------------------------------------------------------------------
// keysym_from_name — convert name to keysym (xkbcommon, backend-provided)
// ---------------------------------------------------------------------------
namespace backend {
uint32_t keysym_from_name(const std::string& name) {
    return (uint32_t)xkb_keysym_from_name(name.c_str(), XKB_KEYSYM_CASE_INSENSITIVE);
}
} // namespace backend

namespace backend::wl {

// ---------------------------------------------------------------------------
// WlInputPort
//
// On Wayland, key/button grabs work differently from X11:
//   - There is no concept of "passive key grab" for the compositor itself —
//     the compositor receives ALL input from the kernel (via libinput).
//   - grab_key/ungrab_key maintain a set of grabbed keysyms that the compositor
//     intercepts in handle_keyboard_key() before forwarding to clients.
//   - grab_button/ungrab_button similarly maintain a grab set.
// ---------------------------------------------------------------------------
class WlInputPort final : public InputPort {
public:
    WlInputPort(wlr_seat* seat, wlr_cursor* cursor, bool& pointer_grabbed)
        : seat_(seat), cursor_(cursor), pointer_grabbed_(pointer_grabbed) {}

    // Key grabs — stored locally; checked in WaylandBackend::handle_keyboard_key
    void grab_key(uint32_t /*keysym*/, uint16_t /*mods*/) override {
        // No-op: Wayland compositor intercepts all keys anyway.
        // The keybindings module will match against emitted KeyPressEv events.
    }

    void ungrab_all_keys() override {
        // No-op.
    }

    // Button grabs
    void grab_button(WindowId /*window*/, uint8_t /*button*/, uint16_t /*mods*/) override {
        // No-op: Wayland compositor gets all button events first.
    }

    void ungrab_all_buttons(WindowId /*window*/) override {}
    void grab_button_any(WindowId /*window*/) override {}

    // Pointer grab — suppress pointer focus forwarding to clients during drag.
    void grab_pointer() override   { pointer_grabbed_ = true;  }
    void ungrab_pointer() override { pointer_grabbed_ = false; }

    void allow_events(bool /*replay*/) override {
        // X11 replay semantic has no direct Wayland equivalent.
    }

    void warp_pointer(WindowId /*window*/, Vec2i16 pos) override {
        wlr_cursor_warp_absolute(cursor_, nullptr,
            (double)pos.x(), (double)pos.y());
    }

    void warp_pointer_abs(Vec2i16 pos) override {
        wlr_cursor_warp_absolute(cursor_, nullptr,
            (double)pos.x(), (double)pos.y());
    }

    void flush() override {
        // No-op.
    }

private:
    wlr_seat*   seat_;
    wlr_cursor* cursor_;
    bool&       pointer_grabbed_;
};

std::unique_ptr<InputPort> create_input_port(wlr_seat* seat, wlr_cursor* cursor, bool& pointer_grabbed) {
    return std::make_unique<WlInputPort>(seat, cursor, pointer_grabbed);
}

} // namespace backend::wl
