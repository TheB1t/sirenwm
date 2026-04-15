#pragma once

#include <swm/ipc/backend_handlers.hpp>
#include <swm/ipc/backend_protocol.hpp>
#include <wl/server/runtime/overlay_manager.hpp>
#include <wl/server/protocol/surface_id.hpp>
#include <wl/server/protocol/xdg_shell.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

struct DisplaySurface {
    uint32_t              id          = 0;
    uint32_t              toplevel_id = 0;
    wl::server::SurfaceId wl_surface_id;
    std::string           app_id;
    std::string           title;
    uint32_t              pid          = 0;
    bool                  mapped       = false;
    bool                  visible      = false;
    int32_t               x            = 0;
    int32_t               y            = 0;
    int32_t               width        = 0;
    int32_t               height       = 0;
    uint32_t              stacking     = 0;
    uint32_t              border_width = 0;
    uint32_t              border_color = 0;
};

class DisplayState : public wl::server::XdgShellEventHandler {
    public:
        struct OutputInfo {
            uint32_t    id = 0;
            std::string name;
            int32_t     x       = 0;
            int32_t     y       = 0;
            int32_t     w       = 0;
            int32_t     h       = 0;
            int32_t     refresh = 0;
        };

        explicit DisplayState(OverlayManager& overlays);

        DisplayState(const DisplayState&)                                 = delete;
        DisplayState&                      operator=(const DisplayState&) = delete;

        void                               set_command_handler(swm::ipc::BackendCommandHandler* handler);
        void                               set_event_handler(swm::ipc::BackendEventHandler* handler);

        uint32_t                           add_surface(const std::string& app_id, const std::string& title,
            uint32_t pid, uint32_t toplevel_id = 0);
        void                               surface_mapped(uint32_t id);
        void                               surface_unmapped(uint32_t id);
        void                               surface_destroyed(uint32_t id);
        void                               destroy_surface_for_toplevel(uint32_t toplevel_id);
        uint32_t                           surface_id_from_toplevel(uint32_t toplevel_id) const;
        void                               set_wl_surface_id(uint32_t id, wl::server::SurfaceId sid);
        void                               surface_title_changed(uint32_t id, const std::string& title);
        void                               surface_app_id_changed(uint32_t id, const std::string& app_id);
        void                               surface_committed(uint32_t id, int32_t width, int32_t height);

        void                               output_added(uint32_t id, const std::string& name,
            int32_t x, int32_t y, int32_t w, int32_t h, int32_t refresh);
        void                               output_removed(uint32_t id);

        void                               key_press(uint32_t keycode, uint32_t keysym, uint32_t mods);
        void                               button_press(uint32_t surface_id, int32_t root_x, int32_t root_y,
            uint32_t button, uint32_t mods, bool released);
        void                               pointer_motion(uint32_t surface_id, int32_t root_x, int32_t root_y, uint32_t mods);
        void                               pointer_enter(uint32_t surface_id);

        bool                               is_intercepted(uint32_t keysym, uint32_t mods) const;

        const DisplaySurface*              surface(uint32_t id) const;
        std::vector<const DisplaySurface*> visible_surfaces_by_stacking() const;
        uint32_t                           surface_at(int32_t x, int32_t y) const;
        std::vector<DisplaySurface>        snapshot_surfaces() const;
        std::vector<OutputInfo>            snapshot_outputs() const;

        OverlayManager&       overlay_manager()       { return overlays_; }
        const OverlayManager& overlay_manager() const { return overlays_; }

        void overlay_button(uint32_t overlay_id, int32_t x, int32_t y, uint32_t button, bool released);

        void configure_surface(uint32_t id, int32_t x, int32_t y, int32_t w, int32_t h);
        void set_surface_activated(uint32_t id, bool activated);
        void set_surface_visible(uint32_t id, bool visible);
        void set_surface_stacking(uint32_t id, uint32_t mode);
        void close_surface(uint32_t id);
        void set_keyboard_intercepts(std::span<const swm::ipc::KeyIntercept> keys);
        void warp_pointer(int32_t x, int32_t y);
        void grab_pointer();
        void ungrab_pointer();
        void set_surface_border(uint32_t id, uint32_t w, uint32_t c);
        void create_overlay(uint32_t oid, int32_t x, int32_t y, int32_t w, int32_t h);
        void update_overlay(uint32_t oid, int32_t fd, uint32_t sz);
        void destroy_overlay(uint32_t oid);

        using wl::server::XdgShellEventHandler::on;

        void on(const wl::server::XdgToplevelCreated& msg) override;
        void on(const wl::server::XdgToplevelDestroyed& msg) override;
        void on(const wl::server::XdgToplevelMapped& msg) override;
        void on(const wl::server::XdgToplevelCommitted& msg) override;
        void on(const wl::server::XdgTitleChanged& msg) override;
        void on(const wl::server::XdgAppIdChanged& msg) override;
        void on(const wl::server::XdgFullscreenRequested& msg) override;
        void on(const wl::server::XdgMinSizeChanged& msg) override;
        void on(const wl::server::XdgMaxSizeChanged& msg) override;

    private:
        template <typename Msg>
        void emit_event(const Msg& msg) {
            if (event_handler_)
                event_handler_->on(msg);
        }

        template <typename Msg>
        void dispatch_command(const Msg& msg) {
            if (command_handler_)
                command_handler_->on(msg);
        }

        swm::ipc::BackendCommandHandler* command_handler_ = nullptr;
        swm::ipc::BackendEventHandler*   event_handler_   = nullptr;

        uint32_t next_surface_id_ = 1;
        std::unordered_map<uint32_t, DisplaySurface> surfaces_;
        OverlayManager& overlays_;
        std::vector<swm::ipc::KeyIntercept>      intercepts_;
        std::unordered_map<uint32_t, OutputInfo> outputs_;
};
