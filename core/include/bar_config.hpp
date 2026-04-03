#pragma once

#include <string>
#include <vector>

enum class BarPosition { TOP, BOTTOM };

struct BarColors {
    std::string normal_bg;
    std::string normal_fg;
    std::string focused_bg;
    std::string focused_fg;
    std::string bar_bg;
    std::string status_fg;
};

// A slot in a bar zone. Either a built-in name ("tags", "title", "status",
// "tray") or a custom Lua widget identified by name (callback registered
// separately via bar.widgets or inline in the zone list).
struct BarSlot {
    std::string name;
};

struct BarConfig {
    std::string          font;
    int                  height   = 0;      // 0 = auto from font metrics
    BarColors            colors;
    BarPosition          position = BarPosition::TOP;
    std::vector<BarSlot> left;
    std::vector<BarSlot> center;
    std::vector<BarSlot> right;
};