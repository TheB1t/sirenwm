#include <wl/server/xdg_shell.hpp>

#include <cstdio>

extern "C" {
#include "xdg-shell-protocol.h"
}

namespace wl::server {

// ── vtables ──

const void* XdgShell::toplevel_vtable() {
    static const struct xdg_toplevel_interface vtable = {
        .destroy = [](wl_client*, wl_resource* r) {
                wl_resource_destroy(r);
            },
        .set_parent = [](wl_client*, wl_resource*, wl_resource*) {
            },
        .set_title = [](wl_client*, wl_resource* r, const char* title) {
                auto* shell = static_cast<XdgShell*>(wl_resource_get_user_data(r));
                auto it     = shell->toplevels_.find(r);
                if (it != shell->toplevels_.end()) {
                    it->second.state.title = title ? title : "";
                    if (shell->listener_)
                        shell->listener_->on_title_changed(it->second.state.id, it->second.state.title);
                }
            },
        .set_app_id = [](wl_client*, wl_resource* r, const char* app_id) {
                auto* shell = static_cast<XdgShell*>(wl_resource_get_user_data(r));
                auto it     = shell->toplevels_.find(r);
                if (it != shell->toplevels_.end()) {
                    it->second.state.app_id = app_id ? app_id : "";
                    if (shell->listener_)
                        shell->listener_->on_app_id_changed(it->second.state.id, it->second.state.app_id);
                }
            },
        .show_window_menu = [](wl_client*, wl_resource*, wl_resource*, uint32_t, int32_t, int32_t) {
            },
        .move = [](wl_client*, wl_resource*, wl_resource*, uint32_t) {
            },
        .resize = [](wl_client*, wl_resource*, wl_resource*, uint32_t, uint32_t) {
            },
        .set_max_size = [](wl_client*, wl_resource* r, int32_t w, int32_t h) {
                auto* shell = static_cast<XdgShell*>(wl_resource_get_user_data(r));
                auto it     = shell->toplevels_.find(r);
                if (it != shell->toplevels_.end() && shell->listener_)
                    shell->listener_->on_max_size_changed(it->second.state.id, w, h);
            },
        .set_min_size = [](wl_client*, wl_resource* r, int32_t w, int32_t h) {
                auto* shell = static_cast<XdgShell*>(wl_resource_get_user_data(r));
                auto it     = shell->toplevels_.find(r);
                if (it != shell->toplevels_.end() && shell->listener_)
                    shell->listener_->on_min_size_changed(it->second.state.id, w, h);
            },
        .set_maximized = [](wl_client*, wl_resource*) {
            },
        .unset_maximized = [](wl_client*, wl_resource*) {
            },
        .set_fullscreen = [](wl_client*, wl_resource* r, wl_resource*) {
                auto* shell = static_cast<XdgShell*>(wl_resource_get_user_data(r));
                auto it     = shell->toplevels_.find(r);
                if (it != shell->toplevels_.end() && shell->listener_)
                    shell->listener_->on_fullscreen_requested(it->second.state.id, true);
            },
        .unset_fullscreen = [](wl_client*, wl_resource* r) {
                auto* shell = static_cast<XdgShell*>(wl_resource_get_user_data(r));
                auto it     = shell->toplevels_.find(r);
                if (it != shell->toplevels_.end() && shell->listener_)
                    shell->listener_->on_fullscreen_requested(it->second.state.id, false);
            },
        .set_minimized = [](wl_client*, wl_resource*) {
            },
    };
    return &vtable;
}

const void* XdgShell::xdg_surface_vtable() {
    static const struct xdg_surface_interface vtable = {
        .destroy = [](wl_client*, wl_resource* r) {
                wl_resource_destroy(r);
            },
        .get_toplevel = [](wl_client* client, wl_resource* resource, uint32_t id) {
                auto* shell = static_cast<XdgShell*>(wl_resource_get_user_data(resource));
                if (!shell) {
                    wl_client_post_implementation_error(client, "invalid xdg_surface state");
                    return;
                }
                auto surf_it = shell->xdg_surface_to_surface_.find(resource);
                if (surf_it == shell->xdg_surface_to_surface_.end() || !surf_it->second) {
                    wl_client_post_implementation_error(client, "missing wl_surface for xdg_surface");
                    return;
                }

                int version = wl_resource_get_version(resource);

                auto* tl_res = wl_resource_create(client, &xdg_toplevel_interface, version, id);
                if (!tl_res) {
                    wl_client_post_no_memory(client); return;
                }

                uint32_t pid = 0;
                { pid_t cpid = 0; wl_client_get_credentials(client, &cpid, nullptr, nullptr); pid = static_cast<uint32_t>(cpid); }

                InternalToplevel tl;
                tl.xdg_surface_res  = resource;
                tl.toplevel_res     = tl_res;
                tl.state.id         = shell->next_toplevel_id_++;
                tl.state.surface_id = shell->compositor_.id_from_resource(surf_it->second);

                shell->toplevels_[tl_res]         = tl;
                shell->xdg_to_toplevel_[resource] = tl_res;

                wl_resource_set_implementation(tl_res, toplevel_vtable(), shell,
                    &XdgShell::toplevel_resource_destroy);

                wl_array states;
                wl_array_init(&states);
                xdg_toplevel_send_configure(tl_res, 0, 0, &states);
                wl_array_release(&states);

                static uint32_t serial = 1;
                xdg_surface_send_configure(resource, serial++);

                if (shell->listener_)
                    shell->listener_->on_toplevel_created(tl.state.id, tl.state.surface_id, pid);
            },
        .get_popup = [](wl_client*, wl_resource*, uint32_t, wl_resource*, wl_resource*) {
            },
        .set_window_geometry = [](wl_client*, wl_resource*, int32_t, int32_t, int32_t, int32_t) {
            },
        .ack_configure = [](wl_client*, wl_resource*, uint32_t) {
            },
    };
    return &vtable;
}

const void* XdgShell::wm_base_vtable() {
    static const struct xdg_wm_base_interface vtable = {
        .destroy = [](wl_client*, wl_resource* r) {
                wl_resource_destroy(r);
            },
        .create_positioner = [](wl_client*, wl_resource*, uint32_t) {
            },
        .get_xdg_surface = [](wl_client* client, wl_resource* resource,
            uint32_t id, wl_resource* surface) {
                auto* shell = static_cast<XdgShell*>(wl_resource_get_user_data(resource));
                int version = wl_resource_get_version(resource);

                auto* xdg_res = wl_resource_create(client, &xdg_surface_interface, version, id);
                if (!xdg_res) {
                    wl_client_post_no_memory(client); return;
                }

                shell->xdg_surface_to_surface_[xdg_res] = surface;
                wl_resource_set_implementation(xdg_res, xdg_surface_vtable(), shell,
                    [](wl_resource* r) {
                        auto* shell = static_cast<XdgShell*>(wl_resource_get_user_data(r));
                        if (!shell) return;
                        shell->xdg_surface_to_surface_.erase(r);
                        shell->on_xdg_surface_destroyed(r);
                    });
            },
        .pong = [](wl_client*, wl_resource*, uint32_t) {
            },
    };
    return &vtable;
}

// ── XdgShell ──

XdgShell::XdgShell(Display& display, Compositor& compositor)
    : global_(display, this)
      , compositor_(compositor)
      , surface_commit_subscription_(
          compositor_.subscribe_surface_commit([this](SurfaceId sid) {
              on_surface_commit(sid);
          })) {}

const wl_interface* XdgShell::interface() {
    return &xdg_wm_base_interface;
}

void XdgShell::bind(wl_client* client, uint32_t version, uint32_t id) {
    auto* resource = wl_resource_create(client, &xdg_wm_base_interface,
            static_cast<int>(version), id);
    if (!resource) {
        wl_client_post_no_memory(client); return;
    }
    wl_resource_set_implementation(resource, wm_base_vtable(), this, nullptr);
}

const ToplevelState* XdgShell::toplevel(uint32_t id) const {
    for (auto& [_, tl] : toplevels_)
        if (tl.state.id == id) return &tl.state;
    return nullptr;
}

const ToplevelState* XdgShell::toplevel_by_surface(SurfaceId sid) const {
    for (auto& [_, tl] : toplevels_)
        if (tl.state.surface_id == sid) return &tl.state;
    return nullptr;
}

XdgShell::InternalToplevel* XdgShell::find_by_id(uint32_t id) {
    for (auto& [_, tl] : toplevels_)
        if (tl.state.id == id) return &tl;
    return nullptr;
}

XdgShell::InternalToplevel* XdgShell::find_by_surface(SurfaceId sid) {
    for (auto& [_, tl] : toplevels_)
        if (tl.state.surface_id == sid) return &tl;
    return nullptr;
}

void XdgShell::configure_toplevel(uint32_t id, int32_t w, int32_t h) {
    auto* tl = find_by_id(id);
    if (!tl || !tl->toplevel_res) return;
    if (!tl->xdg_surface_res) return;

    wl_resource* tl_res = tl->toplevel_res;
    wl_resource* xs_res = tl->xdg_surface_res;
    if (!wl_resource_get_client(tl_res) || !wl_resource_get_client(xs_res))
        return;

    wl_array  states;
    wl_array_init(&states);
    uint32_t* s;
    s = static_cast<uint32_t*>(wl_array_add(&states, sizeof(uint32_t)));
    if (!s) {
        wl_array_release(&states);
        return;
    }
    *s = XDG_TOPLEVEL_STATE_TILED_LEFT;
    s  = static_cast<uint32_t*>(wl_array_add(&states, sizeof(uint32_t)));
    if (!s) {
        wl_array_release(&states);
        return;
    }
    *s = XDG_TOPLEVEL_STATE_TILED_RIGHT;
    s  = static_cast<uint32_t*>(wl_array_add(&states, sizeof(uint32_t)));
    if (!s) {
        wl_array_release(&states);
        return;
    }
    *s = XDG_TOPLEVEL_STATE_TILED_TOP;
    s  = static_cast<uint32_t*>(wl_array_add(&states, sizeof(uint32_t)));
    if (!s) {
        wl_array_release(&states);
        return;
    }
    *s = XDG_TOPLEVEL_STATE_TILED_BOTTOM;
    xdg_toplevel_send_configure(tl_res, w, h, &states);
    wl_array_release(&states);

    if (toplevels_.find(tl_res) == toplevels_.end())
        return;
    if (xdg_to_toplevel_.find(xs_res) == xdg_to_toplevel_.end())
        return;

    static uint32_t serial_counter = 1;
    xdg_surface_send_configure(xs_res, serial_counter++);
}

void XdgShell::close_toplevel(uint32_t id) {
    auto* tl = find_by_id(id);
    if (!tl || !tl->toplevel_res) return;
    if (!wl_resource_get_client(tl->toplevel_res)) return;
    xdg_toplevel_send_close(tl->toplevel_res);
}

void XdgShell::toplevel_resource_destroy(wl_resource* resource) {
    auto* shell = static_cast<XdgShell*>(wl_resource_get_user_data(resource));
    if (!shell) return;
    auto  it = shell->toplevels_.find(resource);
    if (it != shell->toplevels_.end()) {
        if (it->second.xdg_surface_res)
            shell->xdg_to_toplevel_.erase(it->second.xdg_surface_res);
        uint32_t tid = it->second.state.id;
        shell->toplevels_.erase(it);
        if (shell->listener_)
            shell->listener_->on_toplevel_destroyed(tid);
    }
}

void XdgShell::on_xdg_surface_destroyed(wl_resource* xdg_surface) {
    auto map_it = xdg_to_toplevel_.find(xdg_surface);
    if (map_it == xdg_to_toplevel_.end())
        return;

    wl_resource* tl_res = map_it->second;
    xdg_to_toplevel_.erase(map_it);

    auto tl_it = toplevels_.find(tl_res);
    if (tl_it == toplevels_.end())
        return;

    uint32_t tid = tl_it->second.state.id;
    tl_it->second.xdg_surface_res = nullptr;
    tl_it->second.toplevel_res    = nullptr;
    toplevels_.erase(tl_it);
    if (listener_)
        listener_->on_toplevel_destroyed(tid);
}

void XdgShell::on_surface_commit(SurfaceId sid) {
    auto* tl = find_by_surface(sid);
    if (!tl) return;

    auto* info = compositor_.surface_info(sid);
    if (!info) return;

    if (!tl->state.mapped && info->has_commit && info->has_buffer) {
        tl->state.mapped = true;
        if (listener_)
            listener_->on_toplevel_mapped(tl->state.id, info->buf_width, info->buf_height);
    }

    if (info->has_buffer && listener_)
        listener_->on_toplevel_committed(tl->state.id, info->buf_width, info->buf_height);
}

} // namespace wl::server
