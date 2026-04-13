// Keep Xlib includes isolated in this translation unit.
#include <X11/XKBlib.h>
#include <X11/Xlib-xcb.h>
#include <X11/Xlib.h>

#include <xcb/xconnection.hpp>

#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>

namespace xcb {

XConnection::XConnection() {
    dpy_ = XOpenDisplay(nullptr);
    if (!dpy_) {
        std::fprintf(stderr, "sirenwm: cannot open X display\n");
        std::exit(1);
    }
    conn_ = XGetXCBConnection(dpy_);

    int xkb_opcode = 0;
    int xkb_event = 0;
    int xkb_error = 0;
    int xkb_major = XkbMajorVersion;
    int xkb_minor = XkbMinorVersion;
    if (XkbQueryExtension(dpy_, &xkb_opcode, &xkb_event, &xkb_error, &xkb_major, &xkb_minor)) {
        XkbSelectEvents(dpy_, XkbUseCoreKbd, XkbStateNotifyMask, XkbStateNotifyMask);
        xkb_event_type_ = xkb_event;
    }

    XSetEventQueueOwner(dpy_, XCBOwnsEventQueue);

    int xfd = xcb_get_file_descriptor(conn_);
    if (xfd >= 0)
        fcntl(xfd, F_SETFD, FD_CLOEXEC);

    int  screen_idx = DefaultScreen(dpy_);
    auto it         = xcb_setup_roots_iterator(xcb_get_setup(conn_));
    for (int i = 0; i < screen_idx; ++i)
        xcb_screen_next(&it);
    screen_ = it.data;
}

XConnection::~XConnection() {
    shutdown();
}

void XConnection::shutdown() {
    if (!dpy_) return;
    XCloseDisplay(dpy_);
    dpy_   = nullptr;
    conn_  = nullptr;
    screen_ = nullptr;
    dirty_ = false;
}

xcb_atom_t XConnection::get_atom_property(xcb_window_t win, xcb_atom_t prop) const {
    return xcb::get_atom_property(conn_, win, prop);
}

std::vector<xcb_atom_t> XConnection::get_atom_list_property(xcb_window_t win, xcb_atom_t prop) const {
    return xcb::get_atom_list_property(conn_, win, prop);
}

std::pair<std::string, std::string> XConnection::get_wm_class(xcb_window_t win) const {
    return xcb::get_wm_class(conn_, win);
}

std::string XConnection::get_text_property(xcb_window_t win, xcb_atom_t prop, xcb_atom_t type) const {
    return xcb::get_text_property(conn_, win, prop, type);
}

void XConnection::set_property(xcb_window_t win, xcb_atom_t prop, xcb_atom_t type,
    const xcb_atom_t* data, int count) {
    xcb::set_property(conn_, win, prop, type, data, count);
    dirty_ = true;
}

void XConnection::set_property(xcb_window_t win, xcb_atom_t prop,
    const xcb_window_t* data, int count) {
    xcb::set_property(conn_, win, prop, data, count);
    dirty_ = true;
}

void XConnection::set_property(xcb_window_t win, xcb_atom_t prop,
    xcb_atom_t type, uint32_t value) {
    xcb::set_property(conn_, win, prop, type, value);
    dirty_ = true;
}

void XConnection::set_property(xcb_window_t win, xcb_atom_t prop,
    xcb_atom_t type, const std::string& str) {
    xcb::set_property(conn_, win, prop, type, str);
    dirty_ = true;
}

bool XConnection::has_fixed_size_hints(xcb_window_t win) const {
    return get_size_hints(win).fixed;
}

std::unordered_map<std::string, xcb_atom_t>
XConnection::intern_atoms(std::initializer_list<const char*> names) const {
    return xcb::intern_batch(conn_, names);
}

} // namespace xcb
