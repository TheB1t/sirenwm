#include "keyboard_module.hpp"

#include <backend/backend.hpp>
#include <backend/keyboard_port.hpp>
#include <config.hpp>
#include <log.hpp>
#include <module_registry.hpp>
#include <runtime.hpp>

// ---------------------------------------------------------------------------
// Lua config: siren.keyboard = { layouts = "us,ru", options = "grp:alt_shift_toggle" }
// ---------------------------------------------------------------------------

static bool parse_keyboard_table(LuaContext& lua, int idx,
    std::vector<std::string>& layouts_out,
    std::string& options_out,
    std::string& err) {
    if (!lua.is_table(idx)) {
        err = "siren.keyboard must be a table";
        return false;
    }

    layouts_out.clear();
    options_out.clear();

    lua.get_field(idx, "layouts");
    if (lua.is_string(-1)) {
        std::string            raw = lua.to_string(-1);
        // Split comma-separated layout string.
        std::string::size_type pos = 0;
        while (pos < raw.size()) {
            auto        comma = raw.find(',', pos);
            std::string tok   = (comma == std::string::npos)
                ? raw.substr(pos)
                : raw.substr(pos, comma - pos);
            if (!tok.empty())
                layouts_out.push_back(tok);
            if (comma == std::string::npos) break;
            pos = comma + 1;
        }
    } else if (!lua.is_nil(-1)) {
        err = "siren.keyboard.layouts must be a string (e.g. \"us,ru\")";
        lua.pop(1);
        return false;
    }
    lua.pop(1);

    lua.get_field(idx, "options");
    if (lua.is_string(-1))
        options_out = lua.to_string(-1);
    else if (!lua.is_nil(-1)) {
        err = "siren.keyboard.options must be a string";
        lua.pop(1);
        return false;
    }
    lua.pop(1);

    return true;
}

static void register_lua(Config& config,
    std::vector<std::string>& layouts,
    std::string& options) {
    config.register_lua_assignment_handler("keyboard",
        [&layouts, &options](LuaContext& lua, int idx, std::string& err) -> bool {
            return parse_keyboard_table(lua, idx, layouts, options, err);
        });
}

// ---------------------------------------------------------------------------
// Module lifecycle
// ---------------------------------------------------------------------------

void KeyboardModule::apply() {
    if (layouts_.empty())
        return;
    auto* kp = runtime().backend().keyboard_port();
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

void KeyboardModule::on_init(Core&) {
    register_lua(config(), layouts_, options_);
}

void KeyboardModule::on_lua_init(Core&) {
    register_lua(config(), layouts_, options_);
}

void KeyboardModule::on_start(Core&) {
    apply();
}

void KeyboardModule::on_reload(Core&) {
    apply();
}

void KeyboardModule::on_stop(Core&, bool /*is_exec_restart*/) {
    auto* kp = runtime().backend().keyboard_port();
    if (kp)
        kp->restore();
}

SWM_REGISTER_MODULE("keyboard", KeyboardModule)