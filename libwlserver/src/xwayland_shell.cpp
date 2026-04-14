#include <wl/server/xwayland_shell.hpp>

#include <cstdio>

extern "C" {
#include "xwayland-shell-v1-protocol.h"
}

const void* XWaylandShell::surface_vtable() {
    static const struct xwayland_surface_v1_interface vtable = {
        .set_serial = [](wl_client*, wl_resource* r,
            uint32_t serial_lo, uint32_t serial_hi) {
                auto* shell = static_cast<XWaylandShell*>(wl_resource_get_user_data(r));
                auto it     = shell->surfaces_.find(r);
                if (it != shell->surfaces_.end())
                    it->second.serial = (static_cast<uint64_t>(serial_hi) << 32) | serial_lo;
            },
        .destroy = [](wl_client*, wl_resource* r) {
                auto* shell = static_cast<XWaylandShell*>(wl_resource_get_user_data(r));
                shell->surfaces_.erase(r);
                wl_resource_destroy(r);
            },
    };
    return &vtable;
}

const void* XWaylandShell::shell_vtable() {
    static const struct xwayland_shell_v1_interface vtable = {
        .destroy = [](wl_client*, wl_resource* r) {
                wl_resource_destroy(r);
            },
        .get_xwayland_surface = [](wl_client* client, wl_resource* r,
            uint32_t id, wl_resource* surface) {
                auto* shell     = static_cast<XWaylandShell*>(wl_resource_get_user_data(r));
                auto* xsurf_res = wl_resource_create(client,
                        &xwayland_surface_v1_interface, 1, id);
                if (!xsurf_res) {
                    wl_client_post_no_memory(client); return;
                }

                PendingXSurface pending;
                pending.resource            = xsurf_res;
                pending.wl_surface          = surface;
                shell->surfaces_[xsurf_res] = pending;

                wl_resource_set_implementation(xsurf_res, surface_vtable(), shell,
                    [](wl_resource* r) {
                        auto* s = static_cast<XWaylandShell*>(wl_resource_get_user_data(r));
                        s->surfaces_.erase(r);
                    });
            },
    };
    return &vtable;
}

void XWaylandShell::bind(wl_client* client, void* data,
    uint32_t version, uint32_t id) {
    auto* self = static_cast<XWaylandShell*>(data);
    if (client != self->allowed_client_) {
        fprintf(stderr, "xwayland-shell: rejecting non-Xwayland client\n");
        wl_client_post_implementation_error(client,
            "xwayland_shell_v1 is restricted to Xwayland");
        return;
    }

    auto* resource = wl_resource_create(client, &xwayland_shell_v1_interface,
            static_cast<int>(version), id);
    if (!resource) {
        wl_client_post_no_memory(client); return;
    }
    wl_resource_set_implementation(resource, shell_vtable(), self, nullptr);
}

XWaylandShell::XWaylandShell(wl_display* display, wl_client* allowed_client,
    wl::server::Compositor& compositor)
    : allowed_client_(allowed_client), compositor_(compositor) {
    global_ = wl_global_create(display, &xwayland_shell_v1_interface,
            1, this, &XWaylandShell::bind);
}

XWaylandShell::~XWaylandShell() {
    if (global_) wl_global_destroy(global_);
}

wl_resource* XWaylandShell::surface_from_serial(uint64_t serial) const {
    if (serial == 0) return nullptr;
    for (auto& [_, s] : surfaces_) {
        if (s.serial == serial)
            return s.wl_surface;
    }
    return nullptr;
}
