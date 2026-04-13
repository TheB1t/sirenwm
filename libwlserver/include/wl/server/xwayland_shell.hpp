#pragma once

#include <wl/server/compositor.hpp>
#include <wl/server/surface_id.hpp>

#include <cstdint>
#include <unordered_map>

extern "C" {
#include <wayland-server-core.h>
}

struct PendingXSurface {
    wl_resource* resource    = nullptr;
    wl_resource* wl_surface  = nullptr;
    uint64_t     serial      = 0;
    bool         committed   = false;
};

class XWaylandShell {
public:
    XWaylandShell(wl_display* display, wl_client* allowed_client,
                  wl::server::Compositor& compositor);
    ~XWaylandShell();

    XWaylandShell(const XWaylandShell&) = delete;
    XWaylandShell& operator=(const XWaylandShell&) = delete;

    wl_resource* surface_from_serial(uint64_t serial) const;
    wl::server::Compositor& compositor() { return compositor_; }

private:
    wl_global*  global_ = nullptr;
    wl_client*  allowed_client_ = nullptr;
    wl::server::Compositor& compositor_;

    std::unordered_map<wl_resource*, PendingXSurface> surfaces_;

    static void bind(wl_client* client, void* data, uint32_t version, uint32_t id);
    static const void* shell_vtable();
    static const void* surface_vtable();
};
