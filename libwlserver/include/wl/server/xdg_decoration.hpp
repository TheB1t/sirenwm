#pragma once

#include <wl/display.hpp>
#include <wl/global.hpp>

extern "C" {
#include <wayland-server-core.h>
}

namespace wl::server {

class XdgDecoration {
public:
    explicit XdgDecoration(Display& display);

    XdgDecoration(const XdgDecoration&)            = delete;
    XdgDecoration& operator=(const XdgDecoration&) = delete;

    static const wl_interface* interface();
    static int version() { return 1; }
    void bind(wl_client* client, uint32_t version, uint32_t id);

private:
    wl::Global<XdgDecoration> global_;
};

} // namespace wl::server
