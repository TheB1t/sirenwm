#include <xcb/connection.hpp>

#include <cstdlib>
#include <utility>

namespace xcb {

Connection::Connection(xcb_connection_t* conn, xcb_screen_t* screen)
    : conn_(conn), screen_(screen) {}

xcb_screen_t* Connection::first_screen(xcb_connection_t* conn) {
    return xcb_setup_roots_iterator(xcb_get_setup(conn)).data;
}

Connection::~Connection() {
    if (owns_connection_ && conn_)
        xcb_disconnect(conn_);
}

Connection::Connection(Connection&& o) noexcept
    : conn_(o.conn_), screen_(o.screen_), dirty_(o.dirty_)
    , owns_connection_(o.owns_connection_) {
    o.conn_ = nullptr;
    o.screen_ = nullptr;
    o.owns_connection_ = false;
}

Connection& Connection::operator=(Connection&& o) noexcept {
    if (this != &o) {
        if (owns_connection_ && conn_)
            xcb_disconnect(conn_);
        conn_ = o.conn_;
        screen_ = o.screen_;
        dirty_ = o.dirty_;
        owns_connection_ = o.owns_connection_;
        o.conn_ = nullptr;
        o.screen_ = nullptr;
        o.owns_connection_ = false;
    }
    return *this;
}

int Connection::fd() const {
    return conn_ ? xcb_get_file_descriptor(conn_) : -1;
}

void Connection::flush() {
    if (dirty_ && conn_) {
        xcb_flush(conn_);
        dirty_ = false;
    }
}

xcb_generic_event_t* Connection::poll_event() {
    return conn_ ? xcb_poll_for_event(conn_) : nullptr;
}

xcb_window_t Connection::generate_id() {
    return xcb_generate_id(conn_);
}

// Window operations

void Connection::map_window(xcb_window_t win) {
    xcb_map_window(conn_, win);
    dirty_ = true;
}

void Connection::unmap_window(xcb_window_t win) {
    xcb_unmap_window(conn_, win);
    dirty_ = true;
}

void Connection::focus_window(xcb_window_t win) {
    xcb_set_input_focus(conn_, XCB_INPUT_FOCUS_POINTER_ROOT, win, XCB_CURRENT_TIME);
    dirty_ = true;
}

void Connection::kill_client(xcb_window_t win) {
    xcb_kill_client(conn_, win);
    dirty_ = true;
}

void Connection::configure_window(xcb_window_t win, uint16_t mask, const uint32_t* values) {
    xcb_configure_window(conn_, win, mask, values);
    dirty_ = true;
}

void Connection::change_window_attributes(xcb_window_t win, uint32_t mask, const uint32_t* values) {
    xcb_change_window_attributes(conn_, win, mask, values);
    dirty_ = true;
}

void Connection::create_window(xcb_window_t win, xcb_window_t parent,
                               int16_t x, int16_t y, uint16_t w, uint16_t h,
                               uint16_t cls, xcb_visualid_t visual,
                               uint32_t mask, const uint32_t* values) {
    xcb_create_window(conn_, XCB_COPY_FROM_PARENT, win, parent,
                      x, y, w, h, 0, cls, visual, mask, values);
    dirty_ = true;
}

void Connection::destroy_window(xcb_window_t win) {
    xcb_destroy_window(conn_, win);
    dirty_ = true;
}

void Connection::send_event(xcb_window_t win, uint32_t mask, const char* data) {
    xcb_send_event(conn_, 0, win, mask, data);
    dirty_ = true;
}

void Connection::change_property(xcb_window_t win, xcb_atom_t prop, xcb_atom_t type,
                                 uint8_t format, uint32_t count, const void* data) {
    xcb_change_property(conn_, XCB_PROP_MODE_REPLACE, win, prop, type, format, count, data);
    dirty_ = true;
}

// Queries

WindowAttributes Connection::get_window_attributes(xcb_window_t win) const {
    WindowAttributes out{};
    auto r = reply(xcb_get_window_attributes_reply(conn_,
                   xcb_get_window_attributes(conn_, win), nullptr));
    if (!r) return out;

    out.valid             = true;
    out.override_redirect = r->override_redirect;
    out.map_state         = r->map_state;
    out.win_class         = r->_class;
    out.your_event_mask   = r->your_event_mask;
    return out;
}

std::optional<Geometry> Connection::get_window_geometry(xcb_window_t win) const {
    auto r = reply(xcb_get_geometry_reply(conn_,
                   xcb_get_geometry(conn_, win), nullptr));
    if (!r) return std::nullopt;

    return Geometry{r->x, r->y, r->width, r->height, r->border_width};
}

std::vector<xcb_window_t> Connection::query_tree_children(xcb_window_t parent) const {
    auto r = reply(xcb_query_tree_reply(conn_,
                   xcb_query_tree(conn_, parent), nullptr));
    if (!r) return {};

    int   n        = xcb_query_tree_children_length(r.get());
    auto* children = xcb_query_tree_children(r.get());
    return {children, children + n};
}

std::optional<xcb_window_t> Connection::get_transient_for(xcb_window_t win) const {
    auto r = reply(xcb_get_property_reply(conn_,
                   xcb_get_property(conn_, 0, win, XCB_ATOM_WM_TRANSIENT_FOR,
                                    XCB_ATOM_WINDOW, 0, 1), nullptr));
    if (!r) return std::nullopt;

    if (r->format == 32 && r->value_len >= 1) {
        auto* data = static_cast<xcb_window_t*>(xcb_get_property_value(r.get()));
        if (data) return data[0];
    }
    return std::nullopt;
}

int Connection::get_wm_state_value(xcb_window_t win, xcb_atom_t wm_state_atom) const {
    if (wm_state_atom == XCB_ATOM_NONE) return -1;

    auto r = reply(xcb_get_property_reply(conn_,
                   xcb_get_property(conn_, 0, win, wm_state_atom, XCB_ATOM_ANY, 0, 2), nullptr));
    if (!r) return -1;

    if (r->format == 32 && r->value_len >= 1) {
        auto* data = static_cast<uint32_t*>(xcb_get_property_value(r.get()));
        if (data) return static_cast<int>(data[0]);
    }
    return -1;
}

SizeHints Connection::get_size_hints(xcb_window_t win) const {
    auto r = reply(xcb_get_property_reply(conn_,
                   xcb_get_property(conn_, 0, win, XCB_ATOM_WM_NORMAL_HINTS,
                                    XCB_ATOM_ANY, 0, 18), nullptr));
    if (!r) return {};

    SizeHints out;
    if (r->format == 32 && r->value_len >= 9) {
        auto* d = static_cast<int32_t*>(xcb_get_property_value(r.get()));
        if (!d) return out;

        constexpr uint32_t PMinSize   = (1u << 4);
        constexpr uint32_t PMaxSize   = (1u << 5);
        constexpr uint32_t PResizeInc = (1u << 6);
        constexpr uint32_t PBaseSize  = (1u << 8);
        uint32_t flags = static_cast<uint32_t>(d[0]);

        if (flags & PMinSize) {
            out.has_min = true;
            out.min_w = d[5]; out.min_h = d[6];
        }
        if (flags & PMaxSize) {
            out.has_max = true;
            out.max_w = d[7]; out.max_h = d[8];
        }
        if ((flags & PResizeInc) && r->value_len >= 11) {
            out.has_inc = true;
            out.inc_w = d[9]; out.inc_h = d[10];
        }
        if ((flags & PBaseSize) && r->value_len >= 17) {
            out.has_base = true;
            out.base_w = d[15]; out.base_h = d[16];
        }
        out.fixed = out.has_min && out.has_max &&
                    out.min_w > 0 && out.min_h > 0 &&
                    out.min_w == out.max_w && out.min_h == out.max_h;
    }
    return out;
}

WmHints Connection::get_wm_hints(xcb_window_t win) const {
    auto r = reply(xcb_get_property_reply(conn_,
                   xcb_get_property(conn_, 0, win, XCB_ATOM_WM_HINTS, XCB_ATOM_ANY, 0, 9), nullptr));
    if (!r) return {};

    WmHints out;
    if (r->format == 32 && r->value_len >= 2) {
        auto* data = static_cast<uint32_t*>(xcb_get_property_value(r.get()));
        constexpr uint32_t InputHint   = (1u << 0);
        constexpr uint32_t UrgencyHint = (1u << 8);
        if (data) {
            if (data[0] & InputHint)
                out.no_input = (data[1] == 0);
            out.urgent = (data[0] & UrgencyHint) != 0;
        }
    }
    return out;
}

bool Connection::has_property_32(xcb_window_t win, xcb_atom_t prop, uint32_t min_items) const {
    if (prop == XCB_ATOM_NONE) return false;

    auto r = reply(xcb_get_property_reply(conn_,
                   xcb_get_property(conn_, 0, win, prop, XCB_ATOM_ANY, 0, min_items), nullptr));
    if (!r) return false;
    return r->format == 32 && r->value_len >= min_items;
}

bool Connection::motif_no_decorations(xcb_window_t win, xcb_atom_t motif_atom) const {
    if (motif_atom == XCB_ATOM_NONE) return false;

    auto r = reply(xcb_get_property_reply(conn_,
                   xcb_get_property(conn_, 0, win, motif_atom, XCB_ATOM_ANY, 0, 5), nullptr));
    if (!r) return false;

    if (r->format == 32 && r->value_len >= 3) {
        auto* data = static_cast<uint32_t*>(xcb_get_property_value(r.get()));
        return (data[0] & 0x2u) && (data[2] == 0u);
    }
    return false;
}

bool Connection::has_static_gravity(xcb_window_t win) const {
    auto r = reply(xcb_get_property_reply(conn_,
                   xcb_get_property(conn_, 0, win, XCB_ATOM_WM_NORMAL_HINTS,
                                    XCB_ATOM_ANY, 0, 18), nullptr));
    if (!r || r->format != 32 || r->value_len < 1) return false;
    auto* data = static_cast<uint32_t*>(xcb_get_property_value(r.get()));
    constexpr uint32_t P_WIN_GRAVITY  = (1u << 9);
    constexpr uint32_t STATIC_GRAVITY = 10u;
    return r->value_len >= 10 && (data[0] & P_WIN_GRAVITY) && (data[9] == STATIC_GRAVITY);
}

bool Connection::motif_no_decorations(xcb_window_t win) const {
    auto r = reply(xcb_intern_atom_reply(conn_,
                   xcb_intern_atom(conn_, 0, 16, "_MOTIF_WM_HINTS"), nullptr));
    xcb_atom_t atom = r ? r->atom : static_cast<xcb_atom_t>(XCB_ATOM_NONE);
    return motif_no_decorations(win, atom);
}

// Reparent / Save-set

void Connection::reparent_window(xcb_window_t win, xcb_window_t parent, int16_t x, int16_t y) {
    xcb_reparent_window(conn_, win, parent, x, y);
    dirty_ = true;
}

void Connection::save_set_add(xcb_window_t win) {
    xcb_change_save_set(conn_, XCB_SET_MODE_INSERT, win);
    dirty_ = true;
}

void Connection::save_set_remove(xcb_window_t win) {
    xcb_change_save_set(conn_, XCB_SET_MODE_DELETE, win);
    dirty_ = true;
}

// Pixmap / GC

xcb_pixmap_t Connection::create_pixmap(uint8_t depth, xcb_window_t drawable,
                                        uint16_t w, uint16_t h) {
    xcb_pixmap_t pix = xcb_generate_id(conn_);
    xcb_create_pixmap(conn_, depth, pix, drawable, w, h);
    dirty_ = true;
    return pix;
}

void Connection::free_pixmap(xcb_pixmap_t pixmap) {
    xcb_free_pixmap(conn_, pixmap);
    dirty_ = true;
}

void Connection::copy_area(xcb_drawable_t src, xcb_drawable_t dst,
                           int16_t sx, int16_t sy, int16_t dx, int16_t dy,
                           uint16_t w, uint16_t h) {
    xcb_gcontext_t gc = xcb_generate_id(conn_);
    xcb_create_gc(conn_, gc, dst, 0, nullptr);
    xcb_copy_area(conn_, src, dst, gc, sx, sy, dx, dy, w, h);
    xcb_free_gc(conn_, gc);
    dirty_ = true;
}

// Selection

void Connection::set_selection_owner(xcb_window_t win, xcb_atom_t selection,
                                      xcb_timestamp_t time) {
    xcb_set_selection_owner(conn_, win, selection, time);
    dirty_ = true;
}

xcb_window_t Connection::get_selection_owner(xcb_atom_t selection) const {
    auto r = reply(xcb_get_selection_owner_reply(conn_,
                   xcb_get_selection_owner(conn_, selection), nullptr));
    return r ? r->owner : static_cast<xcb_window_t>(XCB_WINDOW_NONE);
}

// Input grabs

void Connection::grab_key(xcb_window_t win, xcb_keycode_t key, uint16_t modifiers) {
    xcb_grab_key(conn_, 1, win, modifiers, key,
                 XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
    dirty_ = true;
}

void Connection::ungrab_key(xcb_keycode_t key, xcb_window_t win, uint16_t modifiers) {
    xcb_ungrab_key(conn_, key, win, modifiers);
    dirty_ = true;
}

void Connection::grab_button(xcb_window_t win, uint32_t event_mask,
                              uint8_t button, uint16_t modifiers) {
    xcb_grab_button(conn_, 0, win, event_mask,
                    XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC,
                    XCB_WINDOW_NONE, XCB_CURSOR_NONE,
                    button, modifiers);
    dirty_ = true;
}

void Connection::grab_button_sync(xcb_window_t win, uint32_t event_mask,
                                   uint8_t button, uint16_t modifiers) {
    xcb_grab_button(conn_, 0, win, event_mask,
                    XCB_GRAB_MODE_SYNC, XCB_GRAB_MODE_ASYNC,
                    XCB_WINDOW_NONE, XCB_CURSOR_NONE,
                    button, modifiers);
    dirty_ = true;
}

void Connection::ungrab_button(uint8_t button, xcb_window_t win, uint16_t modifiers) {
    xcb_ungrab_button(conn_, button, win, modifiers);
    dirty_ = true;
}

void Connection::grab_pointer(xcb_window_t win, uint32_t event_mask,
                               xcb_window_t confine_to) {
    xcb_grab_pointer(conn_, 0, win, event_mask,
                     XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC,
                     confine_to, XCB_CURSOR_NONE, XCB_CURRENT_TIME);
    dirty_ = true;
}

void Connection::ungrab_pointer() {
    xcb_ungrab_pointer(conn_, XCB_CURRENT_TIME);
    dirty_ = true;
}

void Connection::allow_events(uint8_t mode, xcb_timestamp_t time) {
    xcb_allow_events(conn_, mode, time);
    dirty_ = true;
}

void Connection::warp_pointer(xcb_window_t src, xcb_window_t dst,
                               int16_t src_x, int16_t src_y,
                               uint16_t src_w, uint16_t src_h,
                               int16_t dst_x, int16_t dst_y) {
    xcb_warp_pointer(conn_, src, dst, src_x, src_y, src_w, src_h, dst_x, dst_y);
    dirty_ = true;
}

void Connection::set_input_focus(xcb_window_t win, xcb_timestamp_t time) {
    xcb_set_input_focus(conn_, XCB_INPUT_FOCUS_POINTER_ROOT, win, time);
    dirty_ = true;
}

Connection::PointerPosition Connection::query_pointer() const {
    auto r = reply(xcb_query_pointer_reply(conn_,
                   xcb_query_pointer(conn_, root()), nullptr));
    if (!r) return {};
    return {r->root_x, r->root_y, true};
}

xcb_generic_error_t* Connection::change_window_attributes_checked(xcb_window_t win,
    uint32_t mask, const uint32_t* values) {
    auto cookie = xcb_change_window_attributes_checked(conn_, win, mask, values);
    return xcb_request_check(conn_, cookie);
}

void Connection::force_flush() {
    if (conn_) {
        xcb_flush(conn_);
        dirty_ = false;
    }
}

// Cursor

xcb_cursor_t Connection::create_left_ptr_cursor() {
    xcb_font_t font = xcb_generate_id(conn_);
    xcb_open_font(conn_, font, 6, "cursor");

    xcb_cursor_t cursor = xcb_generate_id(conn_);
    xcb_create_glyph_cursor(conn_, cursor, font, font,
                            68, 69, 0, 0, 0, 0xFFFF, 0xFFFF, 0xFFFF);
    xcb_close_font(conn_, font);
    dirty_ = true;
    return cursor;
}

xcb_cursor_t Connection::create_invisible_cursor() {
    xcb_pixmap_t pix = xcb_generate_id(conn_);
    xcb_create_pixmap(conn_, 1, pix, root(), 1, 1);

    xcb_gcontext_t gc = xcb_generate_id(conn_);
    uint32_t       gc_values[] = { 0 };
    xcb_create_gc(conn_, gc, pix, XCB_GC_FOREGROUND, gc_values);
    xcb_rectangle_t rect { 0, 0, 1, 1 };
    xcb_poly_fill_rectangle(conn_, pix, gc, 1, &rect);

    xcb_cursor_t cursor = xcb_generate_id(conn_);
    xcb_create_cursor(conn_, cursor, pix, pix,
                      0, 0, 0, 0, 0, 0, 0, 0);

    xcb_free_gc(conn_, gc);
    xcb_free_pixmap(conn_, pix);
    dirty_ = true;
    return cursor;
}

void Connection::free_cursor(xcb_cursor_t cursor) {
    xcb_free_cursor(conn_, cursor);
    dirty_ = true;
}

int Connection::screen_number() const {
    if (!conn_ || !screen_) return 0;
    int i = 0;
    for (auto it = xcb_setup_roots_iterator(xcb_get_setup(conn_)); it.rem; xcb_screen_next(&it), ++i) {
        if (it.data == screen_)
            return i;
    }
    return 0;
}

std::vector<uint32_t> Connection::get_property_u32(xcb_window_t win, xcb_atom_t prop,
                                                    uint32_t max_items) const {
    auto r = reply(xcb_get_property_reply(conn_,
                   xcb_get_property(conn_, 0, win, prop, XCB_ATOM_ANY, 0, max_items), nullptr));
    if (!r || r->format != 32 || r->value_len == 0) return {};
    auto* data = static_cast<uint32_t*>(xcb_get_property_value(r.get()));
    return {data, data + r->value_len};
}

} // namespace xcb
