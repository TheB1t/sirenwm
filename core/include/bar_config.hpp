#pragma once

#include <string>
#include <vector>

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
