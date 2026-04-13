#pragma once

#include <backend/backend.hpp>

#include <wl/client_display.hpp>
#include <wl/proxy.hpp>
#include <wl/registry.hpp>
#include <wl_ports.hpp>
#include "sirenwm-server-v1-client-api.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

struct wl_surface_info {
    uint32_t    id       = 0;
    std::string app_id;
    std::string title;
    uint32_t    pid      = 0;
    bool        mapped   = false;
    bool        managed  = false;
    int32_t     x        = 0;
    int32_t     y        = 0;
    int32_t     width    = 0;
    int32_t     height   = 0;
};

struct wl_output_info {
    uint32_t    id      = 0;
    std::string name;
    int32_t     x       = 0;
    int32_t     y       = 0;
    int32_t     w       = 0;
    int32_t     h       = 0;
    int32_t     refresh = 0;
};

namespace backend {
class WlInputPort;
class WlMonitorPort;
class WlRenderPort;
class WlKeyboardPort;
}

class WlBackend final : public Backend,
                         public wlproto::SirenwmAdminV1ClientApi<WlBackend> {
public:
    using Admin = wlproto::SirenwmAdminV1ClientApi<WlBackend>;

    WlBackend(Core& core, Runtime& runtime);
    ~WlBackend() override;

    int  event_fd() const override;
    void pump_events(std::size_t max_events_per_tick) override;
    void render_frame() override;
    void on_reload_applied() override { reload_border_colors(); }
    void shutdown() override;
    void on_start(Core& core) override;

    backend::BackendPorts ports() override;

    wl::ClientDisplay&       display()       { return display_; }
    const wl::ClientDisplay& display() const { return display_; }

    const std::unordered_map<uint32_t, wl_surface_info>& surfaces() const { return surfaces_; }
    const std::unordered_map<uint32_t, wl_output_info>&  outputs()  const { return outputs_; }

    // AdminClient event overrides
    void on_surface_created(uint32_t id, const char* app_id, const char* title, uint32_t pid);
    void on_surface_mapped(uint32_t id);
    void on_surface_unmapped(uint32_t id);
    void on_surface_destroyed(uint32_t id);
    void on_surface_title_changed(uint32_t id, const char* title);
    void on_surface_app_id_changed(uint32_t id, const char* app_id);
    void on_surface_committed(uint32_t id, int32_t w, int32_t h);
    void on_key_press(uint32_t keycode, uint32_t keysym, uint32_t mods);
    void on_button_press(uint32_t surface_id, int32_t x, int32_t y,
                         uint32_t button, uint32_t mods, uint32_t released);
    void on_pointer_motion(uint32_t surface_id, int32_t x, int32_t y, uint32_t mods);
    void on_pointer_enter(uint32_t surface_id);
    void on_output_added(uint32_t id, const char* name,
                         int32_t x, int32_t y, int32_t w, int32_t h, int32_t refresh);
    void on_output_removed(uint32_t id);
    void on_overlay_expose(uint32_t overlay_id);
    void on_overlay_button(uint32_t overlay_id, int32_t x, int32_t y,
                           uint32_t button, uint32_t released);

private:
    struct WlRegistry final : wl::Registry<WlRegistry> {
        WlRegistry(wl::ClientDisplay& display, WlBackend& backend_ref)
            : wl::Registry<WlRegistry>(display), backend(backend_ref) {}
        void on_global(uint32_t name, const char* iface, uint32_t version);
        WlBackend& backend;
    };

    wl::ClientDisplay display_;
    std::unique_ptr<WlRegistry> registry_;

    std::unordered_map<uint32_t, wl_surface_info> surfaces_;
    std::unordered_map<uint32_t, wl_output_info>  outputs_;

    backend::WlInputPort    input_;
    backend::WlMonitorPort  monitor_;
    backend::WlRenderPort   render_;
    backend::WlKeyboardPort keyboard_;

    Runtime& runtime_;

    WindowId prev_focused_      = NO_WINDOW;
    uint32_t focused_border_    = 0xFF4488CC;
    uint32_t unfocused_border_  = 0xFF333333;

    void manage_surface(wl_surface_info& s);
    void reload_border_colors();
};
