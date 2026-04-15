#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <vector>

#include <backend/events.hpp>
#include <domain/monitor.hpp>
#include <support/vec.hpp>

namespace layout {

struct Config {
    float master_factor = 0.55f;
    int   nmaster       = 1;
    int   gap           = 4;
    int   border        = 0; // set from theme at reconcile time
};

using PlacementSink = std::function<void (WindowId, Vec2i pos, Vec2i size, uint32_t border)>;

inline void tile(const std::vector<WindowId>& windows, const Monitor& mon, const Config& cfg,
    const PlacementSink& place) {
    int n = (int)windows.size();
    if (n == 0)
        return;

    int b  = cfg.border;
    int g  = cfg.gap;
    int mx = mon.pos().x() + g;
    int my = mon.pos().y() + g;
    int mw = mon.size().x() - g * 2;
    int mh = mon.size().y() - g * 2;

    int nm = std::min(cfg.nmaster, n);      // actual master count

    // All windows fit in master column (no stack)
    if (n <= nm) {
        int unit = (mh - g * (n - 1)) / n;
        for (int i = 0; i < n; i++) {
            // Last window absorbs rounding remainder
            int h = (i == n - 1) ? mh - i * (unit + g) : unit;
            place(windows[i],
                { mx, my + i * (unit + g) },
                { std::max(1, mw - b * 2), std::max(1, h - b * 2) },
                (uint32_t)std::max(0, b));
        }
        return;
    }

    int master_w = (int)(mw * cfg.master_factor) - g / 2;
    int stack_x  = mx + master_w + g;
    int stack_w  = mw - master_w - g;

    // Master column
    int munit = (mh - g * (nm - 1)) / nm;
    for (int i = 0; i < nm; i++) {
        int h = (i == nm - 1) ? mh - i * (munit + g) : munit;
        place(windows[i],
            { mx, my + i * (munit + g) },
            { std::max(1, master_w - b * 2), std::max(1, h - b * 2) },
            (uint32_t)std::max(0, b));
    }

    // Stack column
    int sn    = n - nm;
    int sunit = (mh - g * (sn - 1)) / sn;
    for (int i = nm; i < n; i++) {
        int si = i - nm;
        int h  = (si == sn - 1) ? mh - si * (sunit + g) : sunit;
        place(windows[i],
            { stack_x, my + si * (sunit + g) },
            { std::max(1, stack_w - b * 2), std::max(1, h - b * 2) },
            (uint32_t)std::max(0, b));
    }
}

inline void monocle(const std::vector<WindowId>& windows, const Monitor& mon, const Config& cfg,
    const PlacementSink& place) {
    int b = cfg.border;
    for (auto w : windows)
        place(w,
            { mon.pos().x() + b, mon.pos().y() + b },
            { std::max(1, mon.size().x() - b * 2), std::max(1, mon.size().y() - b * 2) },
            (uint32_t)std::max(0, b));
}

}
