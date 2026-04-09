// Include Xlib before anything else — defines Display, Window typedef etc.
// This file is the ONLY place in the codebase where Xlib.h is included.
#include <X11/Xlib.h>
#include <X11/Xlib-xcb.h>
#include <X11/XKBlib.h>

#include <xconn.hpp>
#include <xcb/randr.hpp>
#include <xcb/property.hpp>
#include <xcb/atom.hpp>
#include <monitor.hpp>

#include <fcntl.h>
#include <unistd.h>

XConnection::XConnection() {
    dpy = XOpenDisplay(nullptr);
    if (!dpy) {
        LOG_ERR("Cannot open display");
        exit(1);
    }
    conn = XGetXCBConnection(dpy);

    // Subscribe to XKB state-change events before handing the event queue to XCB.
    // XkbSelectEvents must be called while Xlib still owns the queue.
    int xkb_opcode = 0, xkb_event = 0, xkb_error = 0, xkb_major = XkbMajorVersion, xkb_minor = XkbMinorVersion;
    if (XkbQueryExtension(dpy, &xkb_opcode, &xkb_event, &xkb_error, &xkb_major, &xkb_minor)) {
        XkbSelectEvents(dpy, XkbUseCoreKbd, XkbStateNotifyMask, XkbStateNotifyMask);
        xkb_event_type_ = xkb_event;
    }

    XSetEventQueueOwner(dpy, XCBOwnsEventQueue);

    // Mark the X connection fd FD_CLOEXEC so it is not inherited by child
    // processes spawned via fork+exec (autostart, etc.) and so it is closed
    // automatically after execv during exec-restart, without an explicit
    // XCloseDisplay call that would disconnect the X server immediately.
    int xfd = xcb_get_file_descriptor(conn);
    if (xfd >= 0)
        fcntl(xfd, F_SETFD, FD_CLOEXEC);

    int  screen_idx = DefaultScreen(dpy);
    auto it         = xcb_setup_roots_iterator(xcb_get_setup(conn));
    for (int i = 0; i < screen_idx; ++i)
        xcb_screen_next(&it);
    screen = it.data;
}

XConnection::~XConnection() {
    shutdown();
}

void XConnection::shutdown() {
    if (!dpy)
        return;
    XCloseDisplay(dpy);
    dpy    = nullptr;
    conn   = nullptr;
    screen = nullptr;
    dirty  = false;
}

xcb_atom_t XConnection::get_atom_property(xcb_window_t win, xcb_atom_t prop) const {
    return xcb::get_atom_property(conn, win, prop);
}

std::vector<xcb_atom_t> XConnection::get_atom_list_property(xcb_window_t win, xcb_atom_t prop) const {
    return xcb::get_atom_list_property(conn, win, prop);
}

std::pair<std::string,std::string> XConnection::get_wm_class(xcb_window_t win) const {
    return xcb::get_wm_class(conn, win);
}

std::string XConnection::get_text_property(xcb_window_t win, xcb_atom_t prop, xcb_atom_t type) const {
    return xcb::get_text_property(conn, win, prop, type);
}

void XConnection::set_property(xcb_window_t win, xcb_atom_t prop, xcb_atom_t type,
    const xcb_atom_t* data, int count) {
    xcb::set_property(conn, win, prop, type, data, count);
    dirty = true;
}

void XConnection::set_property(xcb_window_t win, xcb_atom_t prop,
    const xcb_window_t* data, int count) {
    xcb::set_property(conn, win, prop, data, count);
    dirty = true;
}

void XConnection::set_property(xcb_window_t win, xcb_atom_t prop,
    xcb_atom_t type, uint32_t value) {
    xcb::set_property(conn, win, prop, type, value);
    dirty = true;
}

void XConnection::set_property(xcb_window_t win, xcb_atom_t prop,
    xcb_atom_t type, const std::string& str) {
    xcb::set_property(conn, win, prop, type, str);
    dirty = true;
}

std::vector<xcb_window_t> XConnection::query_tree_children(xcb_window_t parent) const {
    std::vector<xcb_window_t> out;
    auto                      cookie = xcb_query_tree(conn, parent);
    auto*                     tree   = xcb_query_tree_reply(conn, cookie, nullptr);
    if (!tree)
        return out;

    int   n        = xcb_query_tree_children_length(tree);
    auto* children = xcb_query_tree_children(tree);
    out.assign(children, children + n);
    free(tree);
    return out;
}

XConnection::WindowAttributes XConnection::get_window_attributes(xcb_window_t win) const {
    WindowAttributes out {};
    auto             cookie = xcb_get_window_attributes(conn, win);
    auto*            reply  = xcb_get_window_attributes_reply(conn, cookie, nullptr);
    if (!reply)
        return out;

    out.valid             = true;
    out.override_redirect = reply->override_redirect;
    out.map_state         = reply->map_state;
    out.win_class         = reply->_class;
    out.your_event_mask   = reply->your_event_mask;
    free(reply);
    return out;
}

