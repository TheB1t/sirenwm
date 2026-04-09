#include <config.hpp>
#include <config_loader.hpp>
#include <core.hpp>
#include <runtime.hpp>
#include <log.hpp>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include <string_utils.hpp>
#include <unordered_set>
#include <unordered_map>
#include <functional>

// ---------------------------------------------------------------------------
// Config::validate()
// ---------------------------------------------------------------------------

std::vector<std::string> Config::validate() const {
    std::vector<std::string> errs;

    auto                     has_alias = [&](const std::string& alias) {
            for (auto& m : monitor_aliases)
                if (m.alias == alias)
                    return true;
            return false;
        };

    if (!has_mod_mask())
        errs.push_back("modifier not set — use siren.modifier = 'mod4' (or shift/ctrl/alt/mod1..mod5)");

    if (workspace_defs.empty())
        errs.push_back("no workspaces defined — set siren.workspaces = {...}");

    if (monitor_aliases.empty())
        errs.push_back("no monitors defined — set siren.monitors = {...}");
    else {
        std::unordered_set<std::string> aliases_seen;
        std::unordered_set<std::string> outputs_seen;
        for (auto& m : monitor_aliases) {
            if (m.alias.empty())
                errs.push_back("monitor: 'name' must be non-empty");
            if (m.output.empty())
                errs.push_back("monitor '" + m.alias + "': 'output' must be non-empty");
            if (m.width <= 0 || m.height <= 0)
                errs.push_back("monitor '" + m.alias + "': width/height must be > 0");
            if (!is_valid_rotation(m.rotation))
                errs.push_back("monitor '" + m.alias + "': rotation must be one of normal/left/right/inverted");
            if (!aliases_seen.insert(m.alias).second)
                errs.push_back("duplicate monitor alias '" + m.alias + "'");
            if (!outputs_seen.insert(m.output).second)
                errs.push_back("duplicate output mapping '" + m.output + "'");
        }
    }

    if (!monitor_compose.defined) {
        errs.push_back("monitor composition not defined — set siren.compose_monitors = {...}");
    } else {
        auto valid_side = [](const std::string& s) {
                return s == "left" || s == "right" || s == "top" || s == "bottom";
            };
        if (monitor_compose.primary.empty())
            errs.push_back("compose_monitors: 'primary' must be set");

        std::unordered_map<std::string, const MonitorComposeLink*> by_monitor;
        for (auto& l : monitor_compose.layout) {
            if (l.monitor.empty())
                errs.push_back("compose_monitors.layout: 'monitor' must be non-empty");
            if (!has_alias(l.monitor))
                errs.push_back("compose_monitors.layout: unknown monitor alias '" + l.monitor + "'");
            if (by_monitor.count(l.monitor))
                errs.push_back("compose_monitors.layout: duplicate entry for '" + l.monitor + "'");
            else
                by_monitor[l.monitor] = &l;
            if (!l.side.empty() && !valid_side(l.side))
                errs.push_back("compose_monitors.layout: monitor '" + l.monitor +
                    "' has invalid side '" + l.side + "'");
        }

        if (!monitor_compose.primary.empty() && !has_alias(monitor_compose.primary))
            errs.push_back("compose_monitors: primary alias '" + monitor_compose.primary +
                "' not found in siren.monitors");

        for (auto& m : monitor_aliases) {
            if (!m.enabled)
                continue;
            if (m.alias == monitor_compose.primary)
                continue; // primary may be implicit at (0,0), no layout entry required
            if (!by_monitor.count(m.alias))
                errs.push_back("compose_monitors.layout: missing entry for enabled monitor '" + m.alias + "'");
        }

        std::unordered_map<std::string, int>    state;
        std::function<void(const std::string&)> dfs = [&](const std::string& name) {
                if (state[name] == 2)
                    return;
                if (state[name] == 1) {
                    errs.push_back("compose_monitors: cycle detected at '" + name + "'");
                    return;
                }
                state[name] = 1;
                auto it = by_monitor.find(name);
                if (it == by_monitor.end()) {
                    if (name == monitor_compose.primary) {
                        state[name] = 2;
                        return; // implicit primary at origin
                    }
                    errs.push_back("compose_monitors: missing layout entry for '" + name + "'");
                    state[name] = 2;
                    return;
                }
                auto* link = it->second;
                if (name == monitor_compose.primary) {
                    if (!link->relative_to.empty())
                        errs.push_back("compose_monitors: primary monitor '" + name + "' must not have relative_to");
                    if (!link->side.empty())
                        errs.push_back("compose_monitors: primary monitor '" + name + "' must not have side");
                } else {
                    if (link->relative_to.empty())
                        errs.push_back("compose_monitors: monitor '" + name + "' must set relative_to");
                    if (link->side.empty())
                        errs.push_back("compose_monitors: monitor '" + name + "' must set side");
                }
                if (!link->relative_to.empty()) {
                    if (!has_alias(link->relative_to)) {
                        errs.push_back("compose_monitors: monitor '" + name + "' references unknown relative_to '" +
                            link->relative_to + "'");
                    } else if (link->relative_to == name) {
                        errs.push_back("compose_monitors: monitor '" + name + "' cannot reference itself");
                    } else {
                        dfs(link->relative_to);
                    }
                }
                state[name] = 2;
            };

        for (auto& m : monitor_aliases) {
            if (m.enabled)
                dfs(m.alias);
        }
    }

    for (auto& ws : workspace_defs) {
        if (ws.monitor.empty())
            continue;
        if (!has_alias(ws.monitor))
            errs.push_back("workspace '" + ws.name + "': unknown monitor alias '" + ws.monitor + "'");
    }

    if (bar_config.has_value()) {
        if (bar_config->font.empty() && theme_.font.empty())
            errs.push_back("bar: 'font' is required (set in bar.top.font or theme.font)");
    }

    if (bar_config.has_value() || bottom_bar_config.has_value()) {
        if (theme_.bg.empty())     errs.push_back("theme: 'bg' is required when bar is configured");
        if (theme_.fg.empty())     errs.push_back("theme: 'fg' is required when bar is configured");
        if (theme_.alt_bg.empty()) errs.push_back("theme: 'alt_bg' is required when bar is configured");
        if (theme_.alt_fg.empty()) errs.push_back("theme: 'alt_fg' is required when bar is configured");
        if (theme_.accent.empty()) errs.push_back("theme: 'accent' is required when bar is configured");
    }

    return errs;
}

// ---------------------------------------------------------------------------
// Config::load()
// ---------------------------------------------------------------------------

Core& Config::bound_core() const {
    return bound_runtime().core();
}

bool Config::load(const std::string& path, Runtime& runtime, bool reset_lua_vm) {
    return config_loader::load(*this, path, runtime.core(), runtime, runtime.lua(), reset_lua_vm);
}

bool Config::check_syntax(const std::string& path) {
    lua_State* L = luaL_newstate();
    if (!L)
        return false;
    int rc = luaL_loadfile(L, path.c_str());
    if (rc != LUA_OK) {
        const char* err = lua_tostring(L, -1);
        LOG_ERR("config syntax check failed: %s", err ? err : "<unknown>");
    }
    lua_close(L);
    return rc == LUA_OK;
}
