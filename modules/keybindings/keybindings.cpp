#include "keybindings.hpp"
#include <backend/backend.hpp>
#include <backend/input_port.hpp>
#include <core.hpp>
#include <config.hpp>
#include <runtime.hpp>
#include <module_registry.hpp>
#include <log.hpp>

#include <algorithm>
#include <string>
#include <sstream>
#include <unordered_map>
#include <type_traits>
#include <cstdlib>
#include <optional>
#include <cstdint>
#include <vector>

#include <xkbcommon/xkbcommon.h>

// X11 modifier constants used for physical_mod_map and event stripping.
// These are X11 protocol constants, not backend-specific types.
static constexpr uint16_t MOD_MASK_SHIFT   = 1;
static constexpr uint16_t MOD_MASK_CONTROL = 4;
static constexpr uint16_t MOD_MASK_1       = 8;   // Alt
static constexpr uint16_t MOD_MASK_2       = 16;  // Numlock
static constexpr uint16_t MOD_MASK_3       = 32;
static constexpr uint16_t MOD_MASK_4       = 64;  // Super
static constexpr uint16_t MOD_MASK_5       = 128;
static constexpr uint16_t MOD_MASK_LOCK    = 2;   // CapsLock

// Mask to strip numlock and capslock from event modifier state.
static constexpr uint16_t STRIP_MODS_MASK = MOD_MASK_2 | MOD_MASK_LOCK;

static const std::unordered_map<std::string, uint16_t> physical_mod_map = {
    { "shift", MOD_MASK_SHIFT   },
    { "ctrl",  MOD_MASK_CONTROL },
    { "control", MOD_MASK_CONTROL },
    { "alt",   MOD_MASK_1       },
    { "mod1",  MOD_MASK_1       },
    { "mod2",  MOD_MASK_2       },
    { "mod3",  MOD_MASK_3       },
    { "mod4",  MOD_MASK_4       },
    { "mod5",  MOD_MASK_5       },
    { "super", MOD_MASK_4       },
    { "win",   MOD_MASK_4       },
};

static std::string lower_ascii(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z')
            c = (char)(c - 'A' + 'a');
    return s;
}

static uint16_t parse_physical_modifier(const std::string& name) {
    auto it = physical_mod_map.find(lower_ascii(name));
    return (it != physical_mod_map.end()) ? it->second : 0;
}

static std::optional<std::string> validate_modifier_value(const RuntimeValue& value) {
    const auto* s = value.as_string();
    if (!s)
        return std::string("must be a string");
    if (lower_ascii(*s) == "mod")
        return std::string("'mod' is abstract; use a real modifier (e.g. mod4, alt, ctrl, shift)");
    if (!parse_physical_modifier(*s))
        return std::string("unknown modifier '") + *s + "'";
    return std::nullopt;
}

static bool parse_binding_modifier_token(const std::string& token,
    const Config& config,
    uint16_t& out,
    std::string* err_out = nullptr) {
    auto t = lower_ascii(token);
    if (t == "mod") {
        if (!config.has_mod_mask()) {
            if (err_out)
                *err_out = "primary modifier is not set (set siren.modifier first)";
            return false;
        }
        out = config.get_mod_mask();
        return true;
    }

    uint16_t m = parse_physical_modifier(t);
    if (!m) {
        if (err_out)
            *err_out = "unknown modifier token '" + token + "'";
        return false;
    }
    out = m;
    return true;
}

