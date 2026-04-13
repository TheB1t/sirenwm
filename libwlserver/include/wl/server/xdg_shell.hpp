#pragma once

#include <wl/display.hpp>
#include <wl/global.hpp>
#include <wl/server/compositor.hpp>
#include <wl/server/surface_id.hpp>

#include <cstdint>
#include <string>
#include <unordered_map>

extern "C" {
#include <wayland-server-core.h>
}

namespace wl::server {

class XdgShell;

struct XdgShellListener {
    virtual ~XdgShellListener() = default;

    virtual void on_toplevel_created(uint32_t toplevel_id, SurfaceId surface, uint32_t pid) {}
    virtual void on_toplevel_destroyed(uint32_t toplevel_id) {}
    virtual void on_toplevel_mapped(uint32_t toplevel_id, int32_t buf_w, int32_t buf_h) {}
    virtual void on_toplevel_committed(uint32_t toplevel_id, int32_t buf_w, int32_t buf_h) {}
    virtual void on_title_changed(uint32_t toplevel_id, const std::string& title) {}
    virtual void on_app_id_changed(uint32_t toplevel_id, const std::string& app_id) {}
    virtual void on_fullscreen_requested(uint32_t toplevel_id, bool enter) {}
    virtual void on_min_size_changed(uint32_t toplevel_id, int32_t w, int32_t h) {}
    virtual void on_max_size_changed(uint32_t toplevel_id, int32_t w, int32_t h) {}
};

struct ToplevelState {
    uint32_t  id              = 0;
    SurfaceId surface_id;
    std::string app_id;
    std::string title;
    bool      mapped          = false;
};

class XdgShell {
public:
    XdgShell(Display& display, Compositor& compositor);

    XdgShell(const XdgShell&)            = delete;
    XdgShell& operator=(const XdgShell&) = delete;

    void set_listener(XdgShellListener* listener) { listener_ = listener; }

    const ToplevelState* toplevel(uint32_t id) const;
    const ToplevelState* toplevel_by_surface(SurfaceId sid) const;

    void configure_toplevel(uint32_t id, int32_t w, int32_t h);
    void close_toplevel(uint32_t id);

    static const wl_interface* interface();
    static int version() { return 3; }
    void bind(wl_client* client, uint32_t version, uint32_t id);

private:
    struct InternalToplevel {
        ToplevelState  state;
        wl_resource*   xdg_surface_res = nullptr;
        wl_resource*   toplevel_res    = nullptr;
    };

    wl::Global<XdgShell> global_;
    Compositor&          compositor_;
    XdgShellListener*    listener_ = nullptr;

    uint32_t next_toplevel_id_ = 1;
    std::unordered_map<wl_resource*, InternalToplevel> toplevels_;
    std::unordered_map<wl_resource*, wl_resource*>     xdg_to_toplevel_;
    std::unordered_map<wl_resource*, wl_resource*>     xdg_surface_to_surface_;

    InternalToplevel* find_by_id(uint32_t id);
    InternalToplevel* find_by_surface(SurfaceId sid);

    void on_xdg_surface_destroyed(wl_resource* xdg_surface);
    void on_surface_commit(SurfaceId sid);

    static void toplevel_resource_destroy(wl_resource* resource);
    static const void* wm_base_vtable();
    static const void* xdg_surface_vtable();
    static const void* toplevel_vtable();
};

} // namespace wl::server
