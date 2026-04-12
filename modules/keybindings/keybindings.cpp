#include "keybindings.hpp"
#include <backend/backend.hpp>
#include <backend/input_port.hpp>
#include <core.hpp>
#include <runtime.hpp>
#include <module_registry.hpp>
#include <log.hpp>

#include <string_utils.hpp>
#include <algorithm>
#include <string>
#include <sstream>
#include <type_traits>
#include <cstdlib>
#include <optional>
#include <cstdint>
#include <vector>

static uint16_t parse_physical_modifier(const std::string& name) {
    return backend::parse_modifier_name(lower_ascii(name));
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
    const TypedSetting<std::optional<uint16_t>>& mod_mask,
    uint16_t& out,
    std::string* err_out = nullptr) {
    auto t = lower_ascii(token);
    if (t == "mod") {
        if (!mod_mask.get().has_value()) {
            if (err_out)
                *err_out = "primary modifier is not set (set siren.modifier first)";
            return false;
        }
        out = *mod_mask.get();
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
    const TypedSetting<std::optional<uint16_t>>& mod_mask,
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
        if (parse_binding_modifier_token(token, mod_mask, mod, &mod_err)) {
            mods_out |= mod;
            continue;
        }
        if (token_lower == "mod") {
            if (err_out)
                *err_out = mod_err.empty() ? "primary modifier is not set" : mod_err;
            return false;
        }

        keysym_out = backend::keysym_from_name(token);
        if (!keysym_out)
            keysym_out = backend::keysym_from_name(token_lower);
        if (!keysym_out) {
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
    LuaHost& host,
    const TypedSetting<std::optional<uint16_t>>& mod_mask,
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
    if (!parse_key(spec, mod_mask, mods, keysym, &key_err)) {
        if (err_out) {
            if (key_err.empty())
                *err_out = std::string(origin) + ": invalid key spec";
            else
                *err_out = std::string(origin) + ": " + key_err;
        }
        return false;
    }

    auto callback = host.ref_function(fn_index);
    if (!callback.valid()) {
        if (err_out)
            *err_out = std::string(origin) + ": callback must be a function";
        return false;
    }

    core.register_keybinding(mods, keysym, [&host, callback]() {
            host.call_ref(callback, 0, 0, "key callback error");
        });
    return true;
}

static bool load_bind_list(Core& core,
    LuaHost& host,
    const TypedSetting<std::optional<uint16_t>>& mod_mask,
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
            if (!register_lua_keybinding(core, host, mod_mask, lua, spec, fn_idx, label.c_str(), &bind_err))
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
    const TypedSetting<std::optional<uint16_t>>& mod_mask,
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
            try {
                btn_out = (uint32_t)std::stoul(token_lower.substr(6));
            } catch (...) {
                if (err_out)
                    *err_out = "invalid button number in '" + token + "'";
                return false;
            }
            continue;
        }
        uint16_t    mod = 0;
        std::string mod_err;
        if (!parse_binding_modifier_token(token, mod_mask, mod, &mod_err)) {
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

static bool load_mouse_bind_list(LuaHost& host,
    TypedSetting<std::vector<MouseBinding>>& mouse_bindings,
    const TypedSetting<std::optional<uint16_t>>& mod_mask,
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
        if (!parse_mouse_spec(spec, mod_mask, mods, btn, &spec_err)) {
            mark_error(std::string(origin) + "[" + std::to_string(i) + "]: " + spec_err);
            lua.pop();
            continue;
        }

        MouseBinding mb;
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
            if (action == "move")        mb.builtin = MouseAction::Move;
            else if (action == "resize") mb.builtin = MouseAction::Resize;
            else if (action == "float")  mb.builtin = MouseAction::Float;
            else {
                mark_error(std::string(origin) + "[" + std::to_string(i) + "]: unknown builtin action '" + action +
                    "' (use \"move\", \"resize\", \"float\")");
                lua.pop();
                continue;
            }
        } else if (handler_idx != 0 && lua.is_function(handler_idx)) {
            mb.press_callback = host.ref_function(handler_idx);
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
                mb.press_callback = host.ref_function(-1);
            lua.pop();

            lua.get_field(callback_table_idx, "release");
            if (lua.is_function(-1))
                mb.release_callback = host.ref_function(-1);
            lua.pop();

            if (handler_idx != 0)
                lua.pop();
        }

        if (mb.builtin == MouseAction::None &&
            !mb.press_callback.valid() && !mb.release_callback.valid()) {
            mark_error(std::string(origin) + "[" + std::to_string(i) + "]: missing callback");
            lua.pop();
            continue;
        }

        mouse_bindings.get_mut().push_back(std::move(mb));
        lua.pop();
    }

    if (!ok)
        err_out = first_err.empty() ? std::string(origin) + ": invalid mouse binds" : first_err;
    return ok;
}

static void install_mouse_grabs_for_window(backend::InputPort& input,
    WindowId win,
    const std::vector<MouseBinding>& bindings) {
    std::vector<std::pair<uint8_t, uint16_t>> grabs;
    auto                                      push_unique = [&](uint32_t button, uint16_t mods) {
            uint8_t b = (uint8_t)button;
            for (auto& g : grabs)
                if (g.first == b && g.second == mods) return;
            grabs.push_back({ b, mods });
        };

    for (auto& mb : bindings)
        push_unique(mb.button, mb.mods);

    for (auto& g : grabs)
        input.grab_button(win, g.first, g.second);
}

static void reinstall_mouse_grabs_for_all_windows(backend::InputPort& input,
    Core& core,
    const std::vector<MouseBinding>& bindings) {
    for (auto win : core.all_window_ids()) {
        input.ungrab_all_buttons(win);
        install_mouse_grabs_for_window(input, win, bindings);
    }
    input.flush();
}

void KeybindingsModule::on_init() {
    mod_mask_.set_parse(
        [](const RuntimeValue& value, std::optional<uint16_t>& out) -> std::optional<std::string> {
            const auto* s = value.as_string();
            if (!s)
                return std::string("must be a string");
            auto err = validate_modifier_value(value);
            if (err) return err;
            out = parse_physical_modifier(*s);
            return std::nullopt;
        },
        RuntimeValueType::String);
    store().register_setting("modifier", mod_mask_);
    store().register_setting("mouse_bindings", mouse_bindings_);
}

void KeybindingsModule::on_lua_init() {
    // Reset pending refs — new Lua VM epoch on reload.
    pending_binds_ = {};
    pending_mouse_ = {};

    auto& host = lua();
    auto  ctx  = host.context();

    // Module table: __newindex stores refs, applied after siren.modifier is set.
    ctx.new_table(); // module table (proxy)
    ctx.new_table(); // metatable

    host.push_callback([](LuaContext& lctx, void* ud) -> int {
            if (!lctx.is_string(2) || !lctx.is_table(3))
                return 0;
            auto*       mod = static_cast<KeybindingsModule*>(ud);
            std::string key = lctx.to_string(2);
            if (key == "binds")
                mod->pending_binds_ = mod->lua().ref_value(3);
            else if (key == "mouse")
                mod->pending_mouse_ = mod->lua().ref_value(3);
            return 0;
        }, this);
    ctx.set_field(-2, "__newindex");

    ctx.set_metatable(-2);
    host.set_module_table("keybindings");
}

void KeybindingsModule::apply_pending() {
    auto& host = lua();
    if (pending_binds_.valid()) {
        if (host.push_ref(pending_binds_)) {
            auto        ctx = host.context();
            std::string err;
            if (!load_bind_list(core(), host, mod_mask_, ctx, -1, "kb.binds", err))
                LOG_ERR("keybindings: kb.binds: %s", err.c_str());
            ctx.pop();
        }
        pending_binds_ = {};
    }
    if (pending_mouse_.valid()) {
        if (host.push_ref(pending_mouse_)) {
            auto        ctx = host.context();
            std::string err;
            if (!load_mouse_bind_list(host, mouse_bindings_, mod_mask_, ctx, -1, "kb.mouse", err))
                LOG_ERR("keybindings: kb.mouse: %s", err.c_str());
            ctx.pop();
        }
        pending_mouse_ = {};
    }
}

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

void KeybindingsModule::on(event::KeyPressEv ev) {
    uint32_t keysym = ev.keysym;
    uint16_t mods   = ev.mods & ~backend::MOD_STRIP_MASK;

    // Copy-out handler before execution: callbacks like siren.reload() mutate
    // keybinding storage and would invalidate references during iteration.
    auto keybindings = core().get_keybindings();
    for (auto& kb : keybindings) {
        if (kb.keysym == keysym && kb.mods == mods) {
            auto handler = kb.handler;
            handler();
            return;
        }
    }
}

void KeybindingsModule::on_start() {
    apply_pending();

    input_ = &runtime().ports().input;

    for (auto& kb : core().get_keybindings())
        input_->grab_key(kb.keysym, kb.mods);

    input_->flush();
}

void KeybindingsModule::on_stop(bool) {
    stop_drag();
}

void KeybindingsModule::on_reload() {
    if (!input_)
        return;
    stop_drag();
    input_->ungrab_all_keys();
    on_start();
    reinstall_mouse_grabs_for_all_windows(*input_, core(), mouse_bindings_.get());
}

static void call_mouse_ref(LuaHost& host, const LuaRegistryRef& callback, const event::ButtonEv& ev) {
    host.call_ref_with_int_fields(callback, {
        { "window", ev.window },
        { "root_x", ev.root_pos.x() },
        { "root_y", ev.root_pos.y() },
        { "button", ev.button },
    }, "mouse.bind callback error");
}

void KeybindingsModule::on(event::ButtonEv ev) {
    uint16_t mods = ev.state & ~backend::MOD_STRIP_MASK;

    // Click-to-focus grabs (grab_button_any, GrabModeSync) freeze the pointer
    // until AllowEvents fires; replay so the client still receives the click.
    // Modifier-bound WM actions come through async grabs — no replay needed.
    if (!ev.release && mods == 0 && ev.window != focused_window_ && input_)
        input_->allow_events(true);

    for (auto& mb : mouse_bindings_.get()) {
        if (mb.button != ev.button || mb.mods != mods)
            continue;
        if (ev.release)
            call_mouse_ref(lua(), mb.release_callback, ev);
        else
            call_mouse_ref(lua(), mb.press_callback, ev);
    }

    if (ev.release) {
        if (drag_active() && drag.op == DragOp::Move) {
            // After a move drag, reassign the window to the active workspace of
            // whichever monitor its center now overlaps.
            auto window = core().window_state(drag.window);
            if (window) {
                auto        c    = window->center();
                const auto& mons = core().monitor_states();
                for (int i = 0; i < (int)mons.size(); i++) {
                    const auto& mon = mons[(size_t)i];
                    if (mon.contains(c)) {
                        int cur_ws  = core().workspace_of_window(drag.window);
                        int dest_ws = mon.active_ws;
                        if (dest_ws >= 0 && dest_ws != cur_ws)
                            (void)core().dispatch(command::atom::MoveWindowToWorkspace{ drag.window, dest_ws });
                        break;
                    }
                }
            }
        }
        stop_drag();
        return;
    }

    // Find matching builtin binding for this button+mods combo.
    MouseAction builtin = MouseAction::None;
    for (const auto& mb : mouse_bindings_.get()) {
        if (mb.builtin != MouseAction::None &&
            mb.button == ev.button && mb.mods == mods) {
            builtin = mb.builtin;
            break;
        }
    }
    if (builtin == MouseAction::None)
        return;

    auto window = core().window_state(ev.window);
    if (!window)
        return;

    // Fullscreen and borderless windows are pinned to the monitor — do not drag/resize.
    if (window->fullscreen || window->borderless)
        return;

    if (builtin == MouseAction::Float) {
        (void)core().dispatch(command::composite::ToggleWindowFloating{ ev.window });
        (void)core().dispatch(command::atom::ReconcileNow{});
        return;
    }

    drag.window         = ev.window;
    drag.start_root     = { (int)ev.root_pos.x(), (int)ev.root_pos.y() };
    drag.start_win_pos  = window->pos();
    drag.start_win_size = window->size();
    drag.op             = (builtin == MouseAction::Move) ? DragOp::Move : DragOp::Resize;

    if (!window->floating) {
        (void)core().dispatch(command::atom::SetWindowFloating{ ev.window, true });
        (void)core().dispatch(command::atom::ReconcileNow{});
    }

    if (input_) {
        input_->grab_pointer();
        if (drag.op == DragOp::Resize) {
            // Re-read geometry after potential ReconcileNow (tiled→float promotes window).
            auto cur = core().window_state(drag.window);
            if (cur) {
                drag.start_win_pos  = cur->pos();
                drag.start_win_size = cur->size();
            }
            // Warp cursor to the window's bottom-right corner and set start_root there,
            // so the first MotionEv has zero delta and resize begins from the corner.
            Vec2i16 warp = {
                (int16_t)(drag.start_win_pos.x() + drag.start_win_size.x() - 1),
                (int16_t)(drag.start_win_pos.y() + drag.start_win_size.y() - 1)
            };
            input_->warp_pointer_abs(warp);
            drag.start_root = { warp.x(), warp.y() };
        }

        (void)core().dispatch(command::atom::FocusWindow{ ev.window });
        runtime().post_event(event::FocusChanged{ ev.window });
    }
}

void KeybindingsModule::on(event::MotionEv ev) {
    if (!drag_active())
        return;

    int dx = ev.root_pos.x() - drag.start_root.x();
    int dy = ev.root_pos.y() - drag.start_root.y();

    if (drag.op == DragOp::Move) {
        (void)core().dispatch(command::atom::SetWindowPosition{
            drag.window, { drag.start_win_pos.x() + dx, drag.start_win_pos.y() + dy }
        });
        return;
    }

    auto window = core().window_state(drag.window);
    if (!window)
        return;
    static constexpr int MIN_WIN_SIZE = 32;
    int                  nw           = std::max(MIN_WIN_SIZE, (int)ev.root_pos.x() - window->pos().x());
    int                  nh           = std::max(MIN_WIN_SIZE, (int)ev.root_pos.y() - window->pos().y());
    (void)core().dispatch(command::atom::SetWindowSize{ drag.window, { nw, nh } });
}

void KeybindingsModule::on(event::FocusChanged ev) {
    if (!input_)
        return;
    // Restore click-to-focus grab on the previously focused window.
    if (focused_window_ != NO_WINDOW && focused_window_ != ev.window)
        input_->grab_button_any(focused_window_);
    focused_window_ = ev.window;
    // Remove click-to-focus grab from the newly focused window —
    // WM action grabs (mod+button) remain in place.
    if (ev.window != NO_WINDOW) {
        input_->ungrab_all_buttons(ev.window);
        install_mouse_grabs_for_window(*input_, ev.window, mouse_bindings_.get());
    }
    input_->flush();
}

void KeybindingsModule::on(event::WindowMapped ev) {
    if (!input_)
        return;
    input_->ungrab_all_buttons(ev.window);
    install_mouse_grabs_for_window(*input_, ev.window, mouse_bindings_.get());
    // Newly mapped windows are unfocused — add click-to-focus grab.
    if (ev.window != focused_window_)
        input_->grab_button_any(ev.window);
    input_->flush();
}

void KeybindingsModule::on(event::WindowUnmapped ev) {
    maybe_cancel_drag_for_window(ev.window);
}

void KeybindingsModule::on(event::DestroyNotify ev) {
    maybe_cancel_drag_for_window(ev.window);
}

KeybindingsModule::~KeybindingsModule() {
    stop_drag();
}

SIRENWM_REGISTER_MODULE("keybindings", KeybindingsModule)
