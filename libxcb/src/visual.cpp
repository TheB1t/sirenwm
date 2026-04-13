#include <xcb/visual.hpp>

namespace xcb {

xcb_visualtype_t* find_visual(xcb_screen_t* screen, xcb_visualid_t visual_id) {
    auto depth_iter = xcb_screen_allowed_depths_iterator(screen);
    for (; depth_iter.rem; xcb_depth_next(&depth_iter)) {
        auto visual_iter = xcb_depth_visuals_iterator(depth_iter.data);
        for (; visual_iter.rem; xcb_visualtype_next(&visual_iter)) {
            if (visual_iter.data->visual_id == visual_id)
                return visual_iter.data;
        }
    }
    return nullptr;
}

} // namespace xcb
