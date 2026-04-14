#include <wl/server/protocol/compositor.hpp>

#include <algorithm>

extern "C" {
#include <wayland-server-protocol.h>
}

namespace wl::server {

Compositor::SurfaceCommitSubscription::~SurfaceCommitSubscription() {
    reset();
}

Compositor::SurfaceCommitSubscription::SurfaceCommitSubscription(
    SurfaceCommitSubscription&& other) noexcept
    : owner_(other.owner_)
      , id_(other.id_) {
    other.owner_ = nullptr;
    other.id_    = 0;
}

Compositor::SurfaceCommitSubscription&
Compositor::SurfaceCommitSubscription::operator=(SurfaceCommitSubscription&& other) noexcept {
    if (this == &other)
        return *this;
    reset();
    owner_       = other.owner_;
    id_          = other.id_;
    other.owner_ = nullptr;
    other.id_    = 0;
    return *this;
}

void Compositor::SurfaceCommitSubscription::reset() noexcept {
    if (!owner_ || id_ == 0)
        return;
    owner_->unsubscribe_surface_commit(id_);
    owner_ = nullptr;
    id_    = 0;
}

// ── WlSurface methods ──

void Compositor::WlSurface::clear_buffer() {
    if (buffer) {
        wl_list_remove(&buffer_destroy_listener.link);
        wl_list_init(&buffer_destroy_listener.link);
        buffer = nullptr;
    }
}

void Compositor::WlSurface::set_buffer(wl_resource* buf) {
    clear_buffer();
    buffer = buf;
    if (buf) {
        buffer_destroy_listener.notify = [](wl_listener* listener, void*) {
                WlSurface* self;
                self         = wl_container_of(listener, self, buffer_destroy_listener);
                self->buffer = nullptr;
                wl_list_init(&self->buffer_destroy_listener.link);
            };
        wl_resource_add_destroy_listener(buf, &buffer_destroy_listener);
    }
}

// ── Compositor ──

const wl_interface* Compositor::interface() { return &wl_compositor_interface; }

Compositor::Compositor(Display& display, Shm& shm)
    : global_(display, this)
      , shm_(shm) {}

void Compositor::bind(wl_client* client, uint32_t version, uint32_t id) {
    auto* resource = wl_resource_create(client, &wl_compositor_interface,
            static_cast<int>(version), id);
    if (!resource) {
        wl_client_post_no_memory(client); return;
    }
    wl_resource_set_implementation(resource, compositor_vtable(), this, nullptr);
}

Compositor::WlSurface* Compositor::surface_from_resource(wl_resource* resource) {
    auto it = surfaces_.find(resource);
    return it != surfaces_.end() ? &it->second : nullptr;
}

wl_resource* Compositor::resource_for(SurfaceId id) const {
    auto it = id_to_resource_.find(id.value());
    return it != id_to_resource_.end() ? it->second : nullptr;
}

SurfaceId Compositor::id_from_resource(wl_resource* resource) const {
    auto it = surfaces_.find(resource);
    if (it == surfaces_.end()) return {};
    return it->second.id;
}

Compositor::SurfaceCommitSubscription
Compositor::subscribe_surface_commit(SurfaceCommitCallback cb) {
    if (!cb)
        return {};

    const uint64_t id = next_surface_commit_subscription_id_++;
    surface_commit_listeners_.push_back(SurfaceCommitListener{
            .id       = id,
            .callback = std::move(cb),
        });
    return SurfaceCommitSubscription(this, id);
}

void Compositor::unsubscribe_surface_commit(uint64_t id) {
    auto it = std::remove_if(surface_commit_listeners_.begin(),
            surface_commit_listeners_.end(),
            [id](const SurfaceCommitListener& listener) {
                return listener.id == id;
            });
    surface_commit_listeners_.erase(it, surface_commit_listeners_.end());
}

const SurfaceInfo* Compositor::surface_info(SurfaceId id) const {
    auto*                           res = resource_for(id);
    if (!res) return nullptr;
    auto                            it = surfaces_.find(res);
    if (it == surfaces_.end()) return nullptr;
    static thread_local SurfaceInfo info;
    auto&                           surf = it->second;
    info            = SurfaceInfo{
        .id         = surf.id,
        .has_commit = surf.has_commit,
        .has_buffer = surf.buffer != nullptr,
        .buf_width  = surf.buf_width,
        .buf_height = surf.buf_height,
    };
    return &info;
}

BufferView Compositor::buffer_view(SurfaceId id) {
    auto* res = resource_for(id);
    if (!res) return {};
    auto* surf = surface_from_resource(res);
    if (!surf || !surf->buffer) return {};
    return shm_.buffer_view(surf->buffer);
}

void Compositor::create_surface(wl_client* client, uint32_t id, int ver) {
    auto* surf_res = wl_resource_create(client, &wl_surface_interface, ver, id);
    if (!surf_res) {
        wl_client_post_no_memory(client); return;
    }

    WlSurface surf;
    surf.id       = SurfaceId{next_surface_id_++};
    surf.resource = surf_res;
    wl_list_init(&surf.buffer_destroy_listener.link);
    surfaces_[surf_res] = surf;
    wl_list_init(&surfaces_[surf_res].buffer_destroy_listener.link);
    id_to_resource_[surf.id.value()] = surf_res;

    wl_resource_set_implementation(surf_res, surface_vtable(), this,
        [](wl_resource* r) {
            auto* self = static_cast<Compositor*>(wl_resource_get_user_data(r));
            if (!self) return;
            auto it = self->surfaces_.find(r);
            if (it != self->surfaces_.end()) {
                self->id_to_resource_.erase(it->second.id.value());
                it->second.clear_buffer();
                self->surfaces_.erase(it);
            }
        });
}

void Compositor::commit(wl_resource* resource) {
    auto  it = surfaces_.find(resource);
    if (it == surfaces_.end()) return;
    auto& surf = it->second;

    if (surf.pending_attach) {
        if (surf.buffer) wl_buffer_send_release(surf.buffer);
        surf.set_buffer(surf.pending_buffer);
        surf.pending_buffer = nullptr;
        surf.pending_attach = false;

        if (surf.buffer) {
            auto view = shm_.buffer_view(surf.buffer);
            surf.buf_width  = view.width;
            surf.buf_height = view.height;
        }
    }

    surf.has_commit = true;

    auto listeners = surface_commit_listeners_;
    for (const auto& listener : listeners) {
        if (listener.callback)
            listener.callback(surf.id);
    }
}

const void* Compositor::compositor_vtable() {
    static const struct wl_compositor_interface impl = {
        .create_surface = [](wl_client* c, wl_resource* r, uint32_t id) {
                auto* self = static_cast<Compositor*>(wl_resource_get_user_data(r));
                self->create_surface(c, id, wl_resource_get_version(r));
            },
        .create_region = [](wl_client* c, wl_resource* r, uint32_t id) {
                int ver   = wl_resource_get_version(r);
                auto* reg = wl_resource_create(c, &wl_region_interface, ver, id);
                if (!reg) {
                    wl_client_post_no_memory(c); return;
                }
                wl_resource_set_implementation(reg, region_vtable(), nullptr, nullptr);
            },
    };
    return &impl;
}

const void* Compositor::surface_vtable() {
    static const struct wl_surface_interface impl = {
        .destroy = [](wl_client*, wl_resource* r) {
                wl_resource_destroy(r);
            },
        .attach = [](wl_client*, wl_resource* r, wl_resource* buf, int32_t, int32_t) {
                auto* self = static_cast<Compositor*>(wl_resource_get_user_data(r));
                auto* surf = self->surface_from_resource(r);
                if (!surf) return;
                surf->pending_buffer = buf;
                surf->pending_attach = true;
            },
        .damage = [](wl_client*, wl_resource*, int32_t, int32_t, int32_t, int32_t) {
            },
        .frame = [](wl_client* c, wl_resource*, uint32_t callback) {
                auto* cb = wl_resource_create(c, &wl_callback_interface, 1, callback);
                if (cb) {
                    wl_callback_send_done(cb, 0); wl_resource_destroy(cb);
                }
            },
        .set_opaque_region = [](wl_client*, wl_resource*, wl_resource*) {
            },
        .set_input_region = [](wl_client*, wl_resource*, wl_resource*) {
            },
        .commit = [](wl_client*, wl_resource* r) {
                auto* self = static_cast<Compositor*>(wl_resource_get_user_data(r));
                self->commit(r);
            },
        .set_buffer_transform = [](wl_client*, wl_resource*, int32_t) {
            },
        .set_buffer_scale = [](wl_client*, wl_resource*, int32_t) {
            },
        .damage_buffer = [](wl_client*, wl_resource*, int32_t, int32_t, int32_t, int32_t) {
            },
        .offset = [](wl_client*, wl_resource*, int32_t, int32_t) {
            },
    };
    return &impl;
}

const void* Compositor::region_vtable() {
    static const struct wl_region_interface impl = {
        .destroy = [](wl_client*, wl_resource* r) {
                wl_resource_destroy(r);
            },
        .add = [](wl_client*, wl_resource*, int32_t, int32_t, int32_t, int32_t) {
            },
        .subtract = [](wl_client*, wl_resource*, int32_t, int32_t, int32_t, int32_t) {
            },
    };
    return &impl;
}

// ── Subcompositor ──

const wl_interface* Subcompositor::interface() { return &wl_subcompositor_interface; }

Subcompositor::Subcompositor(Display& display)
    : global_(display, this) {}

void Subcompositor::bind(wl_client* client, uint32_t version, uint32_t id) {
    auto* resource = wl_resource_create(client, &wl_subcompositor_interface,
            static_cast<int>(version), id);
    if (!resource) {
        wl_client_post_no_memory(client); return;
    }

    static const struct wl_subcompositor_interface vtable = {
        .destroy = [](wl_client*, wl_resource* r) {
                wl_resource_destroy(r);
            },
        .get_subsurface = [](wl_client* client, wl_resource* resource,
            uint32_t id, wl_resource*, wl_resource*) {
                int ver   = wl_resource_get_version(resource);
                auto* sub = wl_resource_create(client, &wl_subsurface_interface, ver, id);
                if (!sub) {
                    wl_client_post_no_memory(client); return;
                }

                static const struct wl_subsurface_interface sub_vtable = {
                    .destroy = [](wl_client*, wl_resource* r) {
                            wl_resource_destroy(r);
                        },
                    .set_position = [](wl_client*, wl_resource*, int32_t, int32_t) {
                        },
                    .place_above = [](wl_client*, wl_resource*, wl_resource*) {
                        },
                    .place_below = [](wl_client*, wl_resource*, wl_resource*) {
                        },
                    .set_sync = [](wl_client*, wl_resource*) {
                        },
                    .set_desync = [](wl_client*, wl_resource*) {
                        },
                };
                wl_resource_set_implementation(sub, &sub_vtable, nullptr, nullptr);
            },
    };
    wl_resource_set_implementation(resource, &vtable, this, nullptr);
}

} // namespace wl::server
