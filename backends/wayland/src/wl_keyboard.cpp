#include <wl_keyboard.hpp>
#include <log.hpp>

WlKeyboard::WlKeyboard(wlr_input_device* device, KeyCb on_key, ModsCb on_mods, DestroyCb on_destroy)
    : device_(device)
      , keyboard_(wlr_keyboard_from_input_device(device)) {
    // Configure XKB keymap from environment (XKBLAYOUT, XKBOPTIONS, etc.)
    xkb_context*   ctx    = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    xkb_rule_names rules  = {};
    xkb_keymap*    keymap = xkb_keymap_new_from_names(ctx, &rules, XKB_KEYMAP_COMPILE_NO_FLAGS);
    wlr_keyboard_set_keymap(keyboard_, keymap);
    xkb_keymap_unref(keymap);
    xkb_context_unref(ctx);

    wlr_keyboard_set_repeat_info(keyboard_, 25, 600);

    on_key_.connect(&keyboard_->events.key,
        [this, cb = std::move(on_key)](wlr_keyboard_key_event* ev) {
            cb(this, ev);
        });
    on_modifiers_.connect(&keyboard_->events.modifiers,
        [this, cb = std::move(on_mods)](void*) {
            cb(this);
        });
    on_destroy_.connect(&device->events.destroy,
        [this, cb = std::move(on_destroy)](void*) {
            cb(this);
        });
}
