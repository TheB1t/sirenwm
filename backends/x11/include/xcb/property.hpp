#pragma once

#include <xcb/xcb.h>
#include <xcb/reply.hpp>
#include <string>
#include <vector>
#include <cstring>

namespace xcb {

// --- get ---

// Returns the first atom value of a property, or XCB_ATOM_NONE.
inline xcb_atom_t get_atom_property(xcb_connection_t* conn, xcb_window_t win,
    xcb_atom_t prop) {
    auto r = xcb::reply(xcb_get_property_reply(conn,
            xcb_get_property(conn, 0, win, prop, XCB_ATOM_ATOM, 0, 1), nullptr));
    if (!r || xcb_get_property_value_length(r.get()) == 0)
        return XCB_ATOM_NONE;
    return *static_cast<xcb_atom_t*>(xcb_get_property_value(r.get()));
}

// Returns all atom values of a property.
inline std::vector<xcb_atom_t> get_atom_list_property(xcb_connection_t* conn,
    xcb_window_t win,
    xcb_atom_t prop) {
    auto r = xcb::reply(xcb_get_property_reply(conn,
            xcb_get_property(conn, 0, win, prop, XCB_ATOM_ATOM, 0, 256), nullptr));
    if (!r) return {};
    int   n    = xcb_get_property_value_length(r.get()) / sizeof(xcb_atom_t);
    auto* data = static_cast<xcb_atom_t*>(xcb_get_property_value(r.get()));
    return { data, data + n };
}

// Returns WM_CLASS as { instance, class_name }.
inline std::pair<std::string, std::string> get_wm_class(xcb_connection_t* conn,
    xcb_window_t win) {
    auto r = xcb::reply(xcb_get_property_reply(conn,
            xcb_get_property(conn, 0, win, XCB_ATOM_WM_CLASS, XCB_ATOM_STRING, 0, 256), nullptr));
    if (!r) return {};

    int         len  = xcb_get_property_value_length(r.get());
    const char* data = static_cast<const char*>(xcb_get_property_value(r.get()));
    if (len == 0) return {};

    std::string instance(data);
    std::string cls;
    int         inst_len = (int)instance.size() + 1;
    if (inst_len < len)
        cls = std::string(data + inst_len);
    return { instance, cls };
}

inline std::string get_text_property(xcb_connection_t* conn, xcb_window_t win,
    xcb_atom_t prop, xcb_atom_t type = XCB_GET_PROPERTY_TYPE_ANY) {
    auto r = xcb::reply(xcb_get_property_reply(conn,
            xcb_get_property(conn, 0, win, prop, type, 0, 1024), nullptr));
    if (!r) return {};

    int len = xcb_get_property_value_length(r.get());
    if (len <= 0) return {};

    const char* data    = static_cast<const char*>(xcb_get_property_value(r.get()));
    int         str_len = 0;
    while (str_len < len && data[str_len] != '\0')
        ++str_len;
    return std::string(data, str_len);
}

// --- set ---

inline void set_property(xcb_connection_t* conn, xcb_window_t win,
    xcb_atom_t prop, xcb_atom_t type,
    const xcb_atom_t* data, int count) {
    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, win, prop, type, 32, count, data);
}

inline void set_property(xcb_connection_t* conn, xcb_window_t win,
    xcb_atom_t prop, const xcb_window_t* data, int count) {
    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, win, prop, XCB_ATOM_WINDOW, 32, count, data);
}

inline void set_property(xcb_connection_t* conn, xcb_window_t win,
    xcb_atom_t prop, xcb_atom_t type, uint32_t value) {
    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, win, prop, type, 32, 1, &value);
}

inline void set_property(xcb_connection_t* conn, xcb_window_t win,
    xcb_atom_t prop, xcb_atom_t type, const std::string& str) {
    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, win, prop, type, 8,
        (uint32_t)str.size(), str.c_str());
}

} // namespace xcb
