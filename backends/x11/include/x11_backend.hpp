#pragma once

#include <xcb/xcb.h>

#include <array>
#include <cstddef>
#include <memory>
#include <unordered_map>

#include <backend/backend.hpp>
#include <backend/input_port.hpp>
#include <backend/monitor_port.hpp>
#include <xconn.hpp>

class Core;
class Runtime;

class X11Backend final : public Backend {
    private:
        XConnection xconn;
        Core& core;
        Runtime& runtime;

        WindowId root_window = NO_WINDOW;
        std::array<bool, 256> key_down {};
        std::unique_ptr<backend::RenderPort> render_port_impl;
        std::unique_ptr<backend::InputPort> input_port_impl;
        std::unique_ptr<backend::MonitorPort> monitor_port_impl;
        xcb_key_symbols_t* key_syms = nullptr;
        uint32_t net_wm_name        = 0;
        uint32_t utf8_string        = 0;
        uint32_t net_wm_pid         = 0;
        std::unordered_map<int, int> restart_monitor_active_ws;

        // EWMH/ICCCM atoms
        xcb_window_t ewmh_wm_window                 = XCB_WINDOW_NONE;
        xcb_atom_t NET_SUPPORTED                    = XCB_ATOM_NONE;
        xcb_atom_t NET_WM_NAME                      = XCB_ATOM_NONE;
        xcb_atom_t NET_WM_STATE                     = XCB_ATOM_NONE;
        xcb_atom_t NET_WM_STATE_FULLSCREEN          = XCB_ATOM_NONE;
        xcb_atom_t NET_ACTIVE_WINDOW                = XCB_ATOM_NONE;
        xcb_atom_t NET_CLIENT_LIST                  = XCB_ATOM_NONE;
        xcb_atom_t NET_SUPPORTING_WM_CHECK          = XCB_ATOM_NONE;
        xcb_atom_t NET_WM_WINDOW_TYPE               = XCB_ATOM_NONE;
        xcb_atom_t NET_WM_WINDOW_TYPE_DOCK          = XCB_ATOM_NONE;
        xcb_atom_t NET_WM_WINDOW_TYPE_DIALOG        = XCB_ATOM_NONE;
        xcb_atom_t NET_WM_WINDOW_TYPE_DESKTOP       = XCB_ATOM_NONE;
        xcb_atom_t NET_WM_WINDOW_TYPE_NOTIFICATION  = XCB_ATOM_NONE;
        xcb_atom_t NET_WM_WINDOW_TYPE_TOOLTIP       = XCB_ATOM_NONE;
        xcb_atom_t NET_WM_WINDOW_TYPE_DND           = XCB_ATOM_NONE;
        xcb_atom_t NET_WM_WINDOW_TYPE_DROPDOWN_MENU = XCB_ATOM_NONE;
        xcb_atom_t NET_WM_WINDOW_TYPE_POPUP_MENU    = XCB_ATOM_NONE;
        xcb_atom_t NET_WM_WINDOW_TYPE_MENU          = XCB_ATOM_NONE;
        xcb_atom_t XEMBED_INFO                      = XCB_ATOM_NONE;
        xcb_atom_t NET_CLOSE_WINDOW                 = XCB_ATOM_NONE;
        xcb_atom_t NET_NUMBER_OF_DESKTOPS           = XCB_ATOM_NONE;
        xcb_atom_t NET_CURRENT_DESKTOP              = XCB_ATOM_NONE;
        xcb_atom_t NET_DESKTOP_NAMES                = XCB_ATOM_NONE;
        xcb_atom_t NET_DESKTOP_GEOMETRY             = XCB_ATOM_NONE;
        xcb_atom_t NET_DESKTOP_VIEWPORT             = XCB_ATOM_NONE;
        xcb_atom_t NET_WORKAREA                     = XCB_ATOM_NONE;
        xcb_atom_t NET_WM_DESKTOP                   = XCB_ATOM_NONE;
        xcb_atom_t UTF8_STRING_ATOM                 = XCB_ATOM_NONE;
        xcb_atom_t WM_PROTOCOLS                     = XCB_ATOM_NONE;
        xcb_atom_t WM_DELETE_WINDOW                 = XCB_ATOM_NONE;
        xcb_atom_t WM_TAKE_FOCUS                    = XCB_ATOM_NONE;
        xcb_atom_t WM_STATE = XCB_ATOM_NONE;

