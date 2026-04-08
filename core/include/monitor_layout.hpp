#pragma once

#include <vector>
#include <backend/monitor_port.hpp>

class Config;

namespace monitor_layout {

// Build a MonitorLayout vector from user-configured aliases and compose graph.
// Returns an empty vector when no aliases are configured (backend defaults).
std::vector<backend::MonitorLayout> build(const Config& config);

} // namespace monitor_layout
