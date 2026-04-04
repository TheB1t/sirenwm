#include <x11_ports.hpp>

#include <xconn.hpp>
#include <xcb/atom.hpp>
#include <log.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace {

class X11TrayHost final : public backend::TrayHost {
    public:
        X11TrayHost(XConnection& xconn,
            WindowId bar_win,
            int bar_x, int bar_y, int bar_h,
            uint32_t bg_pixel,
            bool own_selection)
            : conn_(xconn.raw_conn()),
              screen_(xconn.raw_screen()),
              bar_win_(bar_win),
              bar_x_(bar_x),
              bar_y_(bar_y),
              bar_h_(bar_h),
              bg_pixel_(bg_pixel) {
            if (!conn_ || !screen_)
                return;

            tray_x_ = bar_x_;
            tray_y_ = bar_y_;
            intern_atoms();

            tray_win_ = xcb_generate_id(conn_);
            uint32_t mask   = XCB_CW_BACK_PIXEL | XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK;
            uint32_t vals[] = {
                bg_pixel_,
                1u,
                XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY | XCB_EVENT_MASK_BUTTON_PRESS |
                XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_EXPOSURE,
            };
            xcb_create_window(conn_, XCB_COPY_FROM_PARENT, tray_win_, screen_->root,
                (int16_t)bar_x_, (int16_t)bar_y_, 1, (uint16_t)bar_h_, 0,
                XCB_WINDOW_CLASS_INPUT_OUTPUT, XCB_COPY_FROM_PARENT,
                mask, vals);

            uint32_t orient = 0; // horizontal
            xcb_change_property(conn_, XCB_PROP_MODE_REPLACE, tray_win_,
                NET_SYSTEM_TRAY_ORIENT_, XCB_ATOM_CARDINAL, 32, 1, &orient);

            if (own_selection) {
                if (!try_acquire_selection()) {
                    LOG_ERR("TrayHost: cannot acquire _NET_SYSTEM_TRAY_S");
                    xcb_destroy_window(conn_, tray_win_);
                    tray_win_ = XCB_WINDOW_NONE;
                    return;
                }
                selection_owner_ = true;
            }

            xcb_map_window(conn_, tray_win_);
            xcb_flush(conn_);
            visible_ = true;
            if (selection_owner_) {
                broadcast_manager();
                adopt_orphaned_icons();
            }
        }

        ~X11TrayHost() override {
            if (!conn_ || !screen_)
                return;
            for (auto& ic : icons_) {
                xcb_change_save_set(conn_, XCB_SET_MODE_DELETE, ic.win);
                xcb_reparent_window(conn_, ic.win, screen_->root, 0, 0);
                if (ic.mapped)
                    xcb_map_window(conn_, ic.win);
            }
            icons_.clear();
            if (tray_win_ != XCB_WINDOW_NONE)
                xcb_destroy_window(conn_, tray_win_);
            xcb_flush(conn_);
        }

        WindowId window() const override { return tray_win_; }
        bool owns_selection() const override { return selection_owner_; }
        int width() const override { return tray_w_; }

        void set_visible(bool visible) override {
            if (!conn_ || tray_win_ == XCB_WINDOW_NONE)
                return;
            if (visible == visible_)
                return;
            visible_ = visible;
            if (visible)
                xcb_map_window(conn_, tray_win_);
            else
                xcb_unmap_window(conn_, tray_win_);
            xcb_flush(conn_);
        }

        void reposition(WindowId owner_bar_window, int bar_right_x, int bar_y) override {
            if (!conn_ || tray_win_ == XCB_WINDOW_NONE || tray_w_ == 0)
                return;
            bar_win_ = owner_bar_window;
            tray_x_  = bar_right_x - tray_w_;
            tray_y_  = bar_y;
            uint32_t vals[] = {
                static_cast<uint32_t>(tray_x_),
                static_cast<uint32_t>(tray_y_),
            };
            xcb_configure_window(conn_, tray_win_,
                XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, vals);
            xcb_flush(conn_);
        }