// "Mod+Shift+Return" -> mods + keysym
static bool parse_key(const std::string& spec,
    const Config& config,
    uint16_t& mods_out,
    uint32_t& keysym_out,
    std::string* err_out = nullptr) {
    std::istringstream ss(spec);
    std::string        token;
    mods_out   = 0;
    keysym_out = 0;

    while (std::getline(ss, token, '+')) {
        auto        token_lower = lower_ascii(token);
        uint16_t    mod         = 0;
        std::string mod_err;
        if (parse_binding_modifier_token(token, config, mod, &mod_err)) {
            mods_out |= mod;
            continue;
        }
        if (token_lower == "mod") {
            if (err_out)
                *err_out = mod_err.empty() ? "primary modifier is not set" : mod_err;
            return false;
        }

        keysym_out = xkb_keysym_from_name(token.c_str(), XKB_KEYSYM_NO_FLAGS);
        if (keysym_out == XKB_KEY_NoSymbol)
            keysym_out = xkb_keysym_from_name(token_lower.c_str(), XKB_KEYSYM_NO_FLAGS);
        if (keysym_out == XKB_KEY_NoSymbol) {
            if (err_out)
                *err_out = "unknown keysym '" + token + "'";
            return false;
        }
    }

    if (keysym_out == 0 && err_out)
        *err_out = "missing keysym in '" + spec + "'";
    return keysym_out != 0;
}

static bool register_lua_keybinding(Core& core,
    Config& config,
    LuaContext& lua,
    const std::string& spec,
    int fn_index,
    const char* origin,
    std::string* err_out = nullptr) {
    fn_index = lua.abs_index(fn_index);
    if (!lua.is_function(fn_index)) {
        if (err_out)
            *err_out = std::string(origin) + ": callback must be a function";
        return false;
    }

    uint16_t    mods   = 0;
    uint32_t    keysym = 0;
    std::string key_err;
    if (!parse_key(spec, config, mods, keysym, &key_err)) {
        if (err_out) {
            if (key_err.empty())
                *err_out = std::string(origin) + ": invalid key spec";
            else
                *err_out = std::string(origin) + ": " + key_err;
        }
        return false;
    }

    auto callback = config.lua().ref_function(fn_index);
    if (!callback.valid()) {
        if (err_out)
            *err_out = std::string(origin) + ": callback must be a function";
        return false;
    }

    core.register_keybinding(mods, keysym, [&config, callback]() {
            config.lua().call_ref(callback, 0, 0, "key callback error");
        });
    return true;
}

static bool load_bind_list(Core& core,
    Config& config,
    LuaContext& lua,
    int table_idx,
    const char* origin,
    std::string& err_out) {
    bool        ok = true;
    std::string first_err;

    auto        mark_error = [&](const std::string& msg) {
            if (first_err.empty())
                first_err = msg;
            LOG_ERR("%s", msg.c_str());
            ok = false;
        };

    table_idx = lua.abs_index(table_idx);
    if (!lua.is_table(table_idx)) {
        err_out = std::string(origin) + ": expected table";
        return false;
    }

    int n = lua.raw_len(table_idx);
    for (int i = 1; i <= n; i++) {
        lua.raw_geti(table_idx, i);
        int entry_idx = lua.abs_index(-1);

        if (!lua.is_table(entry_idx)) {
            mark_error(std::string(origin) + "[" + std::to_string(i) + "]: entry must be a table");
            lua.pop();
            continue;
        }

        std::string spec;
        lua.raw_geti(entry_idx, 1);
        if (lua.is_string(-1))
            spec = lua.to_string(-1);
        lua.pop();

        if (spec.empty()) {
            const char* keys[] = { "key", "bind", "spec" };
            for (const char* k : keys) {
                lua.get_field(entry_idx, k);
                if (lua.is_string(-1)) {
                    spec = lua.to_string(-1);
                    lua.pop();
                    break;
                }
                lua.pop();
            }
        }

        int fn_idx = 0;
        lua.raw_geti(entry_idx, 2);
        if (lua.is_function(-1))
            fn_idx = lua.abs_index(-1);
        else
            lua.pop();

        if (fn_idx == 0) {
            const char* cb_keys[] = { "action", "fn", "callback" };
            for (const char* k : cb_keys) {
                lua.get_field(entry_idx, k);
                if (lua.is_function(-1)) {
                    fn_idx = lua.abs_index(-1);
                    break;
                }
                lua.pop();
            }
        }

        std::string label = std::string(origin) + "[" + std::to_string(i) + "]";
        if (spec.empty() || fn_idx == 0) {
            mark_error(label + ": expected { \"mod+Key\", fn } or { key=..., action=... }");
        } else {
            std::string bind_err;
            if (!register_lua_keybinding(core, config, lua, spec, fn_idx, label.c_str(), &bind_err))
                mark_error(bind_err.empty() ? (label + ": invalid binding") : bind_err);
            lua.pop();
        }

        lua.pop();
    }

    if (!ok)
        err_out = first_err.empty() ? std::string(origin) + ": invalid bindings" : first_err;
    return ok;
}

