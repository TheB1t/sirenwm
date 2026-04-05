#include "rules.hpp"
#include <config.hpp>
#include <core.hpp>
#include <log.hpp>
#include <module_registry.hpp>

#include <string>
#include <cctype>

static std::string lower_ascii(std::string s) {
    for (char& c : s)
        c = (char)std::tolower((unsigned char)c);
    return s;
}

static std::optional<std::string> parse_runtime_rules(const RuntimeValue& value,
    std::vector<WindowRule>& out_rules) {
    const auto* arr = value.as_array();
    if (!arr)
        return std::string("must be an array");

    auto parse_int_field = [](const RuntimeValue::Object& obj,
        const char* key,
        int& out) -> std::optional<std::string> {
            auto it = obj.find(key);
            if (it == obj.end())
                return std::nullopt;
            if (const auto* iv = it->second.as_int()) {
                out = (int)*iv;
                return std::nullopt;
            }
            return std::string("'") + key + "' must be an integer";
        };

    auto parse_bool_field = [](const RuntimeValue::Object& obj,
        const char* key,
        bool& out,
        bool& seen) -> std::optional<std::string> {
            auto it = obj.find(key);
            if (it == obj.end())
                return std::nullopt;
            if (const auto* bv = it->second.as_bool()) {
                out  = *bv;
                seen = true;
                return std::nullopt;
            }
            return std::string("'") + key + "' must be a boolean";
        };

    for (size_t i = 0; i < arr->size(); i++) {
        const RuntimeValue& v   = (*arr)[i];
        const auto*         obj = v.as_object();
        if (!obj)
            return "entry #" + std::to_string(i + 1) + " must be an object";

        WindowRule rule;

        auto       class_it = obj->find("class");
        if (class_it != obj->end()) {
            if (const auto* s = class_it->second.as_string())
                rule.class_name = *s;
            else
                return "entry #" + std::to_string(i + 1) + ": 'class' must be a string";
        }

        auto inst_it = obj->find("instance");
        if (inst_it != obj->end()) {
            if (const auto* s = inst_it->second.as_string())
                rule.instance_name = *s;
            else
                return "entry #" + std::to_string(i + 1) + ": 'instance' must be a string";
        }

        if (obj->find("floating") != obj->end())
            return "entry #" + std::to_string(i + 1) + ": 'floating' is removed, use 'isfloating'";

        int workspace_1idx = 0;
        if (auto e = parse_int_field(*obj, "workspace", workspace_1idx); e.has_value())
            return "entry #" + std::to_string(i + 1) + ": " + *e;
        if (workspace_1idx > 0)
            rule.workspace = workspace_1idx - 1;

        bool seen_isfloating = false;
        if (auto e = parse_bool_field(*obj, "isfloating", rule.isfloating, seen_isfloating); e.has_value())
            return "entry #" + std::to_string(i + 1) + ": " + *e;

        if (rule.class_name.empty() && rule.instance_name.empty())
            return "entry #" + std::to_string(i + 1) + ": at least one of class/instance is required";

        out_rules.push_back(std::move(rule));
    }

    return std::nullopt;
}

static bool rule_matches(const WindowRule& rule,
    const std::string& cls,
    const std::string& instance) {
    std::string c  = lower_ascii(cls);
    std::string in = lower_ascii(instance);

    if (!rule.class_name.empty() &&
        lower_ascii(rule.class_name) != c)
        return false;

    if (!rule.instance_name.empty() &&
        lower_ascii(rule.instance_name) != in)
        return false;

    return !rule.class_name.empty() || !rule.instance_name.empty();
}

void RulesModule::on_init(Core& core) {
    (void)core;
    config().register_runtime_setting("rules", RuntimeSettingSpec{
        .expected_type = RuntimeValueType::Array,
        .validate      = [](const RuntimeValue& value) -> std::optional<std::string> {
            std::vector<WindowRule> tmp;
            return parse_runtime_rules(value, tmp);
        },
        .apply = [this](const RuntimeValue& value) {
            std::vector<WindowRule> rules;
            auto err = parse_runtime_rules(value, rules);
            if (err.has_value()) {
                LOG_ERR("rules.apply: unexpected parse failure after validation: %s",
                err->c_str());
                return;
            }
            for (auto& r : rules)
                config().add_window_rule(std::move(r));
        },
    });
}

void RulesModule::on_lua_init(Core&) {}

void RulesModule::on(Core& core, event::ApplyWindowRules ev) {
    // Windows restored from a restart snapshot already have their workspace
    // and floating state saved — skip all rules to preserve state as-is.
    if (ev.from_restart)
        return;

    WindowId win    = ev.window;
    auto     window = core.window_state_any(win);
    if (!window)
        return;

    const std::string& instance = window->wm_instance;
    const std::string& cls      = window->wm_class;

    // DWM-like defaults:
    // - utility/dialog/splash/modal windows float
    // - fixed-size windows are handled in handle_map_request: fullscreen-sized
    //   ones become borderless, smaller ones get floating set there explicitly.
    if (window->is_dialog())
        (void)core.dispatch(command::SetWindowFloating{ win, true });

    if (instance.empty() && cls.empty())
        return;

    LOG_DEBUG("Rules: window %d class='%s' instance='%s'", win, cls.c_str(), instance.c_str());

    for (auto& rule : config().get_window_rules()) {
        if (!rule_matches(rule, cls, instance))
            continue;

        if (rule.isfloating)
            (void)core.dispatch(command::SetWindowFloating{ win, true });

        if (rule.workspace >= 0) {
            bool visible = core.is_workspace_visible(rule.workspace);

            LOG_INFO("Rules: moving '%s' to workspace %d", cls.c_str(), rule.workspace + 1);
            // Do not steal focus/jump workspace on initial map when a workspace
            // rule explicitly routes the new window.
            (void)core.dispatch(command::SetWindowSuppressFocusOnce{ win, true });
            (void)core.dispatch(command::MoveWindowToWorkspace{ win, rule.workspace });
        }
        return;
    }
}

SWM_REGISTER_MODULE("rules", RulesModule)