        void attach_to_bar(WindowId new_bar_win, int bar_x, int bar_y, int bar_w) override {
            if (!conn_ || tray_win_ == XCB_WINDOW_NONE)
                return;
            bar_win_ = new_bar_win;
            bar_x_   = bar_x;
            bar_y_   = bar_y;
            // Place tray at right edge of bar; relayout will fine-tune x when icons arrive.
            tray_x_  = bar_x + bar_w - std::max(tray_w_, 1);
            tray_y_  = bar_y;
            uint32_t vals[] = {
                static_cast<uint32_t>(tray_x_),
                static_cast<uint32_t>(tray_y_),
            };
            xcb_configure_window(conn_, tray_win_,
                XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, vals);
            xcb_flush(conn_);
            LOG_DEBUG("TrayHost 0x%x: attached to bar 0x%x at %d+%d", tray_win_, new_bar_win, tray_x_, tray_y_);
        }

        void raise(WindowId bar_sibling) override {
            if (!conn_ || tray_win_ == XCB_WINDOW_NONE)
                return;
            uint32_t vals[] = {
                static_cast<uint32_t>(bar_sibling),
                XCB_STACK_MODE_ABOVE,
            };
            xcb_configure_window(conn_, tray_win_,
                XCB_CONFIG_WINDOW_SIBLING | XCB_CONFIG_WINDOW_STACK_MODE, vals);
            xcb_flush(conn_);
        }

        void lower() override {
            if (!conn_ || tray_win_ == XCB_WINDOW_NONE)
                return;
            uint32_t vals[] = { XCB_STACK_MODE_BELOW };
            xcb_configure_window(conn_, tray_win_, XCB_CONFIG_WINDOW_STACK_MODE, vals);
            xcb_flush(conn_);
        }

        bool contains_icon(WindowId win) const override {
            return is_icon(win);
        }

        std::vector<WindowId> icon_windows() const override {
            std::vector<WindowId> out;
            out.reserve(icons_.size());
            for (const auto& ic : icons_)
                out.push_back(ic.win);
            return out;
        }

        std::string icon_wm_class(WindowId win) const override {
            if (!conn_ || !is_icon(win))
                return {};
            auto cookie = xcb_get_property(conn_, 0, win, XCB_ATOM_WM_CLASS,
                    XCB_ATOM_STRING, 0, 256);
            auto reply  = xcb_get_property_reply(conn_, cookie, nullptr);
            if (!reply)
                return {};
            // WM_CLASS is "instance\0class\0" — we want the class (second string)
            int         len       = xcb_get_property_value_length(reply);
            auto*       data      = static_cast<const char*>(xcb_get_property_value(reply));
            std::string result;
            int         first_len = 0;
            while (first_len < len && data[first_len] != '\0')
                first_len++;
            int class_start = first_len + 1;
            if (class_start < len)
                result = std::string(data + class_start);
            free(reply);
            for (auto& c : result) c = (char)tolower((unsigned char)c);
            return result;
        }

        bool transfer_icon_to(backend::TrayHost& dst, WindowId win) override {
            auto* other = dynamic_cast<X11TrayHost*>(&dst);
            if (!other || other == this)
                return false;
            Icon* ic = find_icon(win);
            if (!ic)
                return false;
            if (other->tray_win_ == XCB_WINDOW_NONE)
                return false;

            // Move icon data to the other tray's list.
            other->icons_.push_back(*ic);
            icons_.erase(std::find_if(icons_.begin(), icons_.end(),
                [win](const Icon& i) {
                    return i.win == win;
                }));

            // Re-subscribe events so the icon reports to the new tray.
            uint32_t mask   = XCB_CW_EVENT_MASK;
            uint32_t vals[] = { XCB_EVENT_MASK_PROPERTY_CHANGE | XCB_EVENT_MASK_STRUCTURE_NOTIFY };
            xcb_change_window_attributes(conn_, win, mask, vals);

            // Physically move the icon window under the new tray container.
            xcb_reparent_window(conn_, win, other->tray_win_, 0, 0);
            send_xembed(win, XEMBED_EMBEDDED_NOTIFY, 0, other->tray_win_, XEMBED_VERSION);
            if (other->icons_.back().mapped)
                xcb_map_window(conn_, win);

            relayout();
            other->relayout();
            xcb_flush(conn_);
            return true;
        }

