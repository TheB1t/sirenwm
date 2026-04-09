#pragma once

#include <vector>
#include <backend/monitor_port.hpp>
#include <config_types.hpp>

namespace monitor_layout {

// Build a MonitorLayout vector from user-configured aliases and compose graph.
// Returns an empty vector when no aliases are configured (backend defaults).
std::vector<backend::MonitorLayout> build(
    const std::vector<MonitorAlias>& aliases,
    const MonitorCompose& compose);

} // namespace monitor_layout
