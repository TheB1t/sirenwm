#pragma once

#include <window.hpp>
#include <xconn.hpp>
#include <xcb/xcb.h>
#include <optional>
#include <utility>
#include <vector>
#include <algorithm>

class X11Backend;

// EWMH/ICCCM atoms shared across all X11Windows, owned by X11Backend.
struct X11Atoms {
    xcb_atom_t NET_WM_STATE            = XCB_ATOM_NONE;
    xcb_atom_t NET_WM_STATE_FULLSCREEN = XCB_ATOM_NONE;
    xcb_atom_t NET_WM_STATE_HIDDEN     = XCB_ATOM_NONE;
    xcb_atom_t NET_WM_STATE_FOCUSED    = XCB_ATOM_NONE;
    xcb_atom_t NET_FRAME_EXTENTS       = XCB_ATOM_NONE;
    xcb_atom_t NET_WM_DESKTOP          = XCB_ATOM_NONE;
    xcb_atom_t WM_STATE                = XCB_ATOM_NONE;
    xcb_atom_t WM_PROTOCOLS            = XCB_ATOM_NONE;
    xcb_atom_t WM_DELETE_WINDOW        = XCB_ATOM_NONE;
    xcb_atom_t WM_TAKE_FOCUS           = XCB_ATOM_NONE;
};

// X11-specific window. Created by X11Backend::create_window().
// All X11 operations on a managed window go through this object.
// If the object doesn't exist, the window is dead — no X requests are issued.
struct X11Window : public swm::Window {
    XConnection&    xconn;
    const X11Atoms& atoms;

    // Pending WM-initiated unmaps counter.
    int pending_wm_unmaps = 0;

    X11Window(XConnection& xconn, const X11Atoms& atoms)
        : xconn(xconn), atoms(atoms) {}

    // --- Per-window X11 operations ---

    void set_border_color(uint32_t pixel) {
        xconn.change_window_attributes(id, XCB_CW_BORDER_PIXEL, &pixel);
    }

    void set_wm_state_normal() {
        if (atoms.WM_STATE != XCB_ATOM_NONE) {
            uint32_t data[2] = { 1 /* NormalState */, XCB_WINDOW_NONE };
            xconn.change_property(id, atoms.WM_STATE, atoms.WM_STATE, 32, 2, data);
        }
    }

    void set_wm_state(uint32_t state) {
        if (atoms.WM_STATE != XCB_ATOM_NONE) {
            uint32_t data[2] = { state, XCB_WINDOW_NONE };
            xconn.change_property(id, atoms.WM_STATE, atoms.WM_STATE, 32, 2, data);
        }
    }

    void set_wm_state_atom(xcb_atom_t atom, bool enabled) {
        auto states = xconn.get_atom_list_property(id, atoms.NET_WM_STATE);
        states.erase(std::remove(states.begin(), states.end(), atom), states.end());
        if (enabled)
            states.push_back(atom);
        const xcb_atom_t* data = states.empty() ? nullptr : states.data();
        xconn.set_property(id, atoms.NET_WM_STATE, XCB_ATOM_ATOM, data, (int)states.size());
    }

    void set_fullscreen_state(bool enabled) {
        set_wm_state_atom(atoms.NET_WM_STATE_FULLSCREEN, enabled);
    }

    bool has_fullscreen_state() {
        auto states = xconn.get_atom_list_property(id, atoms.NET_WM_STATE);
        return std::find(states.begin(), states.end(), atoms.NET_WM_STATE_FULLSCREEN) != states.end();
    }

    void set_frame_extents(uint32_t bw) {
        if (atoms.NET_FRAME_EXTENTS != XCB_ATOM_NONE) {
            uint32_t extents[4] = { bw, bw, bw, bw };
            xconn.set_property(id, atoms.NET_FRAME_EXTENTS, XCB_ATOM_CARDINAL, extents, 4);
        }
    }