// "Mod+Button1" -> mods + button
static bool parse_mouse_spec(const std::string& spec,
    const Config& config,
    uint16_t& mods_out,
    uint32_t& btn_out,
    std::string* err_out = nullptr) {
    std::istringstream ss(spec);
    std::string        token;
    mods_out = 0;
    btn_out  = 0;
    while (std::getline(ss, token, '+')) {
        auto token_lower = lower_ascii(token);
        if (token_lower.size() > 6 && token_lower.substr(0, 6) == "button") {
            btn_out = (uint32_t)std::stoul(token_lower.substr(6));
            continue;
        }
        uint16_t    mod = 0;
        std::string mod_err;
        if (!parse_binding_modifier_token(token, config, mod, &mod_err)) {
            if (err_out)
                *err_out = mod_err.empty() ? ("unknown token '" + token + "'") : mod_err;
            return false;
        }
        mods_out |= mod;
    }
    if (btn_out == 0 && err_out)
        *err_out = "missing button in '" + spec + "'";
    return btn_out != 0;
}

static bool load_mouse_bind_list(Config& config,
    LuaContext& lua,
    int table_idx,
    const char* origin,
    std::string& err_out) {
    bool        ok = true;
    std::string first_err;

    auto        mark_error = [&](const std::string& msg) {
            if (first_err.empty())
                first_err = msg;
            LOG_ERR("%s", msg.c_str());
            ok = false;
        };

    table_idx = lua.abs_index(table_idx);
    if (!lua.is_table(table_idx)) {
        err_out = std::string(origin) + ": expected table";
        return false;
    }

    int n = lua.raw_len(table_idx);
    for (int i = 1; i <= n; i++) {
        lua.raw_geti(table_idx, i);
        int entry_idx = lua.abs_index(-1);
        if (!lua.is_table(entry_idx)) {
            mark_error(std::string(origin) + "[" + std::to_string(i) + "]: entry must be a table");
            lua.pop();
            continue;
        }

        std::string spec;
        lua.raw_geti(entry_idx, 1);
        if (lua.is_string(-1))
            spec = lua.to_string(-1);
        lua.pop();
        if (spec.empty()) {
            const char* keys[] = { "spec", "bind", "button" };
            for (const char* k : keys) {
                lua.get_field(entry_idx, k);
                if (lua.is_string(-1)) {
                    spec = lua.to_string(-1);
                    lua.pop();
                    break;
                }
                lua.pop();
            }
        }

        uint16_t    mods = 0;
        uint32_t    btn  = 0;
        std::string spec_err;
        if (!parse_mouse_spec(spec, config, mods, btn, &spec_err)) {
            mark_error(std::string(origin) + "[" + std::to_string(i) + "]: " + spec_err);
            lua.pop();
            continue;
        }

        Config::MouseBinding mb;
        mb.mods   = mods;
        mb.button = btn;

        int handler_idx = 0;
        lua.raw_geti(entry_idx, 2);
        if (lua.is_function(-1) || lua.is_table(-1) || lua.is_string(-1))
            handler_idx = lua.abs_index(-1);
        else
            lua.pop();

        if (handler_idx == 0) {
            const char* cb_keys[] = { "action", "fn", "callback" };
            for (const char* k : cb_keys) {
                lua.get_field(entry_idx, k);
                if (lua.is_function(-1) || lua.is_table(-1) || lua.is_string(-1)) {
                    handler_idx = lua.abs_index(-1);
                    break;
                }
                lua.pop();
            }
        }

        // Check for builtin string action: "move", "resize", "float"
        if (handler_idx != 0 && lua.is_string(handler_idx)) {
            std::string action = lua.to_string(handler_idx);
            lua.pop();
            if (action == "move")        mb.builtin = Config::MouseAction::Move;
            else if (action == "resize") mb.builtin = Config::MouseAction::Resize;
            else if (action == "float")  mb.builtin = Config::MouseAction::Float;
            else {
                mark_error(std::string(origin) + "[" + std::to_string(i) + "]: unknown builtin action '" + action +
                    "' (use \"move\", \"resize\", \"float\")");
                lua.pop();
                continue;
            }
        } else if (handler_idx != 0 && lua.is_function(handler_idx)) {
            mb.press_callback = config.lua().ref_function(handler_idx);
            lua.pop();
        } else {
            int callback_table_idx = handler_idx ? handler_idx : entry_idx;
            if (handler_idx != 0 && !lua.is_table(handler_idx)) {
                mark_error(std::string(origin) + "[" + std::to_string(i) +
                    "]: callback must be function, string action, or table");
                lua.pop();
                lua.pop();
                continue;
            }

            lua.get_field(callback_table_idx, "press");
            if (lua.is_function(-1))
                mb.press_callback = config.lua().ref_function(-1);
            lua.pop();

            lua.get_field(callback_table_idx, "release");
            if (lua.is_function(-1))
                mb.release_callback = config.lua().ref_function(-1);
            lua.pop();

            if (handler_idx != 0)
                lua.pop();
        }

        if (mb.builtin == Config::MouseAction::None &&
            !mb.press_callback.valid() && !mb.release_callback.valid()) {
            mark_error(std::string(origin) + "[" + std::to_string(i) + "]: missing callback");
            lua.pop();
            continue;
        }

        config.add_mouse_binding(std::move(mb));
        lua.pop();
    }

    if (!ok)
        err_out = first_err.empty() ? std::string(origin) + ": invalid mouse binds" : first_err;
    return ok;
}

static void install_mouse_grabs_for_window(backend::InputPort& input,
    WindowId win,
    const Config& config) {
    std::vector<std::pair<uint8_t, uint16_t> > grabs;
    auto push_unique = [&](uint32_t button, uint16_t mods) {
            uint8_t b = (uint8_t)button;
            for (auto& g : grabs)
                if (g.first == b && g.second == mods) return;
            grabs.push_back({ b, mods });
        };

    for (auto& mb : config.get_mouse_bindings())
        push_unique(mb.button, mb.mods);

    for (auto& g : grabs)
        input.grab_button(win, g.first, g.second);
}

static void reinstall_mouse_grabs_for_all_windows(backend::InputPort& input,
    Core& core,
    const Config& config) {
    for (auto win : core.all_window_ids()) {
        input.ungrab_all_buttons(win);
        install_mouse_grabs_for_window(input, win, config);
    }
    input.flush();
}

void KeybindingsModule::on_init(Core& core) {
    config().register_lua_assignment_handler("binds",
        [this, &core](LuaContext& lua, int table_idx, std::string& err) -> bool {
            return load_bind_list(core, config(), lua, table_idx, "siren.binds", err);
        });
    // siren.mouse = { { "mod+Button1", "move" }, { "mod+Button3", "resize" }, ... }
    config().register_lua_assignment_handler("mouse", [this](LuaContext& lua, int table_idx, std::string& err) -> bool {
            return load_mouse_bind_list(config(), lua, table_idx, "siren.mouse", err);
        });
    config().register_runtime_setting("modifier", RuntimeSettingSpec{
        .expected_type = RuntimeValueType::String,
        .validate      = validate_modifier_value,
        .apply         = [this](const RuntimeValue& value) {
            const auto* s = value.as_string();
            if (!s)
                return;
            uint16_t mask = parse_physical_modifier(*s);
            if (!mask)
                return;
            config().set_mod_mask(mask);
        },
    });
}