std::optional<XConnection::Geometry> XConnection::get_window_geometry(xcb_window_t win) const {
    auto  cookie = xcb_get_geometry(conn, win);
    auto* reply  = xcb_get_geometry_reply(conn, cookie, nullptr);
    if (!reply)
        return std::nullopt;

    Geometry g;
    g.x            = reply->x;
    g.y            = reply->y;
    g.width        = reply->width;
    g.height       = reply->height;
    g.border_width = reply->border_width;
    free(reply);
    return g;
}

std::optional<xcb_window_t> XConnection::get_transient_for_window(xcb_window_t win) const {
    auto  cookie = xcb_get_property(conn, 0, win, XCB_ATOM_WM_TRANSIENT_FOR,
            XCB_ATOM_WINDOW, 0, 1);
    auto* reply = xcb_get_property_reply(conn, cookie, nullptr);
    if (!reply)
        return std::nullopt;

    std::optional<xcb_window_t> parent = std::nullopt;
    if (reply->format == 32 && reply->value_len >= 1) {
        auto* data = static_cast<xcb_window_t*>(xcb_get_property_value(reply));
        if (data)
            parent = data[0];
    }
    free(reply);
    return parent;
}

int XConnection::get_wm_state_value(xcb_window_t win, xcb_atom_t wm_state_atom) const {
    if (wm_state_atom == XCB_ATOM_NONE)
        return -1;

    auto  cookie = xcb_get_property(conn, 0, win, wm_state_atom, XCB_ATOM_ANY, 0, 2);
    auto* reply  = xcb_get_property_reply(conn, cookie, nullptr);
    if (!reply)
        return -1;

    int state = -1;
    if (reply->format == 32 && reply->value_len >= 1) {
        auto* data = static_cast<uint32_t*>(xcb_get_property_value(reply));
        if (data)
            state = static_cast<int>(data[0]);
    }
    free(reply);
    return state;
}

bool XConnection::has_static_gravity(xcb_window_t win) const {
    auto  cookie = xcb_get_property(conn, 0, win, XCB_ATOM_WM_NORMAL_HINTS,
            XCB_ATOM_ANY, 0, 18);
    auto* reply = xcb_get_property_reply(conn, cookie, nullptr);
    if (!reply)
        return false;
    bool result = false;
    if (reply->format == 32 && reply->value_len >= 1) {
        auto* data = static_cast<uint32_t*>(xcb_get_property_value(reply));
        // WM_SIZE_HINTS: flags at data[0], win_gravity at data[9]
        // PWinGravity flag = bit 9 (0x200); StaticGravity = 10
        constexpr uint32_t P_WIN_GRAVITY  = (1u << 9);
        constexpr uint32_t STATIC_GRAVITY = 10u;
        if (reply->value_len >= 10 && (data[0] & P_WIN_GRAVITY))
            result = (data[9] == STATIC_GRAVITY);
    }
    free(reply);
    return result;
}

bool XConnection::motif_no_decorations(xcb_window_t win) const {
    auto       atoms = intern_atoms({ "_MOTIF_WM_HINTS" });
    xcb_atom_t atom  = atoms["_MOTIF_WM_HINTS"];
    if (atom == XCB_ATOM_NONE)
        return false;
    auto  cookie = xcb_get_property(conn, 0, win, atom, XCB_ATOM_ANY, 0, 5);
    auto* reply  = xcb_get_property_reply(conn, cookie, nullptr);
    if (!reply)
        return false;
    bool result = false;
    if (reply->format == 32 && reply->value_len >= 3) {
        auto* data = static_cast<uint32_t*>(xcb_get_property_value(reply));
        // flags bit 1 (0x2) = MWM_HINTS_DECORATIONS; decorations=0 means no frame
        result = (data[0] & 0x2u) && (data[2] == 0u);
    }
    free(reply);
    return result;
}

