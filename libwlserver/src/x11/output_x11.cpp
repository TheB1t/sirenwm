#include <wl/server/x11/output_x11.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

wl::server::SurfaceId wl_surface_id_for_surface(uint32_t surface_id, DisplayState& state) {
    auto* surf = state.surface(surface_id);
    if (!surf) return {};
    return surf->wl_surface_id;
}

} // namespace

OutputX11::OutputX11(int width, int height)
    : width_(width), height_(height) {
    conn_ = xcb_connect(nullptr, nullptr);
    if (!conn_ || xcb_connection_has_error(conn_)) {
        fprintf(stderr, "output_x11: failed to connect to X11\n");
        return;
    }
    owns_connection_ = true;

    screen_ = xcb_setup_roots_iterator(xcb_get_setup(conn_)).data;
    visual_ = xcb::find_visual(screen_, screen_->root_visual);
    if (!visual_) {
        fprintf(stderr, "output_x11: no suitable visual\n");
        return;
    }

    window_ = generate_id();
    uint32_t mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
    uint32_t values[] = {
        screen_->black_pixel,
        XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_STRUCTURE_NOTIFY |
        XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE |
        XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE |
        XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_ENTER_WINDOW |
        XCB_EVENT_MASK_LEAVE_WINDOW | XCB_EVENT_MASK_FOCUS_CHANGE
    };

    create_window(window_, screen_->root,
                  0, 0, static_cast<uint16_t>(width_), static_cast<uint16_t>(height_),
                  XCB_WINDOW_CLASS_INPUT_OUTPUT, screen_->root_visual, mask, values);

    const char* title = "sirenwm-wayland-display-server";
    change_property(window_, XCB_ATOM_WM_NAME, XCB_ATOM_STRING,
                    8, static_cast<uint32_t>(strlen(title)), title);

    setup_wm_delete();

    default_cursor_   = create_left_ptr_cursor();
    invisible_cursor_ = create_invisible_cursor();
    uint32_t cursor_val = default_cursor_;
    change_window_attributes(window_, XCB_CW_CURSOR, &cursor_val);

    map_window(window_);
    flush();

    cairo_surface_ = cairo_xcb_surface_create(conn_, window_, visual_,
                                               width_, height_);
    create_back_buffer();
}

OutputX11::~OutputX11() {
    if (conn_) {
        if (default_cursor_ != XCB_CURSOR_NONE)
            free_cursor(default_cursor_);
        if (invisible_cursor_ != XCB_CURSOR_NONE)
            free_cursor(invisible_cursor_);
    }
    if (back_surface_) cairo_surface_destroy(back_surface_);
    if (back_pixmap_ && conn_) xcb_free_pixmap(conn_, back_pixmap_);
    if (cairo_surface_) cairo_surface_destroy(cairo_surface_);
    if (conn_ && window_)
        destroy_window(window_);
}

void OutputX11::set_cursor_surface(wl::server::SurfaceId sid,
                                    int32_t hotspot_x, int32_t hotspot_y) {
    cursor_surface_   = sid;
    cursor_hotspot_x_ = hotspot_x;
    cursor_hotspot_y_ = hotspot_y;

    uint32_t cursor = default_cursor_;
    if (cursor_surface_ && invisible_cursor_ != XCB_CURSOR_NONE)
        cursor = invisible_cursor_;

    change_window_attributes(window_, XCB_CW_CURSOR, &cursor);
    repaint_needed_ = true;
}

void OutputX11::create_back_buffer() {
    if (back_surface_) cairo_surface_destroy(back_surface_);
    if (back_pixmap_ && conn_) xcb_free_pixmap(conn_, back_pixmap_);

    back_pixmap_ = generate_id();
    xcb_create_pixmap(conn_, screen_->root_depth, back_pixmap_,
                      window_, static_cast<uint16_t>(width_),
                      static_cast<uint16_t>(height_));
    back_surface_ = cairo_xcb_surface_create(conn_, back_pixmap_, visual_,
                                              width_, height_);
}

void OutputX11::setup_wm_delete() {
    auto atoms = xcb::intern_batch(conn_, {"WM_PROTOCOLS", "WM_DELETE_WINDOW"});
    auto protocols = atoms["WM_PROTOCOLS"];
    wm_delete_window_ = atoms["WM_DELETE_WINDOW"];
    if (protocols != XCB_ATOM_NONE && wm_delete_window_ != XCB_ATOM_NONE) {
        change_property(window_, protocols, XCB_ATOM_ATOM, 32, 1, &wm_delete_window_);
    }
}

bool OutputX11::handle_client_message(xcb_client_message_event_t* ev) {
    return ev->data.data32[0] != wm_delete_window_;
}

void OutputX11::handle_configure_notify(xcb_configure_notify_event_t* ev) {
    if (ev->width != width_ || ev->height != height_) {
        width_  = ev->width;
        height_ = ev->height;
        cairo_xcb_surface_set_size(cairo_surface_, width_, height_);
        create_back_buffer();
        repaint_needed_ = true;
    }
}

