#include <x11_ports.hpp>

#include <domain/core.hpp>
#include <x11/xconn.hpp>
#include <xcb/atom.hpp>
#include <support/log.hpp>

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
            : xc_(xconn),
              bar_win_(bar_win),
              bar_pos_{ bar_x, bar_y },
              bar_h_(bar_h),
              bg_pixel_(bg_pixel) {
            if (!xc_) return;

            tray_pos_ = bar_pos_;
            intern_atoms();

            tray_win_ = xc_.generate_id();
            uint32_t mask   = XCB_CW_BACK_PIXEL | XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK;
            uint32_t vals[] = {
                bg_pixel_,
                1u,
                XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY | XCB_EVENT_MASK_BUTTON_PRESS |
                XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_EXPOSURE,
            };
            xc_.create_window(tray_win_, xc_.root(),
                (int16_t)bar_pos_.x(), (int16_t)bar_pos_.y(), (uint16_t)1, (uint16_t)bar_h_,
                XCB_WINDOW_CLASS_INPUT_OUTPUT, XCB_COPY_FROM_PARENT,
                mask, vals);

            uint32_t orient = 0;
            xc_.change_property(tray_win_, NET_SYSTEM_TRAY_ORIENT_,
                XCB_ATOM_CARDINAL, 32, 1, &orient);

            if (own_selection) {
                if (!try_acquire_selection()) {
                    LOG_ERR("TrayHost: cannot acquire _NET_SYSTEM_TRAY_S");
                    xc_.destroy_window(tray_win_);
                    tray_win_ = XCB_WINDOW_NONE;
                    return;
                }
                selection_owner_ = true;
            }

            xc_.map_window(tray_win_);
            xc_.force_flush();
            visible_ = true;
            if (selection_owner_) {
                broadcast_manager();
                adopt_orphaned_icons();
            }
        }

        ~X11TrayHost() override {
            if (!xc_) return;
            for (auto& ic : icons_) {
                xc_.save_set_remove(ic.win);
                xc_.reparent_window(ic.win, xc_.root(), 0, 0);
                if (ic.mapped)
                    xc_.map_window(ic.win);
            }
            icons_.clear();
            if (tray_win_ != XCB_WINDOW_NONE)
                xc_.destroy_window(tray_win_);
            xc_.force_flush();
        }

        WindowId window() const override { return tray_win_; }
        bool owns_selection() const override { return selection_owner_; }
        int width() const override { return tray_w_; }

        void set_visible(bool visible) override {
            if (!xc_ || tray_win_ == XCB_WINDOW_NONE)
                return;
            if (visible == visible_)
                return;
            visible_ = visible;
            if (visible)
                xc_.map_window(tray_win_);
            else
                xc_.unmap_window(tray_win_);
            xc_.force_flush();
        }

        void reposition(int bar_right_x, int bar_y) override {
            if (!xc_ || tray_win_ == XCB_WINDOW_NONE || tray_w_ == 0)
                return;
            tray_pos_.x() = bar_right_x - tray_w_;
            tray_pos_.y() = bar_y;
            uint32_t vals[] = {
                static_cast<uint32_t>(tray_pos_.x()),
                static_cast<uint32_t>(tray_pos_.y()),
            };
            xc_.configure_window(tray_win_,
                XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, vals);
            xc_.force_flush();
        }

        void attach_to_bar(WindowId new_bar_win, int bar_x, int bar_y, int bar_w) override {
            if (!xc_ || tray_win_ == XCB_WINDOW_NONE)
                return;
            bar_win_      = new_bar_win;
            bar_pos_      = { bar_x, bar_y };
            tray_pos_.x() = bar_x + bar_w - std::max(tray_w_, 1);
            tray_pos_.y() = bar_y;
            uint32_t vals[] = {
                static_cast<uint32_t>(tray_pos_.x()),
                static_cast<uint32_t>(tray_pos_.y()),
            };
            xc_.configure_window(tray_win_,
                XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, vals);
            xc_.force_flush();
            LOG_DEBUG("TrayHost 0x%x: attached to bar 0x%x at %d+%d", tray_win_, new_bar_win, tray_pos_.x(), tray_pos_.y());
        }

        void raise() override {
            if (!xc_ || tray_win_ == XCB_WINDOW_NONE || bar_win_ == XCB_WINDOW_NONE)
                return;
            uint32_t vals[] = {
                static_cast<uint32_t>(bar_win_),
                XCB_STACK_MODE_ABOVE,
            };
            xc_.configure_window(tray_win_,
                XCB_CONFIG_WINDOW_SIBLING | XCB_CONFIG_WINDOW_STACK_MODE, vals);
            xc_.force_flush();
        }

        void lower() override {
            if (!xc_ || tray_win_ == XCB_WINDOW_NONE)
                return;
            uint32_t vals[] = { XCB_STACK_MODE_BELOW };
            xc_.configure_window(tray_win_, XCB_CONFIG_WINDOW_STACK_MODE, vals);
            xc_.force_flush();
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
            if (!xc_ || !is_icon(win))
                return {};
            auto [instance, cls] = xc_.get_wm_class(win);
            for (auto& c : cls) c = (char)tolower((unsigned char)c);
            return cls;
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

            other->icons_.push_back(*ic);
            icons_.erase(std::find_if(icons_.begin(), icons_.end(),
                [win](const Icon& i) {
                    return i.win == win;
                }));

            uint32_t mask   = XCB_CW_EVENT_MASK;
            uint32_t vals[] = { XCB_EVENT_MASK_PROPERTY_CHANGE | XCB_EVENT_MASK_STRUCTURE_NOTIFY };
            xc_.change_window_attributes(win, mask, vals);

            xc_.reparent_window(win, other->tray_win_, 0, 0);
            send_xembed(win, XEMBED_EMBEDDED_NOTIFY, 0, other->tray_win_, XEMBED_VERSION);
            if (other->icons_.back().mapped)
                xc_.map_window(win);

            relayout();
            other->relayout();
            xc_.force_flush();
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
            if (opcode != 0)
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
                xc_.map_window(win);
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
            auto geo = xc_.get_window_geometry(win);
            if (!geo)
                return true;
            int new_w = std::max<int>(geo->width, 1);
            int new_h = std::max<int>(geo->height, 1);
            if (new_w == ic->size.x() && new_h == ic->size.y())
                return true;
            ic->size.x() = new_w;
            ic->size.y() = new_h;
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
                xc_.map_window(win);
                ic->mapped = true;
                relayout();
            } else if (!should_map && ic->mapped) {
                xc_.unmap_window(win);
                ic->mapped = false;
                relayout();
            }
            return true;
        }

        bool handle_button_event(const event::ButtonEv& ev) override {
            if (tray_win_ == XCB_WINDOW_NONE || tray_w_ <= 0)
                return false;

            int local_x = ev.event_pos.x();
            int local_y = ev.event_pos.y();
            if (ev.window == bar_win_) {
                local_x = static_cast<int>(ev.root_pos.x()) - tray_pos_.x();
                local_y = static_cast<int>(ev.root_pos.y()) - tray_pos_.y();
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

            int16_t                  icon_x = (int16_t)std::max(0, local_x - ic->pos.x());
            int16_t                  icon_y = (int16_t)std::max(0, local_y - ic->pos.y());

            xcb_button_press_event_t bev = {};
            bev.response_type = ev.release ? XCB_BUTTON_RELEASE : XCB_BUTTON_PRESS;
            bev.detail        = ev.button;
            bev.time          = ev.time;
            bev.root          = xc_.root();
            bev.event         = ic->win;
            bev.child         = XCB_WINDOW_NONE;
            bev.root_x        = ev.root_pos.x();
            bev.root_y        = ev.root_pos.y();
            bev.event_x       = icon_x;
            bev.event_y       = icon_y;
            bev.state         = ev.state;
            bev.same_screen   = 1;

            xc_.send_event(ic->win, XCB_EVENT_MASK_NO_EVENT, (const char*)&bev);
            xc_.force_flush();
            return true;
        }

    private:
        struct Icon {
            WindowId win = NO_WINDOW;
            Vec2i    pos;
            Vec2i    size   = { 1, 1 };
            bool     mapped = false;
        };

        static constexpr int      ICON_SPACING           = 2;
        static constexpr uint32_t XEMBED_EMBEDDED_NOTIFY = 0;
        static constexpr uint32_t XEMBED_MAPPED          = (1u << 0);
        static constexpr uint32_t XEMBED_VERSION         = 0;

        void intern_atoms() {
            char tray_sel[32];
            std::snprintf(tray_sel, sizeof(tray_sel), "_NET_SYSTEM_TRAY_S%d",
                xc_.screen_number());

            auto atoms = xcb::intern_batch(xc_.raw(), {
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
            xc_.set_selection_owner(tray_win_, NET_SYSTEM_TRAY_S_);
            xc_.force_flush();
            return xc_.get_selection_owner(NET_SYSTEM_TRAY_S_) == tray_win_;
        }

        void broadcast_manager() {
            xcb_client_message_event_t ev = {};
            ev.response_type  = XCB_CLIENT_MESSAGE;
            ev.format         = 32;
            ev.window         = xc_.root();
            ev.type           = MANAGER_;
            ev.data.data32[0] = XCB_CURRENT_TIME;
            ev.data.data32[1] = NET_SYSTEM_TRAY_S_;
            ev.data.data32[2] = tray_win_;
            ev.data.data32[3] = 0;
            ev.data.data32[4] = 0;

            xc_.send_event(xc_.root(),
                XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT,
                (const char*)&ev);
            xc_.force_flush();
        }

        void add_icon(WindowId win) {
            if (is_icon(win) || tray_win_ == XCB_WINDOW_NONE)
                return;

            auto geo = xc_.get_window_geometry(win);
            if (geo && (geo->width >= 256 || geo->height >= 256)) {
                LOG_DEBUG("TrayHost: rejecting oversized dock request from 0x%x (%ux%u)",
                    win, geo->width, geo->height);
                return;
            }

            uint32_t mask   = XCB_CW_EVENT_MASK;
            uint32_t vals[] = { XCB_EVENT_MASK_PROPERTY_CHANGE | XCB_EVENT_MASK_STRUCTURE_NOTIFY };
            xc_.change_window_attributes(win, mask, vals);
            xc_.save_set_add(win);
            xc_.reparent_window(win, tray_win_, 0, 0);

            Icon ic;
            ic.win      = win;
            ic.size.x() = bar_h_;
            ic.size.y() = bar_h_;
            ic.mapped   = !has_xembed_info(win) || xembed_info_mapped(win);

            auto geo2 = xc_.get_window_geometry(win);
            if (geo2) {
                if (geo2->width > 1) ic.size.x() = geo2->width;
                if (geo2->height > 1) ic.size.y() = geo2->height;
            }

            icons_.push_back(ic);
            send_xembed(win, XEMBED_EMBEDDED_NOTIFY, 0, tray_win_, XEMBED_VERSION);
            if (ic.mapped)
                xc_.map_window(win);

            relayout();
            xc_.force_flush();
        }

        void remove_icon(WindowId win) {
            auto it = std::find_if(icons_.begin(), icons_.end(),
                    [win](const Icon& i) {
                        return i.win == win;
                    });
            if (it == icons_.end())
                return;
            xc_.save_set_remove(win);
            icons_.erase(it);
            relayout();
        }

        void adopt_orphaned_icons() {
            if (tray_win_ == XCB_WINDOW_NONE)
                return;
            auto children = xc_.query_tree_children(xc_.root());
            for (auto win : children) {
                if (win == tray_win_ || is_icon(win))
                    continue;
                if (!has_xembed_info(win))
                    continue;
                add_icon(win);
            }
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
                int icon_h = std::min(ic.size.y(), bar_h_);
                int icon_w = ic.size.x() * icon_h / std::max(ic.size.y(), 1);
                int icon_y = (bar_h_ - icon_h) / 2;
                ic.pos.x()  = x;
                ic.pos.y()  = icon_y;
                ic.size.x() = icon_w;
                ic.size.y() = icon_h;

                uint32_t vals[] = {
                    static_cast<uint32_t>(x),
                    static_cast<uint32_t>(icon_y),
                    static_cast<uint32_t>(icon_w),
                    static_cast<uint32_t>(icon_h),
                };
                xc_.configure_window(ic.win,
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
                xc_.configure_window(tray_win_,
                    XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT,
                    geom);
                xc_.map_window(tray_win_);
            } else {
                xc_.unmap_window(tray_win_);
            }
            xc_.force_flush();
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
            xc_.send_event(win, XCB_EVENT_MASK_STRUCTURE_NOTIFY, (const char*)&ev);
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
                if (x >= ic.pos.x() && x < ic.pos.x() + ic.size.x() &&
                    y >= ic.pos.y() && y < ic.pos.y() + ic.size.y())
                    return &ic;
            }
            return nullptr;
        }

        bool xembed_info_mapped(WindowId win) {
            auto vals = xc_.get_property_u32(win, XEMBED_INFO_, 2);
            return vals.size() >= 2 && (vals[1] & XEMBED_MAPPED) != 0;
        }

        bool has_xembed_info(WindowId win) const {
            return xc_.has_property_32(win, XEMBED_INFO_, 2);
        }

        XConnection& xc_;

        WindowId bar_win_ = NO_WINDOW;
        Vec2i    bar_pos_;
        int      bar_h_ = 0;
        Vec2i    tray_pos_;

        uint32_t bg_pixel_ = 0;

        WindowId tray_win_        = NO_WINDOW;
        int      tray_w_          = 0;
        bool     selection_owner_ = false;
        bool     visible_         = false;

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

namespace {

class X11TrayHostPort final : public backend::TrayHostPort {
    public:
        X11TrayHostPort(XConnection& xconn, Core& core)
            : xconn_(xconn), core_(core) {}

        std::unique_ptr<backend::TrayHost>
        create(WindowId owner_bar_window, int bar_x, int bar_y, int bar_h, bool own_selection) override {
            const auto& bg_str = core_.current_settings().theme.bg;
            uint32_t    bg     = bg_str.empty() ? xconn_.screen_black_pixel()
                                                : XConnection::parse_color_hex(bg_str);
            return create_tray_host(xconn_, owner_bar_window, bar_x, bar_y, bar_h, bg, own_selection);
        }

    private:
        XConnection& xconn_;
        Core&        core_;
};

} // namespace

std::unique_ptr<backend::TrayHostPort>
create_tray_host_port(XConnection& xconn, Core& core) {
    return std::make_unique<X11TrayHostPort>(xconn, core);
}

} // namespace backend::x11
