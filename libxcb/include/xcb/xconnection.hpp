#pragma once

#include <xcb/atom.hpp>
#include <xcb/connection.hpp>
#include <xcb/property.hpp>
#include <xcb/randr.h>
#include <xcb/xcb.h>

#include <cstdint>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

struct _XDisplay;
typedef struct _XDisplay Display;

namespace xcb {

class XConnection : public Connection {
    Display* dpy_            = nullptr;
    int      xkb_event_type_ = -1;

    public:
        XConnection();
        ~XConnection();

        xcb_connection_t*       raw_conn() { return conn_; }
        const xcb_connection_t* raw_conn() const { return conn_; }
        xcb_screen_t*           raw_screen() { return screen_; }
        const xcb_screen_t*     raw_screen() const { return screen_; }
        Display*                xlib_display() { return dpy_; }
        const Display*          xlib_display() const { return dpy_; }
        int                     xkb_event_type() const { return xkb_event_type_; }

        void shutdown();

        xcb_window_t root_window() const { return screen_ ? screen_->root : 0; }
        uint32_t     screen_black_pixel() const { return screen_ ? screen_->black_pixel : 0; }

        static uint32_t parse_color_hex(const std::string& s) {
            if (s.size() == 7 && s[0] == '#') {
                char*    end = nullptr;
                uint32_t rgb = static_cast<uint32_t>(strtoul(s.c_str() + 1, &end, 16));
                if (end == s.c_str() + 7)
                    return rgb;
            }
            return 0;
        }

        template<typename Func, typename... Args>
        auto call(Func f, Args&&... args) -> decltype(f(conn_, std::forward<Args>(args)...)) {
            dirty_ = true;
            return f(conn_, std::forward<Args>(args)...);
        }

        xcb_generic_error_t* change_window_attributes_checked(xcb_window_t win,
            uint32_t mask, const uint32_t* values) {
            auto cookie = xcb_change_window_attributes_checked(conn_, win, mask, values);
            return xcb_request_check(conn_, cookie);
        }

        void grab_button(xcb_window_t win, uint32_t event_mask,
            uint8_t button, uint16_t modifiers) {
            call(xcb_grab_button,
                0, win, event_mask,
                XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC,
                XCB_WINDOW_NONE, XCB_CURSOR_NONE,
                button, modifiers);
        }

        void grab_pointer(xcb_window_t confine_to, uint32_t event_mask) {
            xcb_grab_pointer(conn_, 0, root_window(), event_mask,
                XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC,
                confine_to, XCB_CURSOR_NONE, XCB_CURRENT_TIME);
            dirty_ = true;
        }

        void ungrab_pointer() {
            xcb_ungrab_pointer(conn_, XCB_CURRENT_TIME);
            dirty_ = true;
        }

        xcb_intern_atom_cookie_t intern_atom_async(const char* name, uint16_t len) const {
            return xcb_intern_atom(conn_, 0, len, name);
        }

        xcb_atom_t intern_atom_reply(xcb_intern_atom_cookie_t cookie) const {
            auto       r = ::xcb_intern_atom_reply(conn_, cookie, nullptr);
            if (!r) return XCB_ATOM_NONE;
            xcb_atom_t atom = r->atom;
            free(r);
            return atom;
        }

        xcb_atom_t                          get_atom_property(xcb_window_t win, xcb_atom_t prop) const;
        std::vector<xcb_atom_t>             get_atom_list_property(xcb_window_t win, xcb_atom_t prop) const;
        std::pair<std::string, std::string> get_wm_class(xcb_window_t win) const;
        std::string                         get_text_property(xcb_window_t win, xcb_atom_t prop,
            xcb_atom_t type = XCB_GET_PROPERTY_TYPE_ANY) const;

        void set_property(xcb_window_t win, xcb_atom_t prop, xcb_atom_t type,
            const xcb_atom_t* data, int count);
        void set_property(xcb_window_t win, xcb_atom_t prop,
            const xcb_window_t* data, int count);
        void set_property(xcb_window_t win, xcb_atom_t prop,
            xcb_atom_t type, uint32_t value);
        void set_property(xcb_window_t win, xcb_atom_t prop,
            xcb_atom_t type, const std::string& str);

        std::unordered_map<std::string, xcb_atom_t>
             intern_atoms(std::initializer_list<const char*> names) const;

        bool has_fixed_size_hints(xcb_window_t win) const;

        void randr_select_input(xcb_window_t win, uint32_t mask) {
            xcb_randr_select_input(conn_, win, mask);
            dirty_ = true;
        }

        const xcb_query_extension_reply_t* randr_extension_data() const {
            return xcb_get_extension_data(conn_, &xcb_randr_id);
        }
};

} // namespace xcb
