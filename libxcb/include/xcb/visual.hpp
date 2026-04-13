#pragma once

#include <xcb/xcb.h>

namespace xcb {

xcb_visualtype_t* find_visual(xcb_screen_t* screen, xcb_visualid_t visual_id);

} // namespace xcb
