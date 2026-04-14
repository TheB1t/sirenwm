#pragma once

#include <wl/server/overlay_manager.hpp>
#include <wl/server/xdg_shell.hpp>
#include <wl/display.hpp>
#include <wl/global.hpp>
#include <wl/server/surface_id.hpp>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

struct AdminListener {
    virtual ~AdminListener() = default;

    virtual void on_configure(uint32_t id, int32_t x, int32_t y, int32_t w, int32_t h) {}
    virtual void on_activate(uint32_t id, bool activated) {}
    virtual void on_visibility(uint32_t id, bool visible) {}
    virtual void on_stacking(uint32_t id, uint32_t mode) {}
    virtual void on_close(uint32_t id) {}
    virtual void on_warp_pointer(int32_t x, int32_t y) {}
    virtual void on_pointer_constraint(int32_t x, int32_t y, int32_t w, int32_t h) {}
    virtual void on_grab_pointer() {}
    virtual void on_ungrab_pointer() {}
    virtual void on_border(uint32_t id, uint32_t width, uint32_t color) {}
};

struct AdminSurface {
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

class Admin : public wl::server::XdgShellListener {
    public:
        Admin(wl::Display& display, OverlayManager& overlays);
        ~Admin();

        Admin(const Admin&)              = delete;
        Admin&   operator=(const Admin&) = delete;

        void     set_listener(AdminListener* listener);

        uint32_t add_surface(const std::string& app_id, const std::string& title,
            uint32_t pid, uint32_t toplevel_id = 0);
        void     surface_mapped(uint32_t id);
        void     surface_unmapped(uint32_t id);
        void     surface_destroyed(uint32_t id);
        void     surface_destroyed_by_toplevel(uint32_t toplevel_id);
        uint32_t admin_id_from_toplevel(uint32_t toplevel_id) const;
        void     set_surface_wl_id(uint32_t id, wl::server::SurfaceId sid);
        void     surface_title_changed(uint32_t id, const std::string& title);
        void     surface_app_id_changed(uint32_t id, const std::string& app_id);
        void     surface_committed(uint32_t id, int32_t width, int32_t height);
        void     surface_request_fullscreen(uint32_t id, bool enter);
        void     surface_geometry_hint(uint32_t id, int32_t min_w, int32_t min_h,
            int32_t max_w, int32_t max_h);

        void                             output_added(uint32_t id, const std::string& name,
            int32_t x, int32_t y, int32_t w, int32_t h, int32_t refresh);
        void                             output_removed(uint32_t id);

        void                             key_press(uint32_t keycode, uint32_t keysym, uint32_t mods);
        void                             button_press(uint32_t surface_id, int32_t root_x, int32_t root_y,
            uint32_t button, uint32_t mods, bool released);
        void                             pointer_motion(uint32_t surface_id, int32_t root_x, int32_t root_y, uint32_t mods);
        void                             pointer_enter(uint32_t surface_id);

        bool                             has_admin() const;
        bool                             is_intercepted(uint32_t keysym, uint32_t mods) const;

        const AdminSurface*              surface(uint32_t id) const;
        std::vector<const AdminSurface*> visible_surfaces_by_stacking() const;
        uint32_t                         surface_at(int32_t x, int32_t y) const;

        OverlayManager&       overlay_manager()       { return overlays_; }
        const OverlayManager& overlay_manager() const { return overlays_; }

        void overlay_button(uint32_t overlay_id, int32_t x, int32_t y, uint32_t button, bool released);

        // wl::server::XdgShellListener
        void on_toplevel_created(uint32_t toplevel_id, wl::server::SurfaceId surface, uint32_t pid) override;
        void on_toplevel_destroyed(uint32_t toplevel_id) override;
        void on_toplevel_mapped(uint32_t toplevel_id, int32_t buf_w, int32_t buf_h) override;
        void on_toplevel_committed(uint32_t toplevel_id, int32_t buf_w, int32_t buf_h) override;
        void on_title_changed(uint32_t toplevel_id, const std::string& title) override;
        void on_app_id_changed(uint32_t toplevel_id, const std::string& app_id) override;
        void on_fullscreen_requested(uint32_t toplevel_id, bool enter) override;
        void on_min_size_changed(uint32_t toplevel_id, int32_t w, int32_t h) override;
        void on_max_size_changed(uint32_t toplevel_id, int32_t w, int32_t h) override;

    private:
        wl_global*     global_         = nullptr;
        wl_resource*   admin_resource_ = nullptr;
        AdminListener* listener_       = nullptr;

        uint32_t next_surface_id_ = 1;
        std::unordered_map<uint32_t, AdminSurface> surfaces_;
        OverlayManager& overlays_;

        struct KeyIntercept { uint32_t keysym; uint32_t mods; };
        std::vector<KeyIntercept> intercepts_;

        struct OutputInfo {
            uint32_t id; std::string name;
            int32_t  x, y, w, h, refresh;
        };
        std::unordered_map<uint32_t, OutputInfo> outputs_;

        static void        bind_thunk(wl_client* client, void* data, uint32_t version, uint32_t id);
        void               bind(wl_client* client, uint32_t version, uint32_t id);

        static const void* vtable();
        static Admin*      self(wl_resource* r);

        void               req_get_surface_list();
        void               req_configure_surface(uint32_t id, int32_t x, int32_t y, int32_t w, int32_t h);
        void               req_set_surface_activated(uint32_t id, uint32_t a);
        void               req_set_surface_visible(uint32_t id, uint32_t v);
        void               req_set_surface_stacking(uint32_t id, uint32_t m);
        void               req_close_surface(uint32_t id);
        void               req_set_keyboard_intercepts(wl_array* keys);
        void               req_warp_pointer(int32_t x, int32_t y);
        void               req_set_pointer_constraint(int32_t x, int32_t y, int32_t w, int32_t h);
        void               req_grab_pointer();
        void               req_ungrab_pointer();
        void               req_set_surface_border(uint32_t id, uint32_t w, uint32_t c);
        void               req_create_overlay(uint32_t oid, int32_t x, int32_t y, int32_t w, int32_t h);
        void               req_update_overlay(uint32_t oid, int32_t fd, uint32_t sz);
        void               req_destroy_overlay(uint32_t oid);
};
