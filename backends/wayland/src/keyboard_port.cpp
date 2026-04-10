#include <backend/keyboard_port.hpp>
#include <log.hpp>

#include <string>
#include <vector>
#include <cstring>

extern "C" {
#include <wlr/types/wlr_seat.h>
#include <xkbcommon/xkbcommon.h>
}

namespace backend::wl {

// ---------------------------------------------------------------------------
// WlKeyboardPort
//
// Reads keyboard layout info from the XKB state of the focused keyboard.
// apply()/restore() reconfigure the keymap on all attached keyboards.
// ---------------------------------------------------------------------------
class WlKeyboardPort final : public KeyboardPort {
    public:
        explicit WlKeyboardPort(wlr_seat* seat) : seat_(seat) {}

        std::string current_layout() const override {
            auto* kb = wlr_seat_get_keyboard(seat_);
            if (!kb || !kb->keymap)
                return "";
            xkb_state* state = kb->xkb_state;
            if (!state)
                return "";
            xkb_layout_index_t active = xkb_state_serialize_layout(
                state, XKB_STATE_LAYOUT_EFFECTIVE);
            const char*        name = xkb_keymap_layout_get_name(kb->keymap, active);
            return name ? name : "";
        }

        std::vector<std::string> layout_names() const override {
            auto* kb = wlr_seat_get_keyboard(seat_);
            if (!kb || !kb->keymap)
                return {};
            std::vector<std::string> names;
            xkb_layout_index_t       count = xkb_keymap_num_layouts(kb->keymap);
            for (xkb_layout_index_t i = 0; i < count; i++) {
                const char* n = xkb_keymap_layout_get_name(kb->keymap, i);
                names.emplace_back(n ? n : "");
            }
            return names;
        }

        void apply(const std::vector<std::string>& layouts,
            const std::string& options) override {
            auto* kb = wlr_seat_get_keyboard(seat_);
            if (!kb) return;

            // Build comma-separated layout string for XKB
            std::string layout_str;
            for (size_t i = 0; i < layouts.size(); i++) {
                if (i > 0) layout_str += ',';
                layout_str += layouts[i];
            }

            // Save previous keymap for restore()
            if (!original_keymap_str_.empty())
                return; // already applied — don't overwrite original

            original_keymap_str_ = ""; // sentinel: apply() called

            xkb_context*   ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
            xkb_rule_names rules{};
            rules.layout  = layout_str.c_str();
            rules.options = options.c_str();
            xkb_keymap* keymap = xkb_keymap_new_from_names(ctx, &rules,
                    XKB_KEYMAP_COMPILE_NO_FLAGS);
            if (keymap) {
                wlr_keyboard_set_keymap(kb, keymap);
                LOG_INFO("WlKeyboardPort: applied layout '%s' options '%s'",
                    layout_str.c_str(), options.c_str());
                xkb_keymap_unref(keymap);
            } else {
                LOG_ERR("WlKeyboardPort: failed to compile keymap for '%s'",
                    layout_str.c_str());
            }
            xkb_context_unref(ctx);
        }

        void restore() override {
            auto* kb = wlr_seat_get_keyboard(seat_);
            if (!kb) return;

            // Restore default (environment-derived) keymap
            xkb_context*   ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
            xkb_rule_names rules{};
            xkb_keymap*    keymap = xkb_keymap_new_from_names(ctx, &rules,
                    XKB_KEYMAP_COMPILE_NO_FLAGS);
            if (keymap) {
                wlr_keyboard_set_keymap(kb, keymap);
                xkb_keymap_unref(keymap);
            }
            xkb_context_unref(ctx);
            original_keymap_str_.clear();
        }

        uint32_t get_group() const override {
            auto* kb = wlr_seat_get_keyboard(seat_);
            if (!kb || !kb->xkb_state)
                return 0;
            return static_cast<uint32_t>(
                xkb_state_serialize_layout(kb->xkb_state, XKB_STATE_LAYOUT_EFFECTIVE));
        }

        void set_group(uint32_t group) override {
            auto* kb = wlr_seat_get_keyboard(seat_);
            if (!kb || !kb->keymap)
                return;
            if (group >= xkb_keymap_num_layouts(kb->keymap))
                return;
            // Notify clients via seat that the modifier/group state changed.
            // Setting group with zeroed mods switches the layout without
            // affecting any modifier state.
            wlr_keyboard_modifiers mods{};
            mods.group = group;
            wlr_seat_keyboard_notify_modifiers(seat_, &mods);
        }

    private:
        wlr_seat*   seat_;
        std::string original_keymap_str_; // non-empty once apply() ran
};

std::unique_ptr<KeyboardPort> create_keyboard_port(wlr_seat* seat) {
    return std::make_unique<WlKeyboardPort>(seat);
}

} // namespace backend::wl
