#pragma once

#include <wl/server/protocol/compositor.hpp>
#include <wl/server/protocol/surface_id.hpp>
#include <wl/server/x11/xwayland_shell.hpp>
#include <xcb/connection.hpp>
#include <xcb/atom.hpp>
#include <xcb/property.hpp>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>

struct XWindow {
    uint32_t           xcb_id       = 0;
    uint32_t           surface_id   = 0;
    wl::server::SurfaceId wl_surface_id;

    std::string        title;
    std::string        wm_class;
    uint32_t           pid          = 0;
    bool               override_redirect = false;
    bool               mapped       = false;
    int16_t            x = 0, y = 0;
    uint16_t           width = 0, height = 0;

    uint64_t           serial       = 0;
};

class XwmSurfaceSink {
public:
    virtual ~XwmSurfaceSink() = default;

    virtual uint32_t add_surface(const std::string& app_id, const std::string& title,
                                 uint32_t pid) = 0;
    virtual void surface_mapped(uint32_t id) = 0;
    virtual void surface_destroyed(uint32_t id) = 0;
    virtual void set_wl_surface_id(uint32_t id, wl::server::SurfaceId sid) = 0;
    virtual void surface_title_changed(uint32_t id, const std::string& title) = 0;
    virtual void surface_app_id_changed(uint32_t id, const std::string& app_id) = 0;
    virtual void surface_committed(uint32_t id, int32_t width, int32_t height) = 0;
};

class XWindowManager : protected xcb::Connection {
public:
    XWindowManager(int wm_fd, wl_client* xwl_client,
                   XWaylandShell& shell,
                   wl::server::Compositor& compositor,
                   XwmSurfaceSink& sink,
                   int output_w, int output_h);
    ~XWindowManager();

    XWindowManager(const XWindowManager&) = delete;
    XWindowManager& operator=(const XWindowManager&) = delete;

    using xcb::Connection::fd;
    void dispatch();

    void configure(uint32_t surface_id, int32_t x, int32_t y, int32_t w, int32_t h);
    void close(uint32_t surface_id);
    void activate(uint32_t surface_id, bool activated);
    bool owns(uint32_t surface_id) const;

private:
    xcb_window_t            wm_window_ = 0;
    wl_client*              xwl_client_ = nullptr;
    XWaylandShell&          shell_;
    wl::server::Compositor& compositor_;
    XwmSurfaceSink&         sink_;
    int                     output_w_, output_h_;

    enum Atom {
        WL_SURFACE_ID, WL_SURFACE_SERIAL,
        WM_DELETE_WINDOW, WM_PROTOCOLS, WM_S0, WM_STATE,
        NET_WM_NAME, NET_WM_PID, NET_WM_WINDOW_TYPE,
        NET_SUPPORTED, NET_SUPPORTING_WM_CHECK,
        NET_WM_CM_S0, NET_ACTIVE_WINDOW,
        NET_WM_WINDOW_TYPE_NORMAL,
        UTF8_STRING, WM_CLASS_ATOM,
        ATOM_COUNT
    };
    xcb_atom_t atoms_[ATOM_COUNT] = {};
    static constexpr const char* atom_names_[ATOM_COUNT] = {
        "WL_SURFACE_ID", "WL_SURFACE_SERIAL",
        "WM_DELETE_WINDOW", "WM_PROTOCOLS", "WM_S0", "WM_STATE",
        "_NET_WM_NAME", "_NET_WM_PID", "_NET_WM_WINDOW_TYPE",
        "_NET_SUPPORTED", "_NET_SUPPORTING_WM_CHECK",
        "_NET_WM_CM_S0", "_NET_ACTIVE_WINDOW",
        "_NET_WM_WINDOW_TYPE_NORMAL",
        "UTF8_STRING", "WM_CLASS",
    };

    std::unordered_map<uint32_t, XWindow> windows_;
    std::unordered_set<uint32_t> managed_surface_ids_;

    void intern_atoms();
    void setup_wm();
    void create_wm_window();

    void handle_create_notify(xcb_create_notify_event_t* ev);
    void handle_map_request(xcb_map_request_event_t* ev);
    void handle_configure_request(xcb_configure_request_event_t* ev);
    void handle_unmap_notify(xcb_unmap_notify_event_t* ev);
    void handle_destroy_notify(xcb_destroy_notify_event_t* ev);
    void handle_property_notify(xcb_property_notify_event_t* ev);
    void handle_client_message(xcb_client_message_event_t* ev);

    void try_associate(XWindow& xwin);
    void associate(XWindow& xwin, wl::server::SurfaceId sid);

    void read_title(XWindow& xwin);
    void read_class(XWindow& xwin);
    void read_pid(XWindow& xwin);

    void send_configure(uint32_t win, int16_t x, int16_t y, uint16_t w, uint16_t h);
    void send_wm_delete(uint32_t win);
    void set_wm_state(uint32_t win, uint32_t state);
    void set_net_active(uint32_t win);

    XWindow* window_by_surface_id(uint32_t surface_id);
};
