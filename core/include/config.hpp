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
    RuntimeValueType expected_type = RuntimeValueType::Null;
    // Optional custom validator: return error text on failure.
    std::function<std::optional<std::string>(const RuntimeValue&)> validate;
    // Apply parsed value into Config/Core-owned state.
    std::function<void(const RuntimeValue&)>                       apply;
};

class Config {
    public:
        Config() = default;

    private:
        std::vector<MonitorAlias> monitor_aliases;
        MonitorCompose monitor_compose;
        std::vector<WorkspaceDef> workspace_defs;
        std::optional<BarConfig>  bar_config;
        std::optional<BarConfig>  bottom_bar_config;
        std::optional<uint16_t>   mod_mask;
        ThemeConfig theme_;
        std::unordered_map<std::string, RuntimeSettingSpec> runtime_settings;
        LuaHost lua_host;
        // Non-owning. Set by bind_runtime_handles() before any Lua load.
        // Null until bound; checked at access via bound_core()/bound_runtime().
        // Core is accessed via bound_runtime_->core().
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
            std::optional<BarConfig>  bar_config;
            std::optional<BarConfig>  bottom_bar_config;
            std::optional<uint16_t>   mod_mask;
            std::vector<MouseBinding> mouse_bindings;
            ThemeConfig               theme;
        };

    private:
        std::vector<MouseBinding> mouse_bindings;

    public:
        bool load(const std::string& path,
            Runtime& runtime,
            bool reset_lua_vm = true);

        // Parse-only check: compile the Lua file without executing it.
        // Returns true if the file has no syntax errors.
        static bool check_syntax(const std::string& path);

        void bind_runtime_handles(Runtime& runtime) {
            bound_runtime_ = &runtime;
        }

        Core& bound_core() const;

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
        const std::vector<MonitorAlias>& get_monitor_aliases() const { return monitor_aliases; }
        const MonitorCompose&            get_monitor_compose() const { return monitor_compose; }
        const std::vector<WorkspaceDef>& get_workspace_defs()  const { return workspace_defs; }

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

        void add_mouse_binding(MouseBinding mb) { mouse_bindings.push_back(std::move(mb)); }
        const std::vector<MouseBinding>& get_mouse_bindings() const { return mouse_bindings; }

        void              set_theme(ThemeConfig t)  { theme_ = std::move(t); }
        const ThemeConfig& get_theme()         const { return theme_; }

        Snapshot snapshot() const {
            Snapshot out;
            out.monitor_aliases   = monitor_aliases;
            out.monitor_compose   = monitor_compose;
            out.workspace_defs    = workspace_defs;
            out.bar_config        = bar_config;
            out.bottom_bar_config = bottom_bar_config;
            out.mod_mask          = mod_mask;
            out.mouse_bindings    = mouse_bindings;
            out.theme             = theme_;
            return out;
        }

        void restore(const Snapshot& snap) {
            monitor_aliases   = snap.monitor_aliases;
            monitor_compose   = snap.monitor_compose;
            workspace_defs    = snap.workspace_defs;
            bar_config        = snap.bar_config;
            bottom_bar_config = snap.bottom_bar_config;
            mod_mask          = snap.mod_mask;
            mouse_bindings    = snap.mouse_bindings;
            theme_            = snap.theme;
        }

        LuaHost& lua() { return lua_host; }
        const LuaHost& lua() const { return lua_host; }

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
            bar_config.reset();
            bottom_bar_config.reset();
            mod_mask.reset();

            mouse_bindings.clear();
            theme_ = {};
        }
};
