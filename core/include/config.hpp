#pragma once

#include <string>
#include <vector>
#include <optional>
#include <cstdint>
#include <stdexcept>
#include <functional>
#include <unordered_map>
#include <algorithm>
#include <bar_config.hpp>
#include <config_types.hpp>
#include <lua_host.hpp>
#include <runtime_config.hpp>

class Core;
class Runtime;

struct RuntimeSettingSpec {
    RuntimeValueType                                               expected_type = RuntimeValueType::Null;
    // Optional custom validator: return error text on failure.
    std::function<std::optional<std::string>(const RuntimeValue&)> validate;
    // Apply parsed value into Config/Core-owned state.
    std::function<void(const RuntimeValue&)>                       apply;
};

struct BarWidget {
    std::string    name;
    LuaRegistryRef callback;
    int            interval = 1; // refresh every N timer ticks (1s base); 0 = every redraw
};

class Config {
    public:
        using LuaAssignmentHandler = std::function<bool (LuaContext&, int, std::string&)>;
        Config() = default;

    private:
        std::vector<MonitorAlias> monitor_aliases;
        MonitorCompose monitor_compose;
        std::vector<WorkspaceDef> workspace_defs;
        std::vector<WindowRule> window_rules;
        std::optional<BarConfig> bar_config;
        std::optional<BarConfig> bottom_bar_config;
        std::optional<uint16_t> mod_mask;
        bool follow_moved_window_ = false;
        bool focus_new_window_    = true;
        std::vector<BarWidget> bar_widgets;
        ThemeConfig theme_;
        std::unordered_map<std::string, LuaAssignmentHandler> lua_assignment_handlers;
        std::unordered_map<std::string, RuntimeSettingSpec> runtime_settings;
        LuaHost lua_host;
        // Non-owning. Set by bind_runtime_handles() before any Lua load.
        // Null until bound; checked at access via bound_core()/bound_runtime().
        Core* bound_core_       = nullptr;
        Runtime* bound_runtime_ = nullptr;

    public:
        enum class MouseAction { None, Move, Resize, Float };

        struct MouseBinding {
            uint16_t       mods;
            uint32_t       button;
            MouseAction    builtin = MouseAction::None;
            LuaRegistryRef press_callback;
            LuaRegistryRef release_callback;
        };

        struct Snapshot {
            std::vector<MonitorAlias> monitor_aliases;
            MonitorCompose            monitor_compose;
            std::vector<WorkspaceDef> workspace_defs;
            std::vector<WindowRule>   window_rules;
            std::optional<BarConfig>  bar_config;
            std::optional<BarConfig>  bottom_bar_config;
            std::optional<uint16_t>   mod_mask;
            bool                      follow_moved_window = false;
            bool                      focus_new_window    = true;
            std::vector<BarWidget>    bar_widgets;
            std::vector<MouseBinding> mouse_bindings;
            ThemeConfig               theme;
        };

    private:
        std::vector<MouseBinding> mouse_bindings;

    public:
        bool load(const std::string& path,
            Core& core,
            Runtime& runtime,
            bool reset_lua_vm = true);

        void bind_runtime_handles(Core& core, Runtime& runtime) {
            bound_core_    = &core;
            bound_runtime_ = &runtime;
        }

        Core& bound_core() const {
            if (!bound_core_)
                throw std::logic_error("Config: core is not bound");
            return *bound_core_;
        }

        Runtime& bound_runtime() const {
            if (!bound_runtime_)
                throw std::logic_error("Config: runtime is not bound");
            return *bound_runtime_;
        }

        // Returns list of fatal errors. Empty = config is valid.
        std::vector<std::string> validate() const;

        void add_monitor_alias(MonitorAlias a) { monitor_aliases.push_back(std::move(a)); }
        void set_monitor_aliases(std::vector<MonitorAlias> v) { monitor_aliases = std::move(v); }
        void set_monitor_compose(MonitorCompose c) { monitor_compose = std::move(c); }
        void add_workspace_def(WorkspaceDef w) { workspace_defs.push_back(std::move(w)); }
        void set_workspace_defs(std::vector<WorkspaceDef> v) { workspace_defs = std::move(v); }
        void add_window_rule(WindowRule r)     { window_rules.push_back(std::move(r)); }

        const std::vector<MonitorAlias>& get_monitor_aliases() const { return monitor_aliases; }
        const MonitorCompose&            get_monitor_compose() const { return monitor_compose; }
        const std::vector<WorkspaceDef>& get_workspace_defs()  const { return workspace_defs; }
        const std::vector<WindowRule>&   get_window_rules()    const { return window_rules; }

        void set_bar_config(BarConfig cfg)        { bar_config = std::move(cfg); }
        void set_bottom_bar_config(BarConfig cfg) { bottom_bar_config = std::move(cfg); }
        const std::optional<BarConfig>& get_bar_config()        const { return bar_config; }
        const std::optional<BarConfig>& get_bottom_bar_config() const { return bottom_bar_config; }