void OutputX11::handle_key_press(xcb_key_press_event_t* ev, DisplayState& state, wl::server::Seat& seat) {
    uint32_t evdev_key = ev->detail - 8;
    uint32_t keysym = seat.resolve_keysym(evdev_key);
    seat.update_xkb_state(evdev_key, true);
    if (state.is_intercepted(keysym, ev->state)) {
        intercepted_keys_.insert(evdev_key);
        state.key_press(evdev_key, keysym, ev->state);
    } else {
        seat.send_key(ev->time, evdev_key, true);
    }
    seat.send_current_modifiers();
}

void OutputX11::handle_key_release(xcb_key_release_event_t* ev, wl::server::Seat& seat) {
    uint32_t evdev_key = ev->detail - 8;
    seat.update_xkb_state(evdev_key, false);
    if (!intercepted_keys_.erase(evdev_key))
        seat.send_key(ev->time, evdev_key, false);
    seat.send_current_modifiers();
}

void OutputX11::handle_button_press(xcb_button_press_event_t* ev, DisplayState& state, wl::server::Seat& seat) {
    const int32_t px = ev->root_x;
    const int32_t py = ev->root_y;
    pointer_x_ = px;
    pointer_y_ = py;
    pointer_valid_ = true;
    auto* ov = state.overlay_manager().overlay_at(px, py);
    uint32_t button = 0x110 + ev->detail - 1;
    if (ov) {
        state.overlay_button(ov->id, px - ov->x, py - ov->y, button, false);
        return;
    }
    uint32_t sid = pointer_grabbed_ ? pointer_surface_
                                    : state.surface_at(px, py);
    if (!pointer_grabbed_ && sid != 0 && sid != pointer_surface_) {
        pointer_surface_ = sid;
        state.pointer_enter(sid);
        auto surface = wl_surface_id_for_surface(sid, state);
        if (surface) {
            auto* surf = state.surface(sid);
            seat.send_pointer_enter(surface,
                                    surf ? px - surf->x : 0,
                                    surf ? py - surf->y : 0);
        }
    }
    state.button_press(sid, px, py, button, ev->state, false);
    if (!pointer_grabbed_)
        seat.send_pointer_button(ev->time, button, true);
}

void OutputX11::handle_button_release(xcb_button_release_event_t* ev, DisplayState& state, wl::server::Seat& seat) {
    const int32_t px = ev->root_x;
    const int32_t py = ev->root_y;
    pointer_x_ = px;
    pointer_y_ = py;
    pointer_valid_ = true;
    auto* ov = state.overlay_manager().overlay_at(px, py);
    uint32_t button = 0x110 + ev->detail - 1;
    if (ov) {
        state.overlay_button(ov->id, px - ov->x, py - ov->y, button, true);
        return;
    }
    uint32_t sid = pointer_grabbed_ ? pointer_surface_
                                    : state.surface_at(px, py);
    state.button_press(sid, px, py, button, ev->state, true);
    if (!pointer_grabbed_)
        seat.send_pointer_button(ev->time, button, false);
}

void OutputX11::handle_motion_notify(xcb_motion_notify_event_t* ev,
                                      DisplayState& state, wl::server::Seat& seat, wl::server::XdgShell&) {
    const int32_t px = ev->root_x;
    const int32_t py = ev->root_y;
    pointer_x_ = px;
    pointer_y_ = py;
    pointer_valid_ = true;
    const bool has_sw_cursor = static_cast<bool>(cursor_surface_);
    if (pointer_grabbed_) {
        state.pointer_motion(pointer_surface_, px, py, ev->state);
        if (has_sw_cursor)
            repaint_needed_ = true;
        return;
    }

    uint32_t sid = state.surface_at(px, py);
    if (sid != pointer_surface_) {
        uint32_t old_sid = pointer_surface_;
        pointer_surface_ = sid;

        if (old_sid != 0) {
            auto old_surface = wl_surface_id_for_surface(old_sid, state);
            if (old_surface)
                seat.send_pointer_leave(old_surface);
        }
        if (sid != 0) {
            state.pointer_enter(sid);
            auto surface = wl_surface_id_for_surface(sid, state);
            if (surface) {
                auto* surf = state.surface(sid);
                seat.send_pointer_enter(surface,
                                        surf ? px - surf->x : 0,
                                        surf ? py - surf->y : 0);
            }
        }
    }

    state.pointer_motion(sid, px, py, ev->state);
    auto* surf = state.surface(sid);
    if (surf)
        seat.send_pointer_motion(ev->time,
                                 px - surf->x,
                                 py - surf->y);
    if (has_sw_cursor)
        repaint_needed_ = true;
}