        bool handle_client_message(const event::ClientMessageEv& ev, WindowId* docked_icon_out) override {
            if (tray_win_ == XCB_WINDOW_NONE || !selection_owner_)
                return false;
            if (ev.type != NET_SYSTEM_TRAY_OP_)
                return false;
            LOG_DEBUG("TrayHost: OPCODE msg to 0x%x (tray=0x%x bar=0x%x) opcode=%u icon=0x%x",
                ev.window, tray_win_, bar_win_, ev.data[1], ev.data[2]);
            if (ev.window != tray_win_ && ev.window != bar_win_)
                return false;

            uint32_t opcode = ev.data[1];
            if (opcode != 0) // SYSTEM_TRAY_REQUEST_DOCK
                return true;

            WindowId icon_win = ev.data[2];
            add_icon(icon_win);
            if (docked_icon_out)
                *docked_icon_out = icon_win;
            return true;
        }

        bool handle_destroy_notify(WindowId win) override {
            if (!is_icon(win))
                return false;
            remove_icon(win);
            return true;
        }

        bool handle_unmap_notify(WindowId win) override {
            Icon* ic = find_icon(win);
            if (!ic)
                return false;
            bool should_map = xembed_info_mapped(win);
            if (should_map) {
                xcb_map_window(conn_, win);
                ic->mapped = true;
            } else {
                ic->mapped = false;
            }
            relayout();
            return true;
        }

        bool handle_configure_notify(WindowId win) override {
            Icon* ic = find_icon(win);
            if (!ic)
                return false;
            // Only relayout if the icon reports a size different from what we
            // configured — otherwise we'd loop: relayout → ConfigureNotify → relayout.
            auto cookie = xcb_get_geometry(conn_, win);
            auto reply  = xcb_get_geometry_reply(conn_, cookie, nullptr);
            if (!reply)
                return true;
            int new_w = std::max<int>(reply->width, 1);
            int new_h = std::max<int>(reply->height, 1);
            free(reply);
            if (new_w == ic->w && new_h == ic->h)
                return true;
            ic->w = new_w;
            ic->h = new_h;
            relayout();
            return true;
        }

        bool handle_property_notify(WindowId win, uint32_t atom) override {
            if (atom != XEMBED_INFO_)
                return false;
            Icon* ic = find_icon(win);
            if (!ic)
                return false;

            bool should_map = xembed_info_mapped(win);
            if (should_map && !ic->mapped) {
                xcb_map_window(conn_, win);
                ic->mapped = true;
                relayout();
            } else if (!should_map && ic->mapped) {
                xcb_unmap_window(conn_, win);
                ic->mapped = false;
                relayout();
            }
            return true;
        }

        bool handle_button_event(const event::ButtonEv& ev) override {
            if (tray_win_ == XCB_WINDOW_NONE || tray_w_ <= 0)
                return false;

            int local_x = ev.event_x;
            int local_y = ev.event_y;
            if (ev.window == bar_win_) {
                local_x = static_cast<int>(ev.root_x) - tray_x_;
                local_y = static_cast<int>(ev.root_y) - tray_y_;
            } else if (ev.window == tray_win_) {
            } else if (find_icon(ev.window)) {
                return true;
            } else {
                return false;
            }

            if (local_x < 0 || local_y < 0 || local_x >= tray_w_ || local_y >= bar_h_)
                return false;

            Icon* ic = find_icon_at(local_x, local_y);
            if (!ic || !ic->mapped)
                return false;

            int16_t icon_x               = (int16_t)std::max(0, local_x - ic->x);
            int16_t icon_y               = (int16_t)std::max(0, local_y - ic->y);

            xcb_button_press_event_t bev = {};
            bev.response_type = ev.release ? XCB_BUTTON_RELEASE : XCB_BUTTON_PRESS;
            bev.detail        = ev.button;
            bev.time          = ev.time;
            bev.root          = screen_->root;
            bev.event         = ic->win;
            bev.child         = XCB_WINDOW_NONE;
            bev.root_x        = ev.root_x;
            bev.root_y        = ev.root_y;
            bev.event_x       = icon_x;
            bev.event_y       = icon_y;
            bev.state         = ev.state;
            bev.same_screen   = 1;

            xcb_send_event(conn_, 0, ic->win, XCB_EVENT_MASK_NO_EVENT, (const char*)&bev);
            xcb_flush(conn_);
            return true;
        }

