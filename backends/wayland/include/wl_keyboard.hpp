#pragma once

#include <wl_listener.hpp>

#include <functional>

extern "C" {
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <xkbcommon/xkbcommon.h>
}

// ---------------------------------------------------------------------------
// WlKeyboard — owns per-keyboard device state and signal listeners.
//
// Configures XKB keymap from environment, sets repeat info, and wires
// key/modifiers/destroy signals on construction.
// ---------------------------------------------------------------------------
class WlKeyboard {
    public:
        using KeyCb     = std::function<void (WlKeyboard*, wlr_keyboard_key_event*)>;
        using ModsCb    = std::function<void (WlKeyboard*)>;
        using DestroyCb = std::function<void (WlKeyboard*)>;

        WlKeyboard(wlr_input_device* device, KeyCb on_key, ModsCb on_mods, DestroyCb on_destroy);
        ~WlKeyboard() = default;

        // Non-copyable, non-movable (listeners hold raw this pointer).
        WlKeyboard(const WlKeyboard&)            = delete;
        WlKeyboard& operator=(const WlKeyboard&) = delete;
        WlKeyboard(WlKeyboard&&)                 = delete;
        WlKeyboard& operator=(WlKeyboard&&)      = delete;

        wlr_input_device* device()   const noexcept { return device_; }
        wlr_keyboard*     keyboard() const noexcept { return keyboard_; }

    private:
        wlr_input_device* device_   = nullptr;
        wlr_keyboard*     keyboard_ = nullptr;

        WlListener<wlr_keyboard_key_event> on_key_;
        WlVoidListener on_modifiers_;
        WlVoidListener on_destroy_;
};
