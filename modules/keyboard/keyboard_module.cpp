#include "keyboard_module.hpp"

#include <backend/backend.hpp>
#include <backend/keyboard_port.hpp>
#include <backend/events.hpp>

#include <log.hpp>
#include <module_registry.hpp>
#include <protocol/keyboard.hpp>
#include <runtime.hpp>

// ---------------------------------------------------------------------------
// Lua config parsing
// ---------------------------------------------------------------------------

bool KeyboardModule::parse_setup(LuaContext& lua, int idx, std::string& err) {
    if (!lua.is_table(idx)) {
        err = "keyboard.setup: expected table";
        return false;
    }

    layouts_.clear();
    options_.clear();

    lua.get_field(idx, "layouts");
    if (lua.is_string(-1)) {
        std::string            raw = lua.to_string(-1);
        std::string::size_type pos = 0;
        while (pos < raw.size()) {
            auto        comma = raw.find(',', pos);
            std::string tok   = (comma == std::string::npos)
                ? raw.substr(pos)
                : raw.substr(pos, comma - pos);
            if (!tok.empty())
                layouts_.push_back(tok);
            if (comma == std::string::npos) break;
            pos = comma + 1;
        }
    } else if (!lua.is_nil(-1)) {
        err = "keyboard.setup: layouts must be a string (e.g. \"us,ru\")";
        lua.pop(1);
        return false;
    }
    lua.pop(1);

    lua.get_field(idx, "options");
    if (lua.is_string(-1))
        options_ = lua.to_string(-1);
    else if (!lua.is_nil(-1)) {
        err = "keyboard.setup: options must be a string";
        lua.pop(1);
        return false;
    }
    lua.pop(1);

    return true;
}

// ---------------------------------------------------------------------------
// Module lifecycle
// ---------------------------------------------------------------------------

void KeyboardModule::apply() {
    if (layouts_.empty())
        return;
    auto* kp = &runtime().ports().keyboard;
    if (!kp) {
        LOG_WARN("keyboard: no keyboard port available");
        return;
    }
    kp->apply(layouts_, options_);
    LOG_INFO("keyboard: applied layouts='%s' options='%s'",
        [&]() {
            std::string s;
            for (size_t i = 0; i < layouts_.size(); ++i) {
                if (i) s += ',';
                s += layouts_[i];
            }
            return s;
        }().c_str(),
        options_.c_str());
}

void KeyboardModule::on_init() {}

void KeyboardModule::on_lua_init() {
    auto& lua = this->lua();
    auto  ctx = lua.context();

    // Proxy table: kbd.settings = {...} triggers parse_setup immediately.
    ctx.new_table();   // proxy
    ctx.new_table();   // metatable
    lua.push_callback([](LuaContext& lctx, void* ud) -> int {
            // __newindex(proxy, key, value)
            std::string key = lctx.is_string(2) ? lctx.to_string(2) : "";
            if (key == "settings") {
                auto*       mod = static_cast<KeyboardModule*>(ud);
                std::string err;
                if (!mod->parse_setup(lctx, 3, err))
                    LOG_ERR("%s", err.c_str());
            }
            return 0;
        }, this);
    ctx.set_field(-2, "__newindex");
    ctx.set_metatable(-2);

    lua.set_module_table("keyboard");
}

void KeyboardModule::on_start() {
    apply();
}

void KeyboardModule::on_reload() {
    apply();
}

void KeyboardModule::on_stop(bool is_exec_restart) {
    // Skip XKB restore on exec-restart: XkbGetKeyboardByName crashes when called
    // while the X connection is being torn down. The new process re-applies
    // settings via apply() in on_start().
    if (is_exec_restart)
        return;
    auto* kp = &runtime().ports().keyboard;
    if (kp)
        kp->restore();
}

// ---------------------------------------------------------------------------
// Per-window layout tracking
// ---------------------------------------------------------------------------

void KeyboardModule::on(event::FocusChanged ev) {
    auto* kp = &runtime().ports().keyboard;
    if (!kp) return;

    // Save current group for the window that is losing focus.
    if (focused_window_ != NO_WINDOW)
        window_groups_[focused_window_] = kp->get_group();

    focused_window_ = ev.window;

    // Restore saved group for the newly focused window (or group 0 if unknown).
    uint32_t group = 0;
    auto     it    = window_groups_.find(ev.window);
    if (it != window_groups_.end())
        group = it->second;
    kp->set_group(group);

    // Notify listeners (e.g. bar widgets) about the layout change.
    std::string layout = kp->current_layout();
    runtime().post_event(event::CustomEvent{
        MessageEnvelope::pack(protocol::keyboard::LayoutChanged::from(layout))
    });
}

void KeyboardModule::on(event::WindowUnmapped ev) {
    window_groups_.erase(ev.window);
    if (focused_window_ == ev.window)
        focused_window_ = NO_WINDOW;
}

SIRENWM_REGISTER_MODULE("keyboard", KeyboardModule)
