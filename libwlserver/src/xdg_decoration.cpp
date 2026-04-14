#include <wl/server/xdg_decoration.hpp>

extern "C" {
#include "xdg-decoration-protocol.h"
}

namespace wl::server {

static void deco_mgr_get_toplevel_decoration(wl_client* client, wl_resource*,
    uint32_t id, wl_resource*) {
    static const struct zxdg_toplevel_decoration_v1_interface deco_impl = {
        .destroy = [](wl_client*, wl_resource* r) {
                wl_resource_destroy(r);
            },
        .set_mode = [](wl_client*, wl_resource*, uint32_t) {
            },
        .unset_mode = [](wl_client*, wl_resource*) {
            },
    };

    auto* deco = wl_resource_create(client, &zxdg_toplevel_decoration_v1_interface, 1, id);
    if (!deco) {
        wl_client_post_no_memory(client); return;
    }
    wl_resource_set_implementation(deco, &deco_impl, nullptr, nullptr);
    zxdg_toplevel_decoration_v1_send_configure(deco,
        ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

XdgDecoration::XdgDecoration(Display& display)
    : global_(display, this) {}

const wl_interface* XdgDecoration::interface() {
    return &zxdg_decoration_manager_v1_interface;
}

void XdgDecoration::bind(wl_client* client, uint32_t version, uint32_t id) {
    static const struct zxdg_decoration_manager_v1_interface mgr_impl = {
        .destroy = [](wl_client*, wl_resource* r) {
                wl_resource_destroy(r);
            },
        .get_toplevel_decoration = deco_mgr_get_toplevel_decoration,
    };

    auto* r = wl_resource_create(client, &zxdg_decoration_manager_v1_interface,
            static_cast<int>(version), id);
    if (!r) {
        wl_client_post_no_memory(client); return;
    }
    wl_resource_set_implementation(r, &mgr_impl, nullptr, nullptr);
}

} // namespace wl::server