bool OutputX11::pump_events(DisplayState& state, wl::server::Seat& seat, wl::server::XdgShell& xdg_shell) {
    xcb_generic_event_t* ev;
    while ((ev = poll_event())) {
        uint8_t type = ev->response_type & ~0x80;
        switch (type) {
        case XCB_CLIENT_MESSAGE:
            if (!handle_client_message(reinterpret_cast<xcb_client_message_event_t*>(ev))) {
                free(ev);
                return false;
            }
            break;
        case XCB_CONFIGURE_NOTIFY:
            handle_configure_notify(reinterpret_cast<xcb_configure_notify_event_t*>(ev));
            break;
        case XCB_KEY_PRESS:
            handle_key_press(reinterpret_cast<xcb_key_press_event_t*>(ev), state, seat);
            break;
        case XCB_KEY_RELEASE:
            handle_key_release(reinterpret_cast<xcb_key_release_event_t*>(ev), seat);
            break;
        case XCB_BUTTON_PRESS:
            handle_button_press(reinterpret_cast<xcb_button_press_event_t*>(ev), state, seat);
            break;
        case XCB_BUTTON_RELEASE:
            handle_button_release(reinterpret_cast<xcb_button_release_event_t*>(ev), state, seat);
            break;
        case XCB_MOTION_NOTIFY:
            handle_motion_notify(reinterpret_cast<xcb_motion_notify_event_t*>(ev), state, seat, xdg_shell);
            break;
        case XCB_EXPOSE:
            repaint_needed_ = true;
            break;
        default:
            break;
        }
        free(ev);
    }
    return true;
}

void OutputX11::repaint(wl::server::Compositor& compositor, wl::server::XdgShell&, DisplayState& state) {
    if (!cairo_surface_ || !back_surface_ || !repaint_needed_) return;
    repaint_needed_ = false;

    auto* cr = cairo_create(back_surface_);

    cairo_set_source_rgb(cr, 0.15, 0.15, 0.18);
    cairo_paint(cr);

    for (auto* surf : state.visible_surfaces_by_stacking()) {
        auto surface = wl_surface_id_for_surface(surf->id, state);
        if (!surface) continue;

        auto bv = compositor.buffer_view(surface);
        if (!bv.data || bv.width <= 0 || bv.height <= 0 || bv.stride <= 0) continue;

        cairo_format_t fmt = CAIRO_FORMAT_ARGB32;
        if (bv.format == 1)
            fmt = CAIRO_FORMAT_RGB24;

        auto* img = cairo_image_surface_create_for_data(
            static_cast<unsigned char*>(bv.data),
            fmt, bv.width, bv.height, bv.stride);

        int draw_w = surf->width  > 0 ? surf->width  : bv.width;
        int draw_h = surf->height > 0 ? surf->height : bv.height;

        if (surf->border_width > 0) {
            uint32_t c = surf->border_color;
            double a = ((c >> 24) & 0xFF) / 255.0;
            double r = ((c >> 16) & 0xFF) / 255.0;
            double g = ((c >>  8) & 0xFF) / 255.0;
            double b = ((c      ) & 0xFF) / 255.0;
            int bw = static_cast<int>(surf->border_width);
            cairo_set_source_rgba(cr, r, g, b, a);
            cairo_rectangle(cr, surf->x - bw, surf->y - bw,
                            draw_w + 2 * bw, draw_h + 2 * bw);
            cairo_fill(cr);
        }

        cairo_set_source_surface(cr, img, surf->x, surf->y);
        cairo_paint(cr);
        cairo_surface_destroy(img);
    }

    for (auto& [id, ov] : state.overlay_manager().overlays()) {
        if (!ov.visible || ov.pixels.empty()) continue;
        auto* img = cairo_image_surface_create_for_data(
            const_cast<unsigned char*>(ov.pixels.data()),
            CAIRO_FORMAT_ARGB32, ov.width, ov.height, ov.width * 4);
        cairo_set_source_surface(cr, img, ov.x, ov.y);
        cairo_paint(cr);
        cairo_surface_destroy(img);
    }

    if (pointer_valid_ && cursor_surface_) {
        auto bv = compositor.buffer_view(cursor_surface_);
        if (bv.data && bv.width > 0 && bv.height > 0 && bv.stride > 0) {
            cairo_format_t fmt = CAIRO_FORMAT_ARGB32;
            if (bv.format == 1)
                fmt = CAIRO_FORMAT_RGB24;
            auto* img = cairo_image_surface_create_for_data(
                static_cast<unsigned char*>(bv.data),
                fmt, bv.width, bv.height, bv.stride);
            cairo_set_source_surface(cr, img,
                                     pointer_x_ - cursor_hotspot_x_,
                                     pointer_y_ - cursor_hotspot_y_);
            cairo_paint(cr);
            cairo_surface_destroy(img);
        }
    }

    cairo_destroy(cr);
    cairo_surface_flush(back_surface_);

    auto* front = cairo_create(cairo_surface_);
    cairo_set_source_surface(front, back_surface_, 0, 0);
    cairo_paint(front);
    cairo_destroy(front);
    cairo_surface_flush(cairo_surface_);
    flush();
}