void KeybindingsModule::on_lua_init(Core&) {}

void KeybindingsModule::stop_drag() {
    if (!drag_active())
        return;
    if (input_)
        input_->ungrab_pointer();
    drag = {};
}

void KeybindingsModule::maybe_cancel_drag_for_window(WindowId window) {
    if (!drag_active())
        return;
    if (drag.window != window)
        return;
    stop_drag();
}

void KeybindingsModule::on(Core& core, event::KeyPressEv ev) {
    uint32_t keysym = ev.keysym;
    uint16_t mods   = ev.mods & ~STRIP_MODS_MASK;

    // Copy-out handler before execution: callbacks like siren.reload() mutate
    // keybinding storage and would invalidate references during iteration.
    auto keybindings = core.get_keybindings();
    for (auto& kb : keybindings) {
        if (kb.keysym == keysym && kb.mods == mods) {
            auto handler = kb.handler;
            handler();
            return;
        }
    }
}

void KeybindingsModule::on_start(Core& core) {
    input_ = runtime().backend().input_port();
    if (!input_) {
        LOG_ERR("Keybindings: backend does not provide InputPort");
        return;
    }

    for (auto& kb : core.get_keybindings())
        input_->grab_key(kb.keysym, kb.mods);

    input_->flush();
}

void KeybindingsModule::on_stop(Core&, bool) {
    stop_drag();
}

void KeybindingsModule::on_reload(Core& core) {
    if (!input_)
        return;
    stop_drag();
    input_->ungrab_all_keys();
    on_start(core);
    reinstall_mouse_grabs_for_all_windows(*input_, core, config());
}

static void call_mouse_ref(Config& config, const LuaRegistryRef& callback, const event::ButtonEv& ev) {
    auto& lua = config.lua();
    lua.call_ref_with_int_fields(callback, {
        { "window", ev.window },
        { "root_x", ev.root_x },
        { "root_y", ev.root_y },
        { "button", ev.button },
    }, "mouse.bind callback error");
}

