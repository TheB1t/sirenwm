#include <wl/server/seat.hpp>
#include <wl/server/compositor.hpp>

#include <algorithm>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>

extern "C" {
#include <wayland-server-protocol.h>
}

namespace wl::server {

// ── Seat ──

const wl_interface* Seat::interface() { return &wl_seat_interface; }

Seat::Seat(Display& display, Compositor& compositor)
    : compositor_(compositor), global_(display, this) {
    ddm_ = std::make_unique<DataDeviceManager>(display);

    xkb_ctx_ = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (xkb_ctx_) {
        struct xkb_rule_names names = {};
        xkb_keymap_ = xkb_keymap_new_from_names(xkb_ctx_, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
        if (xkb_keymap_)
            xkb_state_ = xkb_state_new(xkb_keymap_);
    }
}

Seat::~Seat() {
    if (xkb_state_)  xkb_state_unref(xkb_state_);
    if (xkb_keymap_) xkb_keymap_unref(xkb_keymap_);
    if (xkb_ctx_)    xkb_context_unref(xkb_ctx_);
}

wl_resource* Seat::resolve(SurfaceId id) const {
    return compositor_.resource_for(id);
}

void Seat::bind(wl_client* client, uint32_t version, uint32_t id) {
    auto* resource = wl_resource_create(client, &wl_seat_interface,
                                        static_cast<int>(version), id);
    if (!resource) { wl_client_post_no_memory(client); return; }

    static const struct wl_seat_interface vtable = {
        .get_pointer = [](wl_client* c, wl_resource* r, uint32_t id) {
            auto* self = static_cast<Seat*>(wl_resource_get_user_data(r));
            self->add_pointer(c, id, wl_resource_get_version(r));
        },
        .get_keyboard = [](wl_client* c, wl_resource* r, uint32_t id) {
            auto* self = static_cast<Seat*>(wl_resource_get_user_data(r));
            self->add_keyboard(c, id, wl_resource_get_version(r));
        },
        .get_touch = [](wl_client*, wl_resource*, uint32_t) {},
        .release   = [](wl_client*, wl_resource* r) { wl_resource_destroy(r); },
    };
    wl_resource_set_implementation(resource, &vtable, this, nullptr);

    wl_seat_send_capabilities(resource,
        WL_SEAT_CAPABILITY_KEYBOARD | WL_SEAT_CAPABILITY_POINTER);
    if (version >= 2)
        wl_seat_send_name(resource, "seat0");
}

void Seat::add_keyboard(wl_client* client, uint32_t id, int ver) {
    static const struct wl_keyboard_interface kb_vtable = {
        .release = [](wl_client*, wl_resource* r) { wl_resource_destroy(r); },
    };

    auto* kb = wl_resource_create(client, &wl_keyboard_interface, ver, id);
    if (!kb) { wl_client_post_no_memory(client); return; }
    wl_resource_set_implementation(kb, &kb_vtable, this,
        [](wl_resource* r) {
            auto* self = static_cast<Seat*>(wl_resource_get_user_data(r));
            auto& v = self->keyboards_;
            v.erase(std::remove(v.begin(), v.end(), r), v.end());
            auto* focused = self->resolve(self->focused_surface_);
            if (focused && wl_resource_get_client(r) == wl_resource_get_client(focused))
                self->focused_surface_ = SurfaceId{};
        });
    keyboards_.push_back(kb);
    send_keymap(kb);
}

void Seat::add_pointer(wl_client* client, uint32_t id, int ver) {
    static const struct wl_pointer_interface ptr_vtable = {
        .set_cursor = [](wl_client* client, wl_resource* r, uint32_t,
                         wl_resource* surface, int32_t hotspot_x, int32_t hotspot_y) {
            auto* self = static_cast<Seat*>(wl_resource_get_user_data(r));
            if (!self || !self->cursor_update_)
                return;

            auto* pointed = self->resolve(self->pointer_surface_);
            if (!pointed || wl_resource_get_client(pointed) != client)
                return;

            if (!surface) {
                self->cursor_update_(SurfaceId{}, 0, 0);
                return;
            }

            auto sid = self->compositor_.id_from_resource(surface);
            if (!sid)
                return;
            self->cursor_update_(sid, hotspot_x, hotspot_y);
        },
        .release    = [](wl_client*, wl_resource* r) { wl_resource_destroy(r); },
    };

    auto* ptr = wl_resource_create(client, &wl_pointer_interface, ver, id);
    if (!ptr) { wl_client_post_no_memory(client); return; }
    wl_resource_set_implementation(ptr, &ptr_vtable, this,
        [](wl_resource* r) {
            auto* self = static_cast<Seat*>(wl_resource_get_user_data(r));
            auto& v = self->pointers_;
            v.erase(std::remove(v.begin(), v.end(), r), v.end());
            auto* pointed = self->resolve(self->pointer_surface_);
            if (pointed && wl_resource_get_client(r) == wl_resource_get_client(pointed))
                self->pointer_surface_ = SurfaceId{};
        });
    pointers_.push_back(ptr);
}

void Seat::send_keymap(wl_resource* kb) {
    if (!kb || !xkb_keymap_) return;
    char* str = xkb_keymap_get_as_string(xkb_keymap_, XKB_KEYMAP_FORMAT_TEXT_V1);
    if (!str) return;

    size_t size = strlen(str) + 1;
    int fd = memfd_create("keymap", MFD_CLOEXEC);
    if (fd >= 0) {
        if (write(fd, str, size) == static_cast<ssize_t>(size))
            wl_keyboard_send_keymap(kb, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1,
                                    fd, static_cast<uint32_t>(size));
        close(fd);
    }
    free(str);
}

wl_resource* Seat::keyboard_for(wl_resource* surface) const {
    if (!surface) return nullptr;
    auto* client = wl_resource_get_client(surface);
    for (auto* kb : keyboards_)
        if (wl_resource_get_client(kb) == client) return kb;
    return nullptr;
}

wl_resource* Seat::pointer_for(wl_resource* surface) const {
    if (!surface) return nullptr;
    auto* client = wl_resource_get_client(surface);
    for (auto* ptr : pointers_)
        if (wl_resource_get_client(ptr) == client) return ptr;
    return nullptr;
}

void Seat::send_keyboard_enter(SurfaceId surface) {
    auto* res = resolve(surface);
    if (!res) return;
    if (focused_surface_ == surface) return;

    auto* previous = resolve(focused_surface_);
    if (previous) {
        auto* old_kb = keyboard_for(previous);
        if (old_kb)
            wl_keyboard_send_leave(old_kb, next_serial(), previous);
    }

    auto* kb = keyboard_for(res);
    if (!kb) return;
    focused_surface_ = surface;
    wl_array keys;
    wl_array_init(&keys);
    wl_keyboard_send_enter(kb, next_serial(), res, &keys);
    wl_array_release(&keys);
    send_current_modifiers();
}

void Seat::send_keyboard_leave(SurfaceId surface) {
    auto* res = resolve(surface);
    if (!res) {
        if (focused_surface_ == surface)
            focused_surface_ = SurfaceId{};
        return;
    }
    auto* kb = keyboard_for(res);
    if (!kb) {
        if (focused_surface_ == surface)
            focused_surface_ = SurfaceId{};
        return;
    }
    if (focused_surface_ == surface)
        focused_surface_ = SurfaceId{};
    wl_keyboard_send_leave(kb, next_serial(), res);
}

void Seat::send_key(uint32_t time, uint32_t key, bool pressed) {
    auto* focused = resolve(focused_surface_);
    if (!focused) {
        focused_surface_ = SurfaceId{};
        return;
    }
    auto* kb = keyboard_for(focused);
    if (!kb) return;
    wl_keyboard_send_key(kb, next_serial(), time, key,
                         pressed ? WL_KEYBOARD_KEY_STATE_PRESSED
                                 : WL_KEYBOARD_KEY_STATE_RELEASED);
}

void Seat::send_modifiers(uint32_t depressed, uint32_t latched,
                           uint32_t locked, uint32_t group) {
    auto* focused = resolve(focused_surface_);
    if (!focused) {
        focused_surface_ = SurfaceId{};
        return;
    }
    auto* kb = keyboard_for(focused);
    if (!kb) return;
    wl_keyboard_send_modifiers(kb, next_serial(), depressed, latched, locked, group);
}

void Seat::send_current_modifiers() {
    if (!xkb_state_)
        return;
    const uint32_t depressed = xkb_state_serialize_mods(xkb_state_, XKB_STATE_MODS_DEPRESSED);
    const uint32_t latched   = xkb_state_serialize_mods(xkb_state_, XKB_STATE_MODS_LATCHED);
    const uint32_t locked    = xkb_state_serialize_mods(xkb_state_, XKB_STATE_MODS_LOCKED);
    const uint32_t group     = xkb_state_serialize_layout(xkb_state_, XKB_STATE_LAYOUT_EFFECTIVE);
    send_modifiers(depressed, latched, locked, group);
}

void Seat::send_pointer_enter(SurfaceId surface, int32_t x, int32_t y) {
    auto* res = resolve(surface);
    if (!res) return;
    auto* ptr = pointer_for(res);
    if (!ptr) return;
    if (pointer_surface_ == surface) return;
    auto* previous = resolve(pointer_surface_);
    if (previous) {
        auto* old_ptr = pointer_for(previous);
        if (old_ptr)
            wl_pointer_send_leave(old_ptr, next_serial(), previous);
    }
    pointer_surface_ = surface;
    wl_pointer_send_enter(ptr, next_serial(), res,
                          wl_fixed_from_int(x), wl_fixed_from_int(y));
    if (wl_resource_get_version(ptr) >= WL_POINTER_FRAME_SINCE_VERSION)
        wl_pointer_send_frame(ptr);
}

void Seat::send_pointer_leave(SurfaceId surface) {
    auto* res = resolve(surface);
    if (!res) {
        if (pointer_surface_ == surface)
            pointer_surface_ = SurfaceId{};
        return;
    }
    auto* ptr = pointer_for(res);
    if (!ptr) return;
    if (pointer_surface_ == surface) pointer_surface_ = SurfaceId{};
    wl_pointer_send_leave(ptr, next_serial(), res);
    if (wl_resource_get_version(ptr) >= WL_POINTER_FRAME_SINCE_VERSION)
        wl_pointer_send_frame(ptr);
    if (cursor_update_ && pointer_surface_ == SurfaceId{})
        cursor_update_(SurfaceId{}, 0, 0);
}

void Seat::send_pointer_motion(uint32_t time, int32_t x, int32_t y) {
    auto* pointed = resolve(pointer_surface_);
    if (!pointed) {
        pointer_surface_ = SurfaceId{};
        return;
    }
    auto* ptr = pointer_for(pointed);
    if (!ptr) return;
    wl_pointer_send_motion(ptr, time, wl_fixed_from_int(x), wl_fixed_from_int(y));
    if (wl_resource_get_version(ptr) >= WL_POINTER_FRAME_SINCE_VERSION)
        wl_pointer_send_frame(ptr);
}

void Seat::send_pointer_button(uint32_t time, uint32_t button, bool pressed) {
    auto* pointed = resolve(pointer_surface_);
    if (!pointed) {
        pointer_surface_ = SurfaceId{};
        return;
    }
    auto* ptr = pointer_for(pointed);
    if (!ptr) return;
    wl_pointer_send_button(ptr, next_serial(), time, button,
                           pressed ? WL_POINTER_BUTTON_STATE_PRESSED
                                   : WL_POINTER_BUTTON_STATE_RELEASED);
    if (wl_resource_get_version(ptr) >= WL_POINTER_FRAME_SINCE_VERSION)
        wl_pointer_send_frame(ptr);
}

uint32_t Seat::resolve_keysym(uint32_t evdev_keycode) const {
    if (!xkb_state_) return 0;
    return xkb_state_key_get_one_sym(xkb_state_, evdev_keycode + 8);
}

void Seat::update_xkb_state(uint32_t evdev_keycode, bool pressed) {
    if (!xkb_state_) return;
    xkb_state_update_key(xkb_state_, evdev_keycode + 8,
                         pressed ? XKB_KEY_DOWN : XKB_KEY_UP);
}

// ── DataDeviceManager ──

const wl_interface* DataDeviceManager::interface() { return &wl_data_device_manager_interface; }

DataDeviceManager::DataDeviceManager(Display& display)
    : global_(display, this) {}

void DataDeviceManager::bind(wl_client* client, uint32_t version, uint32_t id) {
    auto* resource = wl_resource_create(client, &wl_data_device_manager_interface,
                                        static_cast<int>(version), id);
    if (!resource) { wl_client_post_no_memory(client); return; }

    static const struct wl_data_device_manager_interface vtable = {
        .create_data_source = [](wl_client* client, wl_resource*, uint32_t id) {
            auto* src = wl_resource_create(client, &wl_data_source_interface, 1, id);
            if (!src) { wl_client_post_no_memory(client); return; }
            wl_resource_set_implementation(src, nullptr, nullptr, nullptr);
        },
        .get_data_device = [](wl_client* client, wl_resource* resource,
                              uint32_t id, wl_resource*) {
            int ver = wl_resource_get_version(resource);
            auto* dd = wl_resource_create(client, &wl_data_device_interface, ver, id);
            if (!dd) { wl_client_post_no_memory(client); return; }

            static const struct wl_data_device_interface dd_impl = {
                .start_drag    = [](wl_client*, wl_resource*, wl_resource*, wl_resource*, wl_resource*, uint32_t) {},
                .set_selection = [](wl_client*, wl_resource*, wl_resource*, uint32_t) {},
                .release       = [](wl_client*, wl_resource* r) { wl_resource_destroy(r); },
            };
            wl_resource_set_implementation(dd, &dd_impl, nullptr, nullptr);
        },
    };
    wl_resource_set_implementation(resource, &vtable, this, nullptr);
}

} // namespace wl::server
