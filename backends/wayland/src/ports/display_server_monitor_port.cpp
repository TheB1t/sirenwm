#include <display_server_ports.hpp>
#include <display_server_backend.hpp>

namespace backend {

std::vector<Monitor> DisplayServerMonitorPort::get_monitors() {
    std::vector<Monitor> result;
    int                  idx = 0;
    for (auto& [id, info] : backend_.outputs()) {
        result.emplace_back(idx++, info.name, info.x, info.y, info.w, info.h);
    }
    return result;
}

bool DisplayServerMonitorPort::apply_monitor_layout(const std::vector<MonitorLayout>&) {
    return false;
}

void DisplayServerMonitorPort::select_change_events() {}

void DisplayServerMonitorPort::flush() {
}

} // namespace backend
