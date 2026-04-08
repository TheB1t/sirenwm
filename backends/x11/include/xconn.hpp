#pragma once

#include <xcb/xcb.h>
#include <xcb/randr.h>
#include <xcb/xcb_keysyms.h>
#include <utility>
#include <vector>
#include <string>
#include <unordered_map>
#include <optional>
#include <log.hpp>

// Forward-declare Xlib types to avoid pulling in Xlib.h.
struct _XDisplay;
typedef struct _XDisplay Display;

struct Monitor;

class XConnection {
    Display* dpy             = nullptr;
    xcb_connection_t* conn   = nullptr;
    xcb_screen_t*     screen = nullptr;
    bool dirty               = false;
    int  xkb_event_type_     = -1;

    public:
        XConnection();
        ~XConnection();

        xcb_connection_t* raw_conn() { return conn; }
        const xcb_connection_t* raw_conn() const { return conn; }
        xcb_screen_t* raw_screen() { return screen; }
        const xcb_screen_t* raw_screen() const { return screen; }
        Display* xlib_display() { return dpy; }
        int xkb_event_type() const { return xkb_event_type_; }

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

        // Graceful teardown. Safe to call multiple times.
        void shutdown();

        void flush() {
            if (dirty) {
                xcb_flush(conn); dirty = false;
            }
        }

        int fd() const {
            return xcb_get_file_descriptor(conn);
        }

        xcb_generic_event_t* poll() {
            return xcb_poll_for_event(conn);
        }

        xcb_window_t  root_window()  const { return screen->root; }
        uint32_t screen_black_pixel() const { return screen ? screen->black_pixel : 0; }

        // Generic xcb call — marks connection dirty (needs flush).
        template<typename Func, typename... Args>
        auto call(Func f, Args&&... args) -> decltype(f(conn, std::forward<Args>(args)...)) {
            dirty = true;
            return f(conn, std::forward<Args>(args)...);
        }

        void map_window(xcb_window_t win)   { call(xcb_map_window,   win); }
        void unmap_window(xcb_window_t win) { call(xcb_unmap_window, win); }

        void focus_window(xcb_window_t win) {
            call(xcb_set_input_focus, XCB_INPUT_FOCUS_POINTER_ROOT, win, XCB_CURRENT_TIME);
        }

        void configure_window(xcb_window_t win, uint16_t mask, const uint32_t* values) {
            call(xcb_configure_window, win, mask, values);
        }

        void change_window_attributes(xcb_window_t win, uint32_t mask, const uint32_t* values) {
            call(xcb_change_window_attributes, win, mask, values);
        }

        // Returns error or nullptr. Caller owns the pointer (free it).
        xcb_generic_error_t* change_window_attributes_checked(xcb_window_t win,
            uint32_t mask,
            const uint32_t* values) {
            auto cookie = xcb_change_window_attributes_checked(conn, win, mask, values);
            return xcb_request_check(conn, cookie);
        }

        void grab_button(xcb_window_t win, uint32_t event_mask,
            uint8_t button, uint16_t modifiers) {
            call(xcb_grab_button,
                0, // owner_events: false
                win,
                event_mask,
                XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC,
                XCB_WINDOW_NONE, XCB_CURSOR_NONE,
                button, modifiers);
        }

        void grab_pointer(xcb_window_t confine_to, uint32_t event_mask) {
            xcb_grab_pointer(conn, 0, root_window(), event_mask,
                XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC,
                confine_to, XCB_CURSOR_NONE, XCB_CURRENT_TIME);
            dirty = true;
        }

        void ungrab_pointer() {
            xcb_ungrab_pointer(conn, XCB_CURRENT_TIME);
            dirty = true;
        }

        void kill_client(xcb_window_t win) {
            call(xcb_kill_client, win);
        }

        void send_event(xcb_window_t win, uint32_t mask, const char* data) {
            xcb_send_event(conn, 0, win, mask, data);
            dirty = true;
        }

        xcb_window_t generate_id() {
            return xcb_generate_id(conn);
        }