    private:
        struct Icon {
            WindowId win    = NO_WINDOW;
            int      x      = 0;
            int      y      = 0;
            int      w      = 1;
            int      h      = 1;
            bool     mapped = false;
        };

        static constexpr int ICON_SPACING                = 2;
        static constexpr uint32_t XEMBED_EMBEDDED_NOTIFY = 0;
        static constexpr uint32_t XEMBED_MAPPED          = (1u << 0);
        static constexpr uint32_t XEMBED_VERSION         = 0;

        void intern_atoms() {
            char tray_sel[32];
            int  screen_num = 0;
            int  i          = 0;
            for (auto it = xcb_setup_roots_iterator(xcb_get_setup(conn_)); it.rem; xcb_screen_next(&it), i++) {
                if (it.data == screen_) {
                    screen_num = i;
                    break;
                }
            }
            std::snprintf(tray_sel, sizeof(tray_sel), "_NET_SYSTEM_TRAY_S%d", screen_num);

            auto atoms = xcb::intern_batch(conn_, {
                tray_sel,
                "_NET_SYSTEM_TRAY_OPCODE",
                "_NET_SYSTEM_TRAY_ORIENTATION",
                "_XEMBED",
                "_XEMBED_INFO",
                "MANAGER",
            });
            NET_SYSTEM_TRAY_S_      = atoms[tray_sel];
            NET_SYSTEM_TRAY_OP_     = atoms["_NET_SYSTEM_TRAY_OPCODE"];
            NET_SYSTEM_TRAY_ORIENT_ = atoms["_NET_SYSTEM_TRAY_ORIENTATION"];
            XEMBED_                 = atoms["_XEMBED"];
            XEMBED_INFO_            = atoms["_XEMBED_INFO"];
            MANAGER_                = atoms["MANAGER"];
        }

        bool try_acquire_selection() {
            xcb_set_selection_owner(conn_, tray_win_, NET_SYSTEM_TRAY_S_, XCB_CURRENT_TIME);
            xcb_flush(conn_);

            auto cookie = xcb_get_selection_owner(conn_, NET_SYSTEM_TRAY_S_);
            auto reply  = xcb_get_selection_owner_reply(conn_, cookie, nullptr);
            if (!reply)
                return false;
            bool ok = (reply->owner == tray_win_);
            if (!ok)
                LOG_WARN("TrayHost: acquire failed, owner is 0x%x", reply->owner);
            free(reply);
            return ok;
        }

        void broadcast_manager() {
            xcb_client_message_event_t ev = {};
            ev.response_type  = XCB_CLIENT_MESSAGE;
            ev.format         = 32;
            ev.window         = screen_->root;
            ev.type           = MANAGER_;
            ev.data.data32[0] = XCB_CURRENT_TIME;
            ev.data.data32[1] = NET_SYSTEM_TRAY_S_;
            ev.data.data32[2] = tray_win_;
            ev.data.data32[3] = 0;
            ev.data.data32[4] = 0;

            xcb_send_event(conn_, 0, screen_->root,
                XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT,
                (const char*)&ev);
            xcb_flush(conn_);
        }

