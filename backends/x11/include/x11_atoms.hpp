#pragma once

#include <xcb/xcb.h>
#include <algorithm>
#include <vector>

class XConnection;

inline bool has_atom(const std::vector<xcb_atom_t>& atoms, xcb_atom_t needle) {
    if (needle == XCB_ATOM_NONE)
        return false;
    return std::find(atoms.begin(), atoms.end(), needle) != atoms.end();
}

struct WindowTypeAtoms {
    xcb_atom_t net_wm_window_type = XCB_ATOM_NONE;
    xcb_atom_t dialog             = XCB_ATOM_NONE;
    xcb_atom_t utility            = XCB_ATOM_NONE;
    xcb_atom_t splash             = XCB_ATOM_NONE;
    xcb_atom_t modal              = XCB_ATOM_NONE;
};

const WindowTypeAtoms& window_type_atoms(XConnection& xconn);
