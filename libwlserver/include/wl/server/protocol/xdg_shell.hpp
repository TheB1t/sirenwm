#pragma once

#include <wl/display.hpp>
#include <wl/global.hpp>
#include <wl/server/protocol/compositor.hpp>
#include <wl/server/protocol/surface_id.hpp>

#include <cstdint>
#include <string>
#include <unordered_map>

extern "C" {
#include <wayland-server-core.h>
}

namespace wl::server {

class XdgShell;

#define WL_SERVER_XDG_SHELL_EVENT_TYPES(X) \
    X(XdgToplevelCreated) \
    X(XdgToplevelDestroyed) \
    X(XdgToplevelMapped) \
    X(XdgToplevelCommitted) \
    X(XdgTitleChanged) \
    X(XdgAppIdChanged) \
    X(XdgFullscreenRequested) \
    X(XdgMinSizeChanged) \
    X(XdgMaxSizeChanged)

struct XdgToplevelCreated {
    uint32_t  toplevel_id = 0;
    SurfaceId surface_id;
    uint32_t  pid = 0;
};

struct XdgToplevelDestroyed {
    uint32_t toplevel_id = 0;
};

struct XdgToplevelMapped {
    uint32_t toplevel_id = 0;
    int32_t  width       = 0;
    int32_t  height      = 0;
};

struct XdgToplevelCommitted {
    uint32_t toplevel_id = 0;
    int32_t  width       = 0;
    int32_t  height      = 0;
};

struct XdgTitleChanged {
    uint32_t    toplevel_id = 0;
    std::string title;
};

struct XdgAppIdChanged {
    uint32_t    toplevel_id = 0;
    std::string app_id;
};

struct XdgFullscreenRequested {
    uint32_t toplevel_id = 0;
    bool     enter       = false;
};

struct XdgMinSizeChanged {
    uint32_t toplevel_id = 0;
    int32_t  width       = 0;
    int32_t  height      = 0;
};

struct XdgMaxSizeChanged {
    uint32_t toplevel_id = 0;
    int32_t  width       = 0;
    int32_t  height      = 0;
};

class XdgShellEventHandler {
    public:
        virtual ~XdgShellEventHandler() = default;

#define DECLARE_XDG_SHELL_EVENT_HANDLER(TypeName) \
    virtual void on(const TypeName&) {}
        WL_SERVER_XDG_SHELL_EVENT_TYPES(DECLARE_XDG_SHELL_EVENT_HANDLER)
#undef DECLARE_XDG_SHELL_EVENT_HANDLER
};

struct ToplevelState {
    uint32_t    id = 0;
    SurfaceId   surface_id;
    std::string app_id;
    std::string title;
    bool        mapped = false;
};

class XdgShell {
    public:
        XdgShell(Display& display, Compositor& compositor);

        XdgShell(const XdgShell&)            = delete;
        XdgShell& operator=(const XdgShell&) = delete;

        void set_event_handler(XdgShellEventHandler* handler) { event_handler_ = handler; }

        const ToplevelState*       toplevel(uint32_t id) const;
        const ToplevelState*       toplevel_by_surface(SurfaceId sid) const;

        void                       configure_toplevel(uint32_t id, int32_t w, int32_t h);
        void                       close_toplevel(uint32_t id);

        static const wl_interface* interface();
        static int version() { return 3; }
        void                       bind(wl_client* client, uint32_t version, uint32_t id);

    private:
        struct InternalToplevel {
            ToplevelState state;
            wl_resource*  xdg_surface_res = nullptr;
            wl_resource*  toplevel_res    = nullptr;
        };

        wl::Global<XdgShell> global_;
        Compositor&          compositor_;
        Compositor::SurfaceCommitSubscription surface_commit_subscription_;
        XdgShellEventHandler* event_handler_ = nullptr;

        uint32_t next_toplevel_id_ = 1;
        std::unordered_map<wl_resource*, InternalToplevel> toplevels_;
        std::unordered_map<wl_resource*, wl_resource*>     xdg_to_toplevel_;
        std::unordered_map<wl_resource*, wl_resource*>     xdg_surface_to_surface_;

        InternalToplevel* find_by_id(uint32_t id);
        InternalToplevel* find_by_surface(SurfaceId sid);

        template <typename Event>
        void emit_event(const Event& event) {
            if (event_handler_)
                event_handler_->on(event);
        }

        void               on_xdg_surface_destroyed(wl_resource* xdg_surface);
        void               on_surface_commit(SurfaceId sid);

        static void        toplevel_resource_destroy(wl_resource* resource);
        static const void* wm_base_vtable();
        static const void* xdg_surface_vtable();
        static const void* toplevel_vtable();
};

#undef WL_SERVER_XDG_SHELL_EVENT_TYPES

} // namespace wl::server