XConnection::SizeHints XConnection::get_size_hints(xcb_window_t win) const {
    // WM_NORMAL_HINTS layout (ICCCM §4.1.2.3), 18 x uint32:
    //   [0]  flags
    //   [1-4] obsolete (x,y,w,h)
    //   [5]  min_width  [6]  min_height   (PMinSize  = 1<<4)
    //   [7]  max_width  [8]  max_height   (PMaxSize  = 1<<5)
    //   [9]  width_inc  [10] height_inc   (PResizeInc= 1<<6)
    //   [11-14] min/max aspect
    //   [15] base_width [16] base_height  (PBaseSize = 1<<8)
    //   [17] win_gravity                  (PWinGravity=1<<9)
    auto  cookie = xcb_get_property(conn, 0, win, XCB_ATOM_WM_NORMAL_HINTS,
            XCB_ATOM_ANY, 0, 18);
    auto* reply = xcb_get_property_reply(conn, cookie, nullptr);
    if (!reply)
        return {};

    SizeHints out;
    if (reply->format == 32 && reply->value_len >= 9) {
        auto* d = static_cast<int32_t*>(xcb_get_property_value(reply));
        if (d) {
            constexpr uint32_t PMinSize   = (1u << 4);
            constexpr uint32_t PMaxSize   = (1u << 5);
            constexpr uint32_t PResizeInc = (1u << 6);
            constexpr uint32_t PBaseSize  = (1u << 8);
            uint32_t           flags      = (uint32_t)d[0];

            if (flags & PMinSize) {
                out.has_min = true;
                out.min_w   = d[5]; out.min_h = d[6];
            }
            if (flags & PMaxSize) {
                out.has_max = true;
                out.max_w   = d[7]; out.max_h = d[8];
            }
            if (flags & PResizeInc && reply->value_len >= 11) {
                out.has_inc = true;
                out.inc_w   = d[9]; out.inc_h = d[10];
            }
            if (flags & PBaseSize && reply->value_len >= 17) {
                out.has_base = true;
                out.base_w   = d[15]; out.base_h = d[16];
            }
            out.fixed = out.has_min && out.has_max &&
                out.min_w > 0 && out.min_h > 0 &&
                out.min_w == out.max_w && out.min_h == out.max_h;
        }
    }
    free(reply);
    return out;
}

bool XConnection::has_fixed_size_hints(xcb_window_t win) const {
    return get_size_hints(win).fixed;
}

XConnection::WmHints XConnection::get_wm_hints(xcb_window_t win) const {
    // WM_HINTS layout (ICCCM §4.1.2.4):
    //   data[0] = flags bitmask
    //   data[1] = input (0=False, 1=True)  — present when InputHint (1<<0) set
    // UrgencyHint = (1<<8)
    auto  cookie = xcb_get_property(conn, 0, win, XCB_ATOM_WM_HINTS, XCB_ATOM_ANY, 0, 9);
    auto* reply  = xcb_get_property_reply(conn, cookie, nullptr);
    if (!reply)
        return {};
    WmHints out;
    if (reply->format == 32 && reply->value_len >= 2) {
        auto*              data        = static_cast<uint32_t*>(xcb_get_property_value(reply));
        constexpr uint32_t InputHint   = (1u << 0);
        constexpr uint32_t UrgencyHint = (1u << 8);
        if (data) {
            if (data[0] & InputHint)
                out.no_input = (data[1] == 0);
            out.urgent = (data[0] & UrgencyHint) != 0;
        }
    }
    free(reply);
    return out;
}

bool XConnection::get_wm_hints_no_input(xcb_window_t win) const {
    return get_wm_hints(win).no_input;
}

bool XConnection::has_property_32(xcb_window_t win, xcb_atom_t prop, uint32_t min_items) const {
    if (prop == XCB_ATOM_NONE)
        return false;
    auto  cookie = xcb_get_property(conn, 0, win, prop, XCB_ATOM_ANY, 0, min_items);
    auto* reply  = xcb_get_property_reply(conn, cookie, nullptr);
    if (!reply)
        return false;
    bool ok = (reply->format == 32 && reply->value_len >= min_items);
    free(reply);
    return ok;
}

xcb_cursor_t XConnection::create_left_ptr_cursor() {
    // Load X11 "cursor" font and create XC_left_ptr (glyph 68).
    xcb_font_t font = xcb_generate_id(conn);
    xcb_open_font(conn, font, 6, "cursor");

    xcb_cursor_t cursor = xcb_generate_id(conn);
    xcb_create_glyph_cursor(conn, cursor, font, font,
        68, 69,
        0, 0, 0,
        0xffff, 0xffff, 0xffff);
    xcb_close_font(conn, font);
    dirty = true;
    return cursor;
}

void XConnection::free_cursor(xcb_cursor_t cursor) {
    xcb_free_cursor(conn, cursor);
    dirty = true;
}

std::unordered_map<std::string, xcb_atom_t>
XConnection::intern_atoms(std::initializer_list<const char*> names) const {
    return xcb::intern_batch(conn, names);
}

std::vector<Monitor> XConnection::get_monitors() const {
    xcb::Screen s(conn, screen->root);
    auto        monitors = s.monitors();
    if (monitors.empty()) {
        LOG_WARN("RandR get_monitors returned nothing, falling back to screen size");
        monitors.emplace_back(0, "default", 0, 0,
            screen->width_in_pixels, screen->height_in_pixels);
    }
    return monitors;
}
