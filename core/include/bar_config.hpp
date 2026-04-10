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

static constexpr int kBarDefaultHeight = 18;

struct BarConfig {
    std::string          font;
    int                  height = 0;     // 0 = auto → kBarDefaultHeight
    BarColors            colors;
    BarPosition          position = BarPosition::TOP;
    std::vector<BarSlot> left;
    std::vector<BarSlot> center;
    std::vector<BarSlot> right;
};

// Disposition of one bar side in a per-monitor override entry.
enum class BarSideState {
    Remove,   // key absent in Lua  → remove this bar entirely
    Inherit,  // key = {}           → inherit from default config
    Custom,   // key = { ... }      → use this custom config
};

// One side (top or bottom) of a per-monitor bar override.
struct BarSide {
    BarSideState state = BarSideState::Remove;
    BarConfig    cfg;   // valid only when state == Custom
};

// Per-monitor bar pair: top and bottom sides.
struct MonitorBarConfig {
    BarSide top;
    BarSide bottom;
};

// Full bar settings: a default config and per-monitor alias overrides.
struct BarSetConfig {
    MonitorBarConfig                                  default_cfg;
    std::unordered_map<std::string, MonitorBarConfig> per_monitor;  // key = monitor alias

    // Resolve the effective top/bottom config for a given monitor alias.
    // Per-monitor entry takes precedence; falls back to default_cfg when absent.
    MonitorBarConfig resolve(const std::string& alias) const {
        auto it = per_monitor.find(alias);
        if (it == per_monitor.end())
            return default_cfg;

        const MonitorBarConfig& ov = it->second;
        MonitorBarConfig        result;

        auto                    resolve_side = [](const BarSide& override_side,
            const BarSide& default_side) -> BarSide {
                switch (override_side.state) {
                    case BarSideState::Remove:  return { BarSideState::Remove, {} };
                    case BarSideState::Inherit: return default_side;
                    case BarSideState::Custom:  return override_side;
                }
                return { BarSideState::Remove, {} };
            };

        result.top    = resolve_side(ov.top,    default_cfg.top);
        result.bottom = resolve_side(ov.bottom, default_cfg.bottom);
        return result;
    }
};