        // Border colors (derived from theme on start/reload)
        uint32_t border_focused_pixel   = 0;
        uint32_t border_unfocused_pixel = 0;
        WindowId border_last_focused    = NO_WINDOW;

        void set_border_color(WindowId win, uint32_t pixel);
        void reload_border_colors();

        // EWMH internals
        void ewmh_intern_atoms();
        void ewmh_update_client_list();
        bool ewmh_supports_delete(WindowId win);
        void ewmh_close_with_message(WindowId win);
        bool ewmh_has_fullscreen_state(WindowId win);
        void ewmh_set_fullscreen_state_property(WindowId win, bool enabled);
        void ewmh_apply_fullscreen(WindowId win, bool enabled);

        // X event handlers
        void handle_map_request(xcb_map_request_event_t* ev);
        void handle_map_notify(xcb_map_notify_event_t* ev);
        void handle_reparent_notify(xcb_reparent_notify_event_t* ev);
        void handle_unmap_notify(xcb_unmap_notify_event_t* ev);
        void handle_destroy_notify(xcb_destroy_notify_event_t* ev);
        void handle_configure_request(xcb_configure_request_event_t* ev);
        void handle_configure_notify(xcb_configure_notify_event_t* ev);
        void handle_key_event(xcb_key_press_event_t* ev);
        void handle_focus_event(xcb_focus_in_event_t* ev);
        void handle_button_event(xcb_button_press_event_t* ev);
        void handle_motion_notify(xcb_motion_notify_event_t* ev);
        void handle_client_message(xcb_client_message_event_t* ev);
        void handle_property_notify(xcb_property_notify_event_t* ev);
        void handle_expose(xcb_expose_event_t* ev);
        void handle_no_exposure(xcb_no_exposure_event_t*);
        void handle_graphics_exposure(xcb_graphics_exposure_event_t*);
        void handle_create_notify(xcb_create_notify_event_t*);
        void handle_ge_generic(xcb_ge_generic_event_t* ev);
        void handle_enter_notify(xcb_enter_notify_event_t* ev);
        void apply_core_backend_effects();
        void apply_xresources(Core& core);
        void restore_visible_focus();
        void update_focus(event::FocusChanged ev); // X11 state only, no emit
        // notify() — internal dispatch for EWMH/ICCCM reactions to domain events
        // Always includes runtime.emit so callers don't have to.
        void notify(event::WindowMapped ev);
        void notify(event::WindowUnmapped ev);
        void notify(event::FocusChanged ev);
        void notify(event::WorkspaceSwitched ev);
        void notify(event::WindowAssignedToWorkspace ev);
        // handle() — EWMH request handlers that return a consumed flag
        bool handle(event::ClientMessageEv ev);
        bool handle(event::CloseWindowRequest ev);
        void handle(event::ManageWindowQuery& ev);

    public:
        X11Backend(Core& core, Runtime& runtime);
        ~X11Backend() override;
        XConnection& connection() { return xconn; }
        const XConnection& connection() const { return xconn; }

        int                                 event_fd() const override;
        void                                pump_events(std::size_t max_events_per_tick) override;
        void                                render_frame() override;
        void                                on_reload_applied() override;
        void                                on_start(Core& core) override;
        void                                on(event::WorkspaceSwitched ev) override;
        void                                on(event::WindowAssignedToWorkspace ev) override;
        void                                on(event::FocusChanged ev) override;
        void                                on(event::WindowAdopted ev) override;
        bool                                close_window(WindowId window) override;
        void                                shutdown() override;
        std::vector<ExistingWindowSnapshot> scan_existing_windows() override;
        backend::InputPort*                 input_port()   override;
        backend::MonitorPort*               monitor_port() override;
        backend::RenderPort*                render_port() override;
        xcb_key_symbols_t*                  key_symbols();
        std::unique_ptr<backend::TrayHost>
        create_tray_host(WindowId owner_bar_window, int bar_x, int bar_y,
            int bar_h,
            bool own_selection) override;
        std::string window_title(WindowId window) const override;
        uint32_t    window_pid(WindowId window) const override;

        void        handle_generic_event(xcb_generic_event_t* ev);

        // EWMH public interface
        void ewmh_init();
        void ewmh_update_desktop_props();
        void focus_window(WindowId win);

        // Returns monitor_idx -> active_ws_id mapping saved during last exec-restart.
        // Empty if no restart state was loaded.
        const std::unordered_map<int, int>& consumed_restart_monitor_ws() const override {
            return restart_monitor_active_ws;
        }
};