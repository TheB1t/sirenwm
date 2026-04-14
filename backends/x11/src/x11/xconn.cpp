#include <x11/xconn.hpp>

#include <domain/monitor.hpp>
#include <xcb/randr.hpp>
#include <support/log.hpp>

std::vector<Monitor> XConnection::get_monitors() const {
    xcb::Screen s(raw(), root_window());
    auto        raw = s.monitors();
    if (raw.empty()) {
        LOG_WARN("RandR get_monitors returned nothing, falling back to screen size");
        auto* screen = raw_screen();
        return {Monitor(0, "default", 0, 0,
                    screen->width_in_pixels, screen->height_in_pixels)};
    }

    std::vector<Monitor> result;
    result.reserve(raw.size());
    for (auto& m : raw)
        result.emplace_back(m.id, m.name, m.x, m.y, m.width, m.height);
    return result;
}
