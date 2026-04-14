#pragma once

#include <xcb/xcb.h>
#include <xcb/reply.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace xcb {

struct WindowAttributes {
    bool     valid             = false;
    bool     override_redirect = false;
    uint8_t  map_state         = XCB_MAP_STATE_UNMAPPED;
    uint16_t win_class         = XCB_WINDOW_CLASS_COPY_FROM_PARENT;
    uint32_t your_event_mask   = 0;
};

struct Geometry {
    int16_t  x            = 0;
    int16_t  y            = 0;
    uint16_t width        = 0;
    uint16_t height       = 0;
    uint16_t border_width = 0;
};

struct SizeHints {
    bool fixed    = false;
    bool has_min  = false;
    bool has_max  = false;
    bool has_inc  = false;
    bool has_base = false;
    int  min_w = 0, min_h = 0;
    int  max_w = 0, max_h = 0;
    int  inc_w = 0, inc_h = 0;
    int  base_w = 0, base_h = 0;
};

struct WmHints {
    bool no_input = false;
    bool urgent   = false;
};

class Connection {
    public:
        Connection() = default;
        explicit Connection(xcb_connection_t* conn, xcb_screen_t* screen);
        static xcb_screen_t* first_screen(xcb_connection_t* conn);
        ~Connection();

        Connection(const Connection&)            = delete;
        Connection& operator=(const Connection&) = delete;
        Connection(Connection&& o) noexcept;
        Connection& operator=(Connection&& o) noexcept;

        explicit operator bool() const {
            return conn_ && !xcb_connection_has_error(conn_);
        }

        xcb_connection_t* raw()    const { return conn_; }
        xcb_screen_t*     screen() const { return screen_; }
        xcb_window_t      root()   const { return screen_ ? screen_->root : 0; }
        int                  fd()     const;

        void                 flush();
        void mark_dirty() { dirty_ = true; }
        xcb_generic_event_t* poll_event();
        xcb_window_t         generate_id();

        // Window operations
        void map_window(xcb_window_t win);
        void unmap_window(xcb_window_t win);
        void focus_window(xcb_window_t win);
        void kill_client(xcb_window_t win);
        void configure_window(xcb_window_t win, uint16_t mask, const uint32_t* values);
        void change_window_attributes(xcb_window_t win, uint32_t mask, const uint32_t* values);
        void create_window(xcb_window_t win, xcb_window_t parent,
            int16_t x, int16_t y, uint16_t w, uint16_t h,
            uint16_t cls, xcb_visualid_t visual = XCB_COPY_FROM_PARENT,
            uint32_t mask                       = 0, const uint32_t* values = nullptr);
        void destroy_window(xcb_window_t win);
        void send_event(xcb_window_t win, uint32_t mask, const char* data);

        void change_property(xcb_window_t win, xcb_atom_t prop, xcb_atom_t type,
            uint8_t format, uint32_t count, const void* data);

        // Queries
        WindowAttributes            get_window_attributes(xcb_window_t win) const;
        std::optional<Geometry>     get_window_geometry(xcb_window_t win) const;
        std::vector<xcb_window_t>   query_tree_children(xcb_window_t parent) const;
        std::optional<xcb_window_t> get_transient_for(xcb_window_t win) const;
        int                         get_wm_state_value(xcb_window_t win, xcb_atom_t wm_state_atom) const;
        SizeHints                   get_size_hints(xcb_window_t win) const;
        WmHints                     get_wm_hints(xcb_window_t win) const;
        bool                        has_property_32(xcb_window_t win, xcb_atom_t prop, uint32_t min_items) const;
        bool                        motif_no_decorations(xcb_window_t win, xcb_atom_t motif_atom) const;
        bool                        motif_no_decorations(xcb_window_t win) const;
        bool                        has_static_gravity(xcb_window_t win) const;

        // Reparent / Save-set
        void reparent_window(xcb_window_t win, xcb_window_t parent, int16_t x, int16_t y);
        void save_set_add(xcb_window_t win);
        void save_set_remove(xcb_window_t win);

        // Pixmap / GC
        xcb_pixmap_t create_pixmap(uint8_t depth, xcb_window_t drawable, uint16_t w, uint16_t h);
        void         free_pixmap(xcb_pixmap_t pixmap);
        void         copy_area(xcb_drawable_t src, xcb_drawable_t dst,
            int16_t sx, int16_t sy, int16_t dx, int16_t dy,
            uint16_t w, uint16_t h);

        // Selection
        void         set_selection_owner(xcb_window_t win, xcb_atom_t selection,
            xcb_timestamp_t time = XCB_CURRENT_TIME);
        xcb_window_t get_selection_owner(xcb_atom_t selection) const;

        // Input grabs
        void grab_key(xcb_window_t win, xcb_keycode_t key, uint16_t modifiers);
        void ungrab_key(xcb_keycode_t key, xcb_window_t win, uint16_t modifiers);
        void grab_button(xcb_window_t win, uint32_t event_mask,
            uint8_t button, uint16_t modifiers);
        void grab_button_sync(xcb_window_t win, uint32_t event_mask,
            uint8_t button, uint16_t modifiers);
        void ungrab_button(uint8_t button, xcb_window_t win, uint16_t modifiers);
        void grab_pointer(xcb_window_t win, uint32_t event_mask,
            xcb_window_t confine_to = XCB_WINDOW_NONE);
        void ungrab_pointer();
        void allow_events(uint8_t mode, xcb_timestamp_t time = XCB_CURRENT_TIME);
        void warp_pointer(xcb_window_t src, xcb_window_t dst,
            int16_t src_x, int16_t src_y,
            uint16_t src_w, uint16_t src_h,
            int16_t dst_x, int16_t dst_y);

        // Focus with explicit timestamp
        void set_input_focus(xcb_window_t win, xcb_timestamp_t time = XCB_CURRENT_TIME);

        // Pointer query
        struct PointerPosition { int16_t x = 0, y = 0; bool valid = false; };
        PointerPosition query_pointer() const;

        // Checked operations
        xcb_generic_error_t* change_window_attributes_checked(xcb_window_t win,
            uint32_t mask, const uint32_t* values);

        // Cursor
        xcb_cursor_t create_left_ptr_cursor();
        xcb_cursor_t create_invisible_cursor();
        void         free_cursor(xcb_cursor_t cursor);

        // Flush unconditionally
        void force_flush();

        // Screen info
        uint32_t screen_black_pixel() const { return screen_ ? screen_->black_pixel : 0; }
        uint8_t  screen_root_depth()  const { return screen_ ? screen_->root_depth : 0; }
        int screen_number()      const;

        // Generic property read (format-32, up to max_items uint32_t values)
        std::vector<uint32_t> get_property_u32(xcb_window_t win, xcb_atom_t prop,
            uint32_t max_items) const;

    protected:
        xcb_connection_t* conn_   = nullptr;
        xcb_screen_t*     screen_ = nullptr;
        bool dirty_               = false;
        bool owns_connection_     = false;
};

} // namespace xcb