void KeybindingsModule::on(Core& core, event::ButtonEv ev) {
    uint16_t mods = ev.state & ~STRIP_MODS_MASK;
    for (auto& mb : config().get_mouse_bindings()) {
        if (mb.button != ev.button || mb.mods != mods)
            continue;
        if (ev.release)
            call_mouse_ref(config(), mb.release_callback, ev);
        else
            call_mouse_ref(config(), mb.press_callback, ev);
    }

    if (ev.release) {
        if (drag_active() && drag.op == DragOp::Move) {
            // After a move drag, reassign the window to the active workspace of
            // whichever monitor its center now overlaps.
            auto window = core.window_state(drag.window);
            if (window) {
                int         cx   = window->x + (int)window->width  / 2;
                int         cy   = window->y + (int)window->height / 2;
                const auto& mons = core.monitor_states();
                for (int i = 0; i < (int)mons.size(); i++) {
                    const auto& mon = mons[(size_t)i];
                    if (cx >= mon.x && cx < mon.x + mon.width &&
                        cy >= mon.y && cy < mon.y + mon.height) {
                        int cur_ws  = core.workspace_of_window(drag.window);
                        int dest_ws = mon.active_ws;
                        if (dest_ws >= 0 && dest_ws != cur_ws)
                            (void)core.dispatch(command::MoveWindowToWorkspace{ drag.window, dest_ws });
                        break;
                    }
                }
            }
        }
        stop_drag();
        return;
    }

    // Find matching builtin binding for this button+mods combo.
    Config::MouseAction builtin = Config::MouseAction::None;
    for (const auto& mb : config().get_mouse_bindings()) {
        if (mb.builtin != Config::MouseAction::None &&
            mb.button == ev.button && mb.mods == mods) {
            builtin = mb.builtin;
            break;
        }
    }
    if (builtin == Config::MouseAction::None)
        return;

    auto window = core.window_state(ev.window);
    if (!window)
        return;

    if (builtin == Config::MouseAction::Float) {
        (void)core.dispatch(command::ToggleWindowFloating{ ev.window });
        (void)core.dispatch(command::ReconcileNow{});
        return;
    }

    drag.window   = ev.window;
    drag.start_rx = ev.root_x;
    drag.start_ry = ev.root_y;
    drag.start_wx = window->x;
    drag.start_wy = window->y;
    drag.start_ww = window->width;
    drag.start_wh = window->height;
    drag.op       = (builtin == Config::MouseAction::Move) ? DragOp::Move : DragOp::Resize;

    if (!core.is_window_floating(ev.window)) {
        (void)core.dispatch(command::SetWindowFloating{ ev.window, true });
        (void)core.dispatch(command::ReconcileNow{});
    }

    if (input_) {
        input_->grab_pointer();
        if (drag.op == DragOp::Resize) {
            // Re-read geometry after potential ReconcileNow (tiled→float promotes window).
            auto cur = core.window_state(drag.window);
            if (cur) {
                drag.start_wx = cur->x;
                drag.start_wy = cur->y;
                drag.start_ww = cur->width;
                drag.start_wh = cur->height;
            }
            // Warp to bottom-right corner in root coordinates and sync start_rx/ry
            // so the first MotionEv produces zero delta.
            int16_t warp_x = (int16_t)(drag.start_wx + (int)drag.start_ww - 1);
            int16_t warp_y = (int16_t)(drag.start_wy + (int)drag.start_wh - 1);
            input_->warp_pointer_abs(warp_x, warp_y);
            drag.start_rx = warp_x;
            drag.start_ry = warp_y;
        }

        (void)core.dispatch(command::FocusWindow{ ev.window });
        input_->focus_window(ev.window);
        runtime().emit(core, event::FocusChanged{ ev.window });
    }
}

void KeybindingsModule::on(Core& core, event::MotionEv ev) {
    if (!drag_active())
        return;

    int dx = ev.root_x - drag.start_rx;
    int dy = ev.root_y - drag.start_ry;

    if (drag.op == DragOp::Move) {
        (void)core.dispatch(command::SetWindowPosition{
            drag.window, drag.start_wx + dx, drag.start_wy + dy
        });
        return;
    }

    auto window = core.window_state(drag.window);
    if (!window)
        return;
    static constexpr int MIN_WIN_SIZE = 32;
    int                  nw           = std::max(MIN_WIN_SIZE, ev.root_x - window->x);
    int                  nh           = std::max(MIN_WIN_SIZE, ev.root_y - window->y);
    (void)core.dispatch(command::SetWindowSize{ drag.window, (uint32_t)nw, (uint32_t)nh });
}

void KeybindingsModule::on(Core&, event::WindowMapped ev) {
    if (!input_)
        return;
    input_->ungrab_all_buttons(ev.window);
    install_mouse_grabs_for_window(*input_, ev.window, config());
}

void KeybindingsModule::on(Core&, event::WindowUnmapped ev) {
    maybe_cancel_drag_for_window(ev.window);
}

void KeybindingsModule::on(Core&, event::DestroyNotify ev) {
    maybe_cancel_drag_for_window(ev.window);
}

KeybindingsModule::~KeybindingsModule() {
    stop_drag();
}

static bool _swm_registered_lua_symbols_keybindings = []() {
        module_registry_static::add_lua_symbol_registration("mouse", "keybindings");
        module_registry_static::add_lua_symbol_registration("binds", "keybindings");
        return true;
    }();

SWM_REGISTER_MODULE("keybindings", KeybindingsModule)