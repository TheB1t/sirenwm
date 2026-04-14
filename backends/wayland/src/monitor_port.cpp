#include <wl_ports.hpp>
#include <wl_backend.hpp>

namespace backend {

std::vector<Monitor> WlMonitorPort::get_monitors() {
    std::vector<Monitor> result;
    int                  idx = 0;
    for (auto& [id, info] : backend_.outputs()) {
        result.emplace_back(idx++, info.name, info.x, info.y, info.w, info.h);
    }
    return result;
}

bool WlMonitorPort::apply_monitor_layout(const std::vector<MonitorLayout>&) {
    return false;
}

void WlMonitorPort::select_change_events() {}

void WlMonitorPort::flush() {
    if (backend_.display())
        backend_.display().flush();
}

} // namespace backend
