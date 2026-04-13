#pragma once

#include <xcb/xcb_keysyms.h>
#include <xcb/xconnection.hpp>

#include <vector>

struct Monitor;

class XConnection : public xcb::XConnection {
public:
    using xcb::XConnection::XConnection;

    xcb_key_symbols_t* alloc_key_symbols() const {
        return xcb_key_symbols_alloc(raw());
    }

    std::vector<Monitor> get_monitors() const;
};