        void add_icon(WindowId win) {
            if (is_icon(win) || tray_win_ == XCB_WINDOW_NONE)
                return;

            // Reject oversized windows — real tray icons are square (~ bar height).
            // Real icons are small (16-64px); apps like Telegram send REQUEST_DOCK
            // for full application windows that happen to have _XEMBED_INFO.
            auto geo = xcb_get_geometry_reply(conn_, xcb_get_geometry(conn_, win), nullptr);
            if (geo) {
                uint16_t w = geo->width;
                uint16_t h = geo->height;
                free(geo);
                if (w >= 256 || h >= 256) {
                    LOG_DEBUG("TrayHost: rejecting oversized dock request from 0x%x (%ux%u)",
                        win, w, h);
                    return;
                }
            }

            uint32_t mask   = XCB_CW_EVENT_MASK;
            uint32_t vals[] = { XCB_EVENT_MASK_PROPERTY_CHANGE | XCB_EVENT_MASK_STRUCTURE_NOTIFY };
            xcb_change_window_attributes(conn_, win, mask, vals);
            xcb_change_save_set(conn_, XCB_SET_MODE_INSERT, win);
            xcb_reparent_window(conn_, win, tray_win_, 0, 0);

            Icon ic;
            ic.win = win;
            ic.w   = bar_h_;
            ic.h   = bar_h_;
            // If _XEMBED_INFO is present, respect the MAPPED flag.
            // If absent, default to mapped (same as dwm behaviour).
            ic.mapped = !has_xembed_info(win) || xembed_info_mapped(win);

            auto geo2 = xcb_get_geometry_reply(conn_, xcb_get_geometry(conn_, win), nullptr);
            if (geo2) {
                if (geo2->width > 1) ic.w = geo2->width;
                if (geo2->height > 1) ic.h = geo2->height;
                free(geo2);
            }

            icons_.push_back(ic);
            send_xembed(win, XEMBED_EMBEDDED_NOTIFY, 0, tray_win_, XEMBED_VERSION);
            if (ic.mapped)
                xcb_map_window(conn_, win);

            relayout();
            xcb_flush(conn_);
        }

        void remove_icon(WindowId win) {
            auto it = std::find_if(icons_.begin(), icons_.end(),
                    [win](const Icon& i) {
                        return i.win == win;
                    });
            if (it == icons_.end())
                return;
            xcb_change_save_set(conn_, XCB_SET_MODE_DELETE, win);
            icons_.erase(it);
            relayout();
        }

        void adopt_orphaned_icons() {
            if (tray_win_ == XCB_WINDOW_NONE)
                return;
            auto tree_reply = xcb_query_tree_reply(conn_,
                    xcb_query_tree(conn_, screen_->root), nullptr);
            if (!tree_reply)
                return;
            int   n        = xcb_query_tree_children_length(tree_reply);
            auto* children = xcb_query_tree_children(tree_reply);
            for (int i = 0; i < n; i++) {
                WindowId win = children[i];
                if (win == tray_win_ || is_icon(win))
                    continue;
                if (!has_xembed_info(win))
                    continue;
                add_icon(win);
            }
            free(tree_reply);
        }

        void adopt_icon(WindowId win) override {
            if (!selection_owner_ || tray_win_ == XCB_WINDOW_NONE)
                return;
            if (is_icon(win))
                return;
            if (!has_xembed_info(win))
                return;
            LOG_DEBUG("TrayHost 0x%x: adopting icon 0x%x", tray_win_, win);
            add_icon(win);
        }

        void relayout() {
            if (tray_win_ == XCB_WINDOW_NONE)
                return;

            int x = ICON_SPACING;
            for (auto& ic : icons_) {
                if (!ic.mapped)
                    continue;
                int icon_h = std::min(ic.h, bar_h_);
                int icon_w = ic.w * icon_h / std::max(ic.h, 1);
                int icon_y = (bar_h_ - icon_h) / 2;
                ic.x = x;
                ic.y = icon_y;
                ic.w = icon_w;
                ic.h = icon_h;

                uint32_t vals[] = {
                    static_cast<uint32_t>(x),
                    static_cast<uint32_t>(icon_y),
                    static_cast<uint32_t>(icon_w),
                    static_cast<uint32_t>(icon_h),
                };
                xcb_configure_window(conn_, ic.win,
                    XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y |
                    XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT,
                    vals);
                x += icon_w + ICON_SPACING;
            }

            tray_w_ = (x > ICON_SPACING) ? x : 0;
            if (tray_w_ > 0) {
                uint32_t geom[] = {
                    static_cast<uint32_t>(tray_w_),
                    static_cast<uint32_t>(bar_h_),
                };
                xcb_configure_window(conn_, tray_win_,
                    XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT,
                    geom);
                xcb_map_window(conn_, tray_win_);
            } else {
                xcb_unmap_window(conn_, tray_win_);
            }
            xcb_flush(conn_);
        }

