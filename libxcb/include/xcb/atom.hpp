#pragma once

#include <xcb/xcb.h>
#include <xcb/reply.hpp>
#include <string>
#include <unordered_map>
#include <vector>
#include <cstring>

namespace xcb {

// Lazy-interned X atom. Resolved on first use, cached for the lifetime of the connection.
// Usage:
//   xcb::Atom NET_WM_NAME(conn, "_NET_WM_NAME");
//   xcb_atom_t raw = NET_WM_NAME;

class Atom {
    xcb_connection_t*  conn;
    const char*        name;
    mutable xcb_atom_t value    = XCB_ATOM_NONE;
    mutable bool       resolved = false;

    public:
        Atom(xcb_connection_t* c, const char* n) : conn(c), name(n) {}

        operator xcb_atom_t() const {
            if (!resolved) {
                auto r = xcb::reply(xcb_intern_atom_reply(conn,
                        xcb_intern_atom(conn, 0, (uint16_t)strlen(name), name), nullptr));
                value    = r ? r->atom : (xcb_atom_t)XCB_ATOM_NONE;
                resolved = true;
            }
            return value;
        }

        xcb_atom_t get() const { return operator xcb_atom_t(); }
};

// Intern a batch of atoms upfront (avoids round-trips by sending all cookies first).
// Returns a map from name → atom value.
inline std::unordered_map<std::string, xcb_atom_t>
intern_batch(xcb_connection_t* conn, const char* const* names, size_t count) {
    std::vector<xcb_intern_atom_cookie_t> cookies;
    cookies.reserve(count);
    for (size_t i = 0; i < count; i++)
        cookies.push_back(xcb_intern_atom(conn, 0, (uint16_t)strlen(names[i]), names[i]));

    std::unordered_map<std::string, xcb_atom_t> result;
    for (size_t i = 0; i < count; i++) {
        auto r = xcb::reply(xcb_intern_atom_reply(conn, cookies[i], nullptr));
        result[names[i]] = r ? r->atom : (xcb_atom_t)XCB_ATOM_NONE;
    }
    return result;
}

inline std::unordered_map<std::string, xcb_atom_t>
intern_batch(xcb_connection_t* conn, std::initializer_list<const char*> names) {
    return intern_batch(conn, names.begin(), names.size());
}

} // namespace xcb