    void set_desktop(uint32_t ws) {
        if (atoms.NET_WM_DESKTOP != XCB_ATOM_NONE)
            xconn.set_property(id, atoms.NET_WM_DESKTOP, XCB_ATOM_CARDINAL, ws);
    }

    bool supports_delete() {
        for (auto atom : xconn.get_atom_list_property(id, atoms.WM_PROTOCOLS))
            if (atom == atoms.WM_DELETE_WINDOW) return true;
        return false;
    }

    void send_delete_message() {
        xcb_client_message_event_t ev = {};
        ev.response_type  = XCB_CLIENT_MESSAGE;
        ev.window         = id;
        ev.format         = 32;
        ev.type           = atoms.WM_PROTOCOLS;
        ev.data.data32[0] = atoms.WM_DELETE_WINDOW;
        ev.data.data32[1] = XCB_CURRENT_TIME;
        xconn.send_event(id, XCB_EVENT_MASK_NO_EVENT, (char*)&ev);
    }

    void send_take_focus(xcb_timestamp_t timestamp) {
        if (atoms.WM_TAKE_FOCUS == XCB_ATOM_NONE)
            return;
        for (auto atom : xconn.get_atom_list_property(id, atoms.WM_PROTOCOLS)) {
            if (atom != atoms.WM_TAKE_FOCUS)
                continue;
            xcb_client_message_event_t ev = {};
            ev.response_type  = XCB_CLIENT_MESSAGE;
            ev.window         = id;
            ev.format         = 32;
            ev.type           = atoms.WM_PROTOCOLS;
            ev.data.data32[0] = atoms.WM_TAKE_FOCUS;
            ev.data.data32[1] = timestamp;
            xconn.send_event(id, XCB_EVENT_MASK_NO_EVENT, (char*)&ev);
            break;
        }
    }

    void focus(xcb_timestamp_t timestamp) {
        if (!no_input_focus)
            xconn.set_input_focus(id, timestamp);
        send_take_focus(timestamp);
    }

    void note_wm_unmap() { pending_wm_unmaps += 2; }

    bool consume_wm_unmap() {
        if (pending_wm_unmaps <= 0)
            return false;
        --pending_wm_unmaps;
        return true;
    }

    void map()   { xconn.map_window(id); }
    void unmap() { xconn.unmap_window(id); }
    void raise() {
        uint32_t mode = XCB_STACK_MODE_ABOVE;
        xconn.configure_window(id, XCB_CONFIG_WINDOW_STACK_MODE, &mode);
    }
    void lower() {
        uint32_t mode = XCB_STACK_MODE_BELOW;
        xconn.configure_window(id, XCB_CONFIG_WINDOW_STACK_MODE, &mode);
    }
    void kill() { xconn.kill_client(id); }

    void send_expose() {
        auto               geom = xconn.get_window_geometry(id);
        xcb_expose_event_t ex   = {};
        ex.response_type = XCB_EXPOSE;
        ex.window        = id;
        ex.width         = geom ? geom->width  : 1;
        ex.height        = geom ? geom->height : 1;
        xconn.send_event(id, XCB_EVENT_MASK_EXPOSURE, (const char*)&ex);
    }

    void configure(uint16_t mask, const uint32_t* values) {
        xconn.configure_window(id, mask, values);
    }

    void send_configure_notify(int x, int y, int w, int h, int bw) {
        xcb_configure_notify_event_t ce = {};
        ce.response_type     = XCB_CONFIGURE_NOTIFY;
        ce.event             = id;
        ce.window            = id;
        ce.x                 = (int16_t)x;
        ce.y                 = (int16_t)y;
        ce.width             = (uint16_t)w;
        ce.height            = (uint16_t)h;
        ce.border_width      = (uint16_t)bw;
        ce.above_sibling     = XCB_WINDOW_NONE;
        ce.override_redirect = 0;
        xconn.send_event(id, XCB_EVENT_MASK_STRUCTURE_NOTIFY, (char*)&ce);
    }
};
