#include <wl_backend_obj.hpp>
#include <log.hpp>

WlBackendObj::WlBackendObj(wl_event_loop* ev_loop) {
    backend_ = wlr_backend_autocreate(ev_loop, nullptr);
    if (!backend_)
        LOG_ERR("WlBackendObj: wlr_backend_autocreate failed");
}

WlBackendObj::~WlBackendObj() {
    if (backend_)
        wlr_backend_destroy(backend_);
}

bool WlBackendObj::start() {
    if (!wlr_backend_start(backend_)) {
        LOG_ERR("WlBackendObj: wlr_backend_start failed");
        return false;
    }
    return true;
}