        void send_xembed(WindowId win, uint32_t message,
            uint32_t detail = 0, uint32_t data1 = 0, uint32_t data2 = 0) {
            xcb_client_message_event_t ev = {};
            ev.response_type  = XCB_CLIENT_MESSAGE;
            ev.format         = 32;
            ev.window         = win;
            ev.type           = XEMBED_;
            ev.data.data32[0] = XCB_CURRENT_TIME;
            ev.data.data32[1] = message;
            ev.data.data32[2] = detail;
            ev.data.data32[3] = data1;
            ev.data.data32[4] = data2;
            xcb_send_event(conn_, 0, win, XCB_EVENT_MASK_STRUCTURE_NOTIFY, (const char*)&ev);
        }

        bool is_icon(WindowId win) const {
            return std::any_of(icons_.begin(), icons_.end(),
                       [win](const Icon& i) {
                           return i.win == win;
                       });
        }

        Icon* find_icon(WindowId win) {
            auto it = std::find_if(icons_.begin(), icons_.end(),
                    [win](const Icon& i) {
                        return i.win == win;
                    });
            return (it != icons_.end()) ? &*it : nullptr;
        }

        Icon* find_icon_at(int x, int y) {
            for (auto& ic : icons_) {
                if (!ic.mapped)
                    continue;
                if (x >= ic.x && x < ic.x + ic.w &&
                    y >= ic.y && y < ic.y + ic.h)
                    return &ic;
            }
            return nullptr;
        }

        bool xembed_info_mapped(WindowId win) {
            auto cookie = xcb_get_property(conn_, 0, win, XEMBED_INFO_, XCB_ATOM_ANY, 0, 2);
            auto reply  = xcb_get_property_reply(conn_, cookie, nullptr);
            if (!reply)
                return false;
            bool mapped = false;
            if (reply->format == 32 && reply->length >= 2) {
                auto* data = (uint32_t*)xcb_get_property_value(reply);
                mapped = (data[1] & XEMBED_MAPPED) != 0;
            }
            free(reply);
            return mapped;
        }

        bool has_xembed_info(WindowId win) const {
            auto cookie = xcb_get_property(conn_, 0, win, XEMBED_INFO_, XCB_ATOM_ANY, 0, 2);
            auto reply  = xcb_get_property_reply(conn_, cookie, nullptr);
            if (!reply)
                return false;
            bool ok = (reply->format == 32 && reply->length >= 2);
            free(reply);
            return ok;
        }

        xcb_connection_t* conn_ = nullptr;
        xcb_screen_t* screen_   = nullptr;

        WindowId bar_win_       = NO_WINDOW;
        int bar_x_              = 0;
        int bar_y_              = 0;
        int bar_h_              = 0;
        int tray_x_             = 0;
        int tray_y_             = 0;
        uint32_t bg_pixel_      = 0;

        WindowId tray_win_      = NO_WINDOW;
        int tray_w_             = 0;
        bool selection_owner_   = false;
        bool visible_           = false;

        std::vector<Icon> icons_;

        uint32_t NET_SYSTEM_TRAY_S_      = 0;
        uint32_t NET_SYSTEM_TRAY_OP_     = 0;
        uint32_t NET_SYSTEM_TRAY_ORIENT_ = 0;
        uint32_t XEMBED_                 = 0;
        uint32_t XEMBED_INFO_            = 0;
        uint32_t MANAGER_                = 0;
};

} // namespace

namespace backend::x11 {

std::unique_ptr<backend::TrayHost>
create_tray_host(XConnection& xconn,
    WindowId owner_bar_window,
    int bar_x, int bar_y, int bar_h,
    uint32_t bg_pixel,
    bool own_selection) {
    return std::make_unique<X11TrayHost>(xconn, owner_bar_window, bar_x, bar_y, bar_h, bg_pixel, own_selection);
}

} // namespace backend::x11