        void set_mod_mask(uint16_t m) { mod_mask = m; }
        bool     has_mod_mask()  const { return mod_mask.has_value(); }
        uint16_t get_mod_mask()  const {
            if (!mod_mask)
                throw std::logic_error("Config: mod_mask not set — set siren.modifier in init.lua");
            return *mod_mask;
        }

        void set_follow_moved_window(bool v) { follow_moved_window_ = v; }
        bool get_follow_moved_window() const { return follow_moved_window_; }

        void set_focus_new_window(bool v) { focus_new_window_ = v; }
        bool get_focus_new_window() const { return focus_new_window_; }

        void add_mouse_binding(MouseBinding mb) { mouse_bindings.push_back(std::move(mb)); }
        const std::vector<MouseBinding>& get_mouse_bindings() const { return mouse_bindings; }

        void add_bar_widget(const std::string& name, LuaRegistryRef callback, int interval = 1) {
            bar_widgets.push_back({ name, std::move(callback), interval });
        }
        const std::vector<BarWidget>& get_bar_widgets() const { return bar_widgets; }

        void              set_theme(ThemeConfig t)  { theme_ = std::move(t); }
        const ThemeConfig& get_theme()         const { return theme_; }

        Snapshot snapshot() const {
            Snapshot out;
            out.monitor_aliases     = monitor_aliases;
            out.monitor_compose     = monitor_compose;
            out.workspace_defs      = workspace_defs;
            out.window_rules        = window_rules;
            out.bar_config          = bar_config;
            out.bottom_bar_config   = bottom_bar_config;
            out.mod_mask            = mod_mask;
            out.follow_moved_window = follow_moved_window_;
            out.focus_new_window    = focus_new_window_;
            out.bar_widgets         = bar_widgets;
            out.mouse_bindings      = mouse_bindings;
            out.theme               = theme_;
            return out;
        }

        void restore(const Snapshot& snap) {
            monitor_aliases      = snap.monitor_aliases;
            monitor_compose      = snap.monitor_compose;
            workspace_defs       = snap.workspace_defs;
            window_rules         = snap.window_rules;
            bar_config           = snap.bar_config;
            bottom_bar_config    = snap.bottom_bar_config;
            mod_mask             = snap.mod_mask;
            follow_moved_window_ = snap.follow_moved_window;
            focus_new_window_    = snap.focus_new_window;
            bar_widgets          = snap.bar_widgets;
            mouse_bindings       = snap.mouse_bindings;
            theme_               = snap.theme;
        }

        LuaHost& lua() { return lua_host; }
        const LuaHost& lua() const { return lua_host; }

        void register_lua_assignment_handler(const std::string& key,
            LuaAssignmentHandler fn) {
            lua_assignment_handlers[key] = std::move(fn);
        }

        std::vector<std::string> lua_assignment_handler_keys() const {
            std::vector<std::string> keys;
            keys.reserve(lua_assignment_handlers.size());
            for (const auto& [k, _] : lua_assignment_handlers)
                keys.push_back(k);
            std::sort(keys.begin(), keys.end());
            return keys;
        }

        bool dispatch_lua_assignment_handler(const std::string& key,
            LuaContext& lua_ctx,
            int value_idx,
            std::string& err) const {
            auto it = lua_assignment_handlers.find(key);
            if (it == lua_assignment_handlers.end()) {
                err = "assignment handler is not registered";
                return false;
            }
            return it->second(lua_ctx, value_idx, err);
        }

        void register_runtime_setting(const std::string& key,
            RuntimeSettingSpec spec) {
            runtime_settings[key] = std::move(spec);
        }

        std::vector<std::string> runtime_setting_keys() const {
            std::vector<std::string> keys;
            keys.reserve(runtime_settings.size());
            for (const auto& [k, _] : runtime_settings)
                keys.push_back(k);
            std::sort(keys.begin(), keys.end());
            return keys;
        }

        bool apply_runtime_setting(const std::string& key,
            const RuntimeValue& value,
            std::string& err) const {
            auto it = runtime_settings.find(key);
            if (it == runtime_settings.end()) {
                err = "setting is not registered";
                return false;
            }
            const RuntimeSettingSpec& spec = it->second;
            if (runtime_value_type(value) != spec.expected_type) {
                err = "expected " + std::string(runtime_value_type_name(spec.expected_type)) +
                    ", got " + runtime_value_type_name(runtime_value_type(value));
                return false;
            }
            if (spec.validate) {
                auto v = spec.validate(value);
                if (v.has_value()) {
                    err = *v;
                    return false;
                }
            }
            if (spec.apply)
                spec.apply(value);
            return true;
        }

        void clear() {
            monitor_aliases.clear();
            monitor_compose = {};
            workspace_defs.clear();
            window_rules.clear();
            bar_config.reset();
            bottom_bar_config.reset();
            mod_mask.reset();
            follow_moved_window_ = false;
            focus_new_window_    = true;
            bar_widgets.clear();
            mouse_bindings.clear();
            theme_ = {};
        }
};