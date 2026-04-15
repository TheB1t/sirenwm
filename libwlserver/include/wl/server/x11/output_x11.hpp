#pragma once

#include <wl/server/protocol/compositor.hpp>
#include <wl/server/runtime/display_state.hpp>
#include <wl/server/protocol/seat.hpp>
#include <wl/server/protocol/surface_id.hpp>
#include <wl/server/protocol/xdg_shell.hpp>
#include <xcb/connection.hpp>
#include <xcb/visual.hpp>
#include <xcb/atom.hpp>

#include <cstdint>
#include <unordered_set>

extern "C" {
#include <cairo/cairo.h>
#include <cairo/cairo-xcb.h>
}

class OutputX11 : protected xcb::Connection {
    public:
        OutputX11(int width, int height);
        ~OutputX11();

        OutputX11(const OutputX11&)            = delete;
        OutputX11& operator=(const OutputX11&) = delete;

        bool valid() const { return conn_ && window_ != 0; }

        using xcb::Connection::fd;

        void repaint(wl::server::Compositor& compositor, wl::server::XdgShell& xdg_shell, DisplayState& state);
        bool pump_events(DisplayState& state, wl::server::Seat& seat, wl::server::XdgShell& xdg_shell);

        void request_repaint() { repaint_needed_ = true; }
        void set_pointer_grabbed(bool g) { pointer_grabbed_ = g; }
        void set_cursor_surface(wl::server::SurfaceId sid, int32_t hotspot_x, int32_t hotspot_y);

        int width()  const { return width_; }
        int height() const { return height_; }

    private:
        xcb_window_t      window_           = 0;
        xcb_visualtype_t* visual_           = nullptr;
        xcb_atom_t        wm_delete_window_ = 0;

        cairo_surface_t* cairo_surface_ = nullptr;

        int      width_;
        int      height_;
        bool     repaint_needed_  = true;
        bool     pointer_grabbed_ = false;
        uint32_t pointer_surface_ = 0;
        std::unordered_set<uint32_t> intercepted_keys_;
        wl::server::SurfaceId        cursor_surface_;
        int32_t      cursor_hotspot_x_ = 0;
        int32_t      cursor_hotspot_y_ = 0;
        int32_t      pointer_x_        = 0;
        int32_t      pointer_y_        = 0;
        bool         pointer_valid_    = false;
        xcb_cursor_t default_cursor_   = XCB_CURSOR_NONE;
        xcb_cursor_t invisible_cursor_ = XCB_CURSOR_NONE;

        xcb_pixmap_t     back_pixmap_  = 0;
        cairo_surface_t* back_surface_ = nullptr;

        void setup_wm_delete();
        void create_back_buffer();

        bool handle_client_message(xcb_client_message_event_t* ev);
        void handle_configure_notify(xcb_configure_notify_event_t* ev);
        void handle_key_press(xcb_key_press_event_t* ev, DisplayState& state, wl::server::Seat& seat);
        void handle_key_release(xcb_key_release_event_t* ev, wl::server::Seat& seat);
        void handle_button_press(xcb_button_press_event_t* ev, DisplayState& state, wl::server::Seat& seat);
        void handle_button_release(xcb_button_release_event_t* ev, DisplayState& state, wl::server::Seat& seat);
        void handle_motion_notify(xcb_motion_notify_event_t* ev, DisplayState& state, wl::server::Seat& seat, wl::server::XdgShell& xdg_shell);
};