        void create_window(xcb_window_t win, xcb_window_t parent,
            int16_t x, int16_t y, uint16_t w, uint16_t h,
            uint16_t cls) {
            xcb_create_window(conn, XCB_COPY_FROM_PARENT, win, parent,
                x, y, w, h, 0, cls, XCB_COPY_FROM_PARENT, 0, nullptr);
            dirty = true;
        }

        std::vector<xcb_window_t>   query_tree_children(xcb_window_t parent) const;
        WindowAttributes            get_window_attributes(xcb_window_t win) const;
        std::optional<Geometry>     get_window_geometry(xcb_window_t win) const;
        std::optional<xcb_window_t> get_transient_for_window(xcb_window_t win) const;
        int                         get_wm_state_value(xcb_window_t win, xcb_atom_t wm_state_atom) const;
        struct SizeHints {
            bool fixed    = false; // min == max (implies no_resize)
            bool has_min  = false;
            bool has_max  = false;
            bool has_inc  = false;
            bool has_base = false;
            int  min_w = 0, min_h = 0;
            int  max_w = 0, max_h = 0;
            int  inc_w = 0, inc_h = 0;  // size increment (terminal cell size)
            int  base_w = 0, base_h = 0;
        };
        SizeHints                   get_size_hints(xcb_window_t win) const;
        bool                        has_fixed_size_hints(xcb_window_t win) const;
        bool                        has_static_gravity(xcb_window_t win) const;
        bool                        motif_no_decorations(xcb_window_t win) const;
        struct WmHints { bool no_input = false; bool urgent = false; };
        WmHints                     get_wm_hints(xcb_window_t win) const;
        bool                        get_wm_hints_no_input(xcb_window_t win) const;
        bool                        has_property_32(xcb_window_t win, xcb_atom_t prop, uint32_t min_items) const;
        xcb_cursor_t                create_left_ptr_cursor();
        void                        free_cursor(xcb_cursor_t cursor);

        void change_property(xcb_window_t win, xcb_atom_t prop, xcb_atom_t type,
            uint8_t format, uint32_t count, const void* data) {
            xcb_change_property(conn, XCB_PROP_MODE_REPLACE, win, prop, type, format, count, data);
            dirty = true;
        }

        xcb_intern_atom_cookie_t intern_atom_async(const char* name, uint16_t len) const {
            return xcb_intern_atom(conn, 0, len, name);
        }

        xcb_atom_t intern_atom_reply(xcb_intern_atom_cookie_t cookie) const {
            auto       r = xcb_intern_atom_reply(conn, cookie, nullptr);
            if (!r) return XCB_ATOM_NONE;
            xcb_atom_t atom = r->atom;
            free(r);
            return atom;
        }

        xcb_key_symbols_t* alloc_key_symbols() const {
            return xcb_key_symbols_alloc(conn);
        }

        xcb_atom_t                         get_atom_property(xcb_window_t win, xcb_atom_t prop) const;
        std::vector<xcb_atom_t>            get_atom_list_property(xcb_window_t win, xcb_atom_t prop) const;
        std::pair<std::string,std::string> get_wm_class(xcb_window_t win) const;
        std::string                        get_text_property(xcb_window_t win, xcb_atom_t prop,
            xcb_atom_t type = XCB_GET_PROPERTY_TYPE_ANY) const;

        void set_property(xcb_window_t win, xcb_atom_t prop, xcb_atom_t type,
            const xcb_atom_t* data, int count);
        void set_property(xcb_window_t win, xcb_atom_t prop,
            const xcb_window_t* data, int count);
        void set_property(xcb_window_t win, xcb_atom_t prop,
            xcb_atom_t type, uint32_t value);
        void set_property(xcb_window_t win, xcb_atom_t prop,
            xcb_atom_t type, const std::string& str);

        // Batch intern atoms — returns map name→atom
        std::unordered_map<std::string, xcb_atom_t>
        intern_atoms(std::initializer_list<const char*> names) const;

        void randr_select_input(xcb_window_t win, uint32_t mask) {
            xcb_randr_select_input(conn, win, mask);
            dirty = true;
        }

        const xcb_query_extension_reply_t* randr_extension_data() const {
            return xcb_get_extension_data(conn, &xcb_randr_id);
        }

        // Returns monitors via RandR. Falls back to screen size if RandR unavailable.
        std::vector<Monitor> get_monitors() const;
};
