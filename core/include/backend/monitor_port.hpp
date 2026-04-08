#pragma once

#include <string>
#include <vector>

#include <monitor.hpp>
#include <vec.hpp>

namespace backend {

// Platform-agnostic monitor layout description.
// Fully resolved by the feature layer (compose graph -> absolute positions).
// Backend applies this using platform-specific API.
struct MonitorLayout {
    std::string output;    // physical output name (e.g. "eDP-1")
    std::string alias;     // logical monitor name
    bool        enabled = true;
    Vec2i       pos;                  // absolute screen position
    Vec2i       size;
    int         refresh_rate = 0;
    std::string rotation;  // "normal", "left", "right", "inverted"
    bool        primary = false;      // whether this is the primary output
};

class MonitorPort {
    public:
        virtual ~MonitorPort() = default;

        // Query current physical monitors from the platform.
        virtual std::vector<Monitor> get_monitors() = 0;

        // Apply a fully resolved monitor layout.
        // The feature layer computes positions from the compose graph;
        // the backend translates this to platform API (RandR, wlr-output-management, etc).
        virtual bool apply_monitor_layout(const std::vector<MonitorLayout>& layout) = 0;

        // Start listening for display change events from the platform.
        // Backend internally calls Runtime::dispatch_display_change() when change is detected.
        virtual void select_change_events() = 0;

        virtual void flush() = 0;
};

} // namespace backend
