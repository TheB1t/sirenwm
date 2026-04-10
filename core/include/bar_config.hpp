#pragma once

#include <string>
#include <vector>
#include <optional>
#include <unordered_map>

#include <lua_host.hpp>

enum class BarPosition { TOP, BOTTOM };

struct BarColors {
    std::string normal_bg;
    std::string normal_fg;
    std::string focused_bg;
    std::string focused_fg;
    std::string bar_bg;
    std::string status_fg;
};

enum class BarSlotKind { Tags, Title, Tray, Lua };

// A slot in a bar zone.
//   kind == Tags/Title/Tray  → built-in widget, no widget ref
//   kind == Lua              → Widget object with render() method and optional interval field
struct BarSlot {
    BarSlotKind    kind = BarSlotKind::Lua;
    LuaRegistryRef widget;               // ref to Widget object when kind == Lua
    int            interval = 1;         // 0 = every redraw; >0 = every N seconds

    // Runtime state (not part of config, mutated during redraw).
    mutable std::string cached_text;
    mutable int         ticks = 0;  // set to interval after parse so first tick renders
};

struct BarConfig {
    std::string          font;
    int                  height = 0;     // 0 = auto from font metrics
    BarColors            colors;
    BarPosition          position = BarPosition::TOP;
    std::vector<BarSlot> left;
    std::vector<BarSlot> center;
    std::vector<BarSlot> right;
};

// Per-monitor bar pair.
//
// Each side uses optional<optional<BarConfig>>:
//   nullopt              — key absent in Lua  → remove this bar entirely
//   some(nullopt)        — key = {}           → inherit from default config
//   some(some(cfg))      — key = { ... }      → use this custom config
struct MonitorBarConfig {
    std::optional<std::optional<BarConfig>> top;
    std::optional<std::optional<BarConfig>> bottom;
};

// Full bar settings: a default config and per-monitor alias overrides.
struct BarSetConfig {
    MonitorBarConfig                                   default_cfg;
    std::unordered_map<std::string, MonitorBarConfig>  per_monitor; // key = monitor alias

    // Resolve the effective top/bottom config for a given monitor alias.
    // Per-monitor entry takes precedence:
    //   absent key      → remove bar
    //   {} (nullopt)    → use default
    //   { ... }         → use custom config
    // Falls back to default_cfg when no per-monitor entry exists.
    MonitorBarConfig resolve(const std::string& alias) const {
        auto it = per_monitor.find(alias);
        if (it == per_monitor.end())
            return default_cfg;

        const MonitorBarConfig& ov = it->second;
        MonitorBarConfig result;

        // Top side
        if (!ov.top.has_value()) {
            result.top = std::nullopt;              // explicitly absent → remove
        } else if (!ov.top->has_value()) {
            result.top = default_cfg.top;           // {} → inherit default
        } else {
            result.top = ov.top;                    // custom config
        }

        // Bottom side
        if (!ov.bottom.has_value()) {
            result.bottom = std::nullopt;           // explicitly absent → remove
        } else if (!ov.bottom->has_value()) {
            result.bottom = default_cfg.bottom;     // {} → inherit default
        } else {
            result.bottom = ov.bottom;              // custom config
        }

        return result;
    }
};
