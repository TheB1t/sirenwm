#include "keyboard_module.hpp"

#include <backend/backend.hpp>
#include <backend/keyboard_port.hpp>

#include <log.hpp>
#include <module_registry.hpp>
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
    auto* kp = backend().keyboard_port();
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

void KeyboardModule::on_stop(bool /*is_exec_restart*/) {
    auto* kp = backend().keyboard_port();
    if (kp)
        kp->restore();
}

SIRENWM_REGISTER_MODULE("keyboard", KeyboardModule)
