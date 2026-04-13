#pragma once

#include <wl/display.hpp>
#include <wl/global.hpp>
#include <wl/server/surface_id.hpp>

#include <cstdint>
#include <memory>
#include <vector>

extern "C" {
#include <xkbcommon/xkbcommon.h>
}

namespace wl::server {

class Compositor;
class DataDeviceManager;

class Seat {
public:
    Seat(Display& display, Compositor& compositor);
    ~Seat();

    Seat(const Seat&)            = delete;
    Seat& operator=(const Seat&) = delete;

    void send_keyboard_enter(SurfaceId surface);
    void send_keyboard_leave(SurfaceId surface);
    void send_key(uint32_t time, uint32_t key, bool pressed);
    void send_modifiers(uint32_t mods_depressed, uint32_t mods_latched,
                        uint32_t mods_locked, uint32_t group);

    void send_pointer_enter(SurfaceId surface, int32_t x, int32_t y);
    void send_pointer_leave(SurfaceId surface);
    void send_pointer_motion(uint32_t time, int32_t x, int32_t y);
    void send_pointer_button(uint32_t time, uint32_t button, bool pressed);

    uint32_t resolve_keysym(uint32_t evdev_keycode) const;
    void update_xkb_state(uint32_t evdev_keycode, bool pressed);

    static const wl_interface* interface();
    static int version() { return 5; }
    void bind(wl_client* client, uint32_t version, uint32_t id);

private:
    Compositor& compositor_;
    wl::Global<Seat> global_;
    std::unique_ptr<DataDeviceManager> ddm_;

    std::vector<wl_resource*> keyboards_;
    std::vector<wl_resource*> pointers_;
    SurfaceId focused_surface_;
    SurfaceId pointer_surface_;

    xkb_context* xkb_ctx_    = nullptr;
    xkb_keymap*  xkb_keymap_ = nullptr;
    xkb_state*   xkb_state_  = nullptr;

    uint32_t serial_ = 1;
    uint32_t next_serial() { return serial_++; }

    wl_resource* resolve(SurfaceId id) const;
    wl_resource* keyboard_for(wl_resource* surface) const;
    wl_resource* pointer_for(wl_resource* surface) const;

    void send_keymap(wl_resource* kb);
    void add_keyboard(wl_client* client, uint32_t id, int version);
    void add_pointer(wl_client* client, uint32_t id, int version);
};

class DataDeviceManager {
public:
    explicit DataDeviceManager(Display& display);

    DataDeviceManager(const DataDeviceManager&)            = delete;
    DataDeviceManager& operator=(const DataDeviceManager&) = delete;

    static const wl_interface* interface();
    static int version() { return 3; }

    void bind(wl_client* client, uint32_t version, uint32_t id);

private:
    wl::Global<DataDeviceManager> global_;
};

} // namespace wl::server
