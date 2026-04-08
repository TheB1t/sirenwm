#include <x11_ports.hpp>

#include <xconn.hpp>
#include <xcb/atom.hpp>

#include <cairo/cairo.h>
#include <cairo/cairo-xcb.h>

#include <algorithm>
#include <memory>

namespace {

xcb_visualtype_t* find_root_visual(xcb_screen_t* screen) {
    if (!screen)
        return nullptr;
    for (auto di = xcb_screen_allowed_depths_iterator(screen); di.rem; xcb_depth_next(&di)) {
        for (auto vi = xcb_depth_visuals_iterator(di.data); vi.rem; xcb_visualtype_next(&vi)) {
            if (vi.data->visual_id == screen->root_visual)
                return vi.data;
        }
    }
    return nullptr;
}

class X11RenderWindow final : public backend::RenderWindow {
    public:
        X11RenderWindow(XConnection& xconn, const backend::RenderWindowCreateInfo& info)
            : xconn_(xconn),
              conn_(xconn.raw_conn()),
              screen_(xconn.raw_screen()),
              monitor_index_(info.monitor_index),
              x_(info.pos.x()),
              y_(info.pos.y()),
              w_(std::max(1, info.size.x())),
              h_(std::max(1, info.size.y())) {
            if (!conn_ || !screen_)
                return;

            auto atoms = xcb::intern_batch(conn_, {
                "_NET_WM_STRUT_PARTIAL",
                "_NET_WM_STRUT",
                "_NET_WM_WINDOW_TYPE",
                "_NET_WM_WINDOW_TYPE_DOCK",
                "_NET_WM_STATE",
                "_NET_WM_STATE_ABOVE",
            });
            NET_WM_STRUT_PARTIAL_    = atoms["_NET_WM_STRUT_PARTIAL"];
            NET_WM_STRUT_            = atoms["_NET_WM_STRUT"];
            NET_WM_WINDOW_TYPE_      = atoms["_NET_WM_WINDOW_TYPE"];
            NET_WM_WINDOW_TYPE_DOCK_ = atoms["_NET_WM_WINDOW_TYPE_DOCK"];
            NET_WM_STATE_            = atoms["_NET_WM_STATE"];
            NET_WM_STATE_ABOVE_      = atoms["_NET_WM_STATE_ABOVE"];

            win_ = xcb_generate_id(conn_);
            uint32_t mask    = XCB_CW_BACK_PIXEL;
            uint32_t vals[3] = {};
            int      vcount  = 0;
            vals[vcount++] = info.background_pixel;
            if (info.hints.override_redirect) {
                mask          |= XCB_CW_OVERRIDE_REDIRECT;
                vals[vcount++] = 1u;
            }
            uint32_t event_mask = 0;
            if (info.want_expose)
                event_mask |= XCB_EVENT_MASK_EXPOSURE;
            if (info.want_button_press)
                event_mask |= XCB_EVENT_MASK_BUTTON_PRESS;
            if (info.want_button_release)
                event_mask |= XCB_EVENT_MASK_BUTTON_RELEASE;
            if (event_mask != 0) {
                mask          |= XCB_CW_EVENT_MASK;
                vals[vcount++] = event_mask;
            }

            xcb_create_window(conn_, XCB_COPY_FROM_PARENT, win_, screen_->root,
                (int16_t)x_, (int16_t)y_, (uint16_t)w_, (uint16_t)h_, 0,
                XCB_WINDOW_CLASS_INPUT_OUTPUT,
                XCB_COPY_FROM_PARENT,
                mask, vals);

            buf_ = xcb_generate_id(conn_);
            xcb_create_pixmap(conn_, screen_->root_depth, buf_, win_, (uint16_t)w_, (uint16_t)h_);

            if (info.hints.dock) {
                xcb_change_property(conn_, XCB_PROP_MODE_REPLACE, win_,
                    NET_WM_WINDOW_TYPE_, XCB_ATOM_ATOM, 32,
                    1, &NET_WM_WINDOW_TYPE_DOCK_);
            }
            if (info.hints.keep_above) {
                xcb_change_property(conn_, XCB_PROP_MODE_REPLACE, win_,
                    NET_WM_STATE_, XCB_ATOM_ATOM, 32,
                    1, &NET_WM_STATE_ABOVE_);
            }

            auto* visual = find_root_visual(screen_);
            if (visual) {
                surface_ = cairo_xcb_surface_create(conn_, buf_, visual, w_, h_);
                cr_      = cairo_create(surface_);
            }

            xcb_map_window(conn_, win_);
            xcb_flush(conn_);
        }

        ~X11RenderWindow() override {
            if (cr_)
                cairo_destroy(cr_);
            if (surface_)
                cairo_surface_destroy(surface_);
            if (buf_ != XCB_PIXMAP_NONE)
                xcb_free_pixmap(conn_, buf_);
            if (win_ != XCB_WINDOW_NONE)
                xcb_destroy_window(conn_, win_);
            if (conn_)
                xcb_flush(conn_);
        }

        WindowId id() const override { return win_; }
        int monitor_index() const override { return monitor_index_; }
        int x() const override { return x_; }
        int y() const override { return y_; }
        int width() const override { return w_; }
        int height() const override { return h_; }

        cairo_t* cairo() override { return cr_; }

        void present() override {
            if (!conn_ || win_ == XCB_WINDOW_NONE || buf_ == XCB_PIXMAP_NONE)
                return;
            if (surface_)
                cairo_surface_flush(surface_);
            xcb_gcontext_t gc = xcb_generate_id(conn_);
            xcb_create_gc(conn_, gc, win_, 0, nullptr);
            xcb_copy_area(conn_, buf_, win_, gc, 0, 0, 0, 0, (uint16_t)w_, (uint16_t)h_);
            xcb_free_gc(conn_, gc);
            xcb_flush(conn_);
        }

        void set_visible(bool visible) override {
            if (!conn_ || win_ == XCB_WINDOW_NONE)
                return;
            if (visible)
                xcb_map_window(conn_, win_);
            else
                xcb_unmap_window(conn_, win_);
            xcb_flush(conn_);
        }

        void raise() override {
            if (!conn_ || win_ == XCB_WINDOW_NONE)
                return;
            uint32_t vals[] = { XCB_STACK_MODE_ABOVE };
            xcb_configure_window(conn_, win_, XCB_CONFIG_WINDOW_STACK_MODE, vals);
            xcb_flush(conn_);
        }

        void lower() override {
            if (!conn_ || win_ == XCB_WINDOW_NONE)
                return;
            uint32_t vals[] = { XCB_STACK_MODE_BELOW };
            xcb_configure_window(conn_, win_, XCB_CONFIG_WINDOW_STACK_MODE, vals);
            xcb_flush(conn_);
        }

        void move_to(int x, int y) override {
            if (!conn_ || win_ == XCB_WINDOW_NONE)
                return;
            x_ = x;
            y_ = y;
            uint32_t vals[] = {
                static_cast<uint32_t>(x_),
                static_cast<uint32_t>(y_),
            };
            xcb_configure_window(conn_, win_,
                XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y,
                vals);
            xcb_flush(conn_);
        }

        void reserve_top_strut(int strut_height, int x_start, int x_end) override {
            if (!conn_ || win_ == XCB_WINDOW_NONE ||
                NET_WM_STRUT_PARTIAL_ == XCB_ATOM_NONE || NET_WM_STRUT_ == XCB_ATOM_NONE)
                return;
            uint32_t strut[12] = {};
            strut[2] = static_cast<uint32_t>(std::max(0, strut_height));
            strut[8] = static_cast<uint32_t>(std::max(0, x_start));
            strut[9] = static_cast<uint32_t>(std::max(0, x_end));
            xcb_change_property(conn_, XCB_PROP_MODE_REPLACE, win_,
                NET_WM_STRUT_PARTIAL_, XCB_ATOM_CARDINAL, 32, 12, strut);
            xcb_change_property(conn_, XCB_PROP_MODE_REPLACE, win_,
                NET_WM_STRUT_, XCB_ATOM_CARDINAL, 32, 4, strut);
            xcb_flush(conn_);
        }

        void reserve_bottom_strut(int strut_height, int x_start, int x_end) override {
            if (!conn_ || win_ == XCB_WINDOW_NONE ||
                NET_WM_STRUT_PARTIAL_ == XCB_ATOM_NONE || NET_WM_STRUT_ == XCB_ATOM_NONE)
                return;
            uint32_t strut[12] = {};
            strut[3]  = static_cast<uint32_t>(std::max(0, strut_height));
            strut[10] = static_cast<uint32_t>(std::max(0, x_start));
            strut[11] = static_cast<uint32_t>(std::max(0, x_end));
            xcb_change_property(conn_, XCB_PROP_MODE_REPLACE, win_,
                NET_WM_STRUT_PARTIAL_, XCB_ATOM_CARDINAL, 32, 12, strut);
            xcb_change_property(conn_, XCB_PROP_MODE_REPLACE, win_,
                NET_WM_STRUT_, XCB_ATOM_CARDINAL, 32, 4, strut);
            xcb_flush(conn_);
        }

    private:
        XConnection&      xconn_;
        xcb_connection_t* conn_   = nullptr;
        xcb_screen_t*     screen_ = nullptr;

        xcb_window_t win_           = XCB_WINDOW_NONE;
        xcb_pixmap_t buf_           = XCB_PIXMAP_NONE;
        int          monitor_index_ = -1;
        int          x_             = 0;
        int          y_             = 0;
        int          w_             = 1;
        int          h_             = 1;

        cairo_surface_t* surface_ = nullptr;
        cairo_t*         cr_      = nullptr;

        xcb_atom_t NET_WM_STRUT_PARTIAL_    = XCB_ATOM_NONE;
        xcb_atom_t NET_WM_STRUT_            = XCB_ATOM_NONE;
        xcb_atom_t NET_WM_WINDOW_TYPE_      = XCB_ATOM_NONE;
        xcb_atom_t NET_WM_WINDOW_TYPE_DOCK_ = XCB_ATOM_NONE;
        xcb_atom_t NET_WM_STATE_            = XCB_ATOM_NONE;
        xcb_atom_t NET_WM_STATE_ABOVE_      = XCB_ATOM_NONE;
};

class X11RenderPort final : public backend::RenderPort {
    public:
        explicit X11RenderPort(XConnection& xconn) : xconn_(xconn) {}

        std::unique_ptr<backend::RenderWindow>
        create_window(const backend::RenderWindowCreateInfo& info) override {
            return std::make_unique<X11RenderWindow>(xconn_, info);
        }

        uint32_t black_pixel() const override {
            return xconn_.screen_black_pixel();
        }

    private:
        XConnection& xconn_;
};

} // namespace

namespace backend::x11 {

std::unique_ptr<backend::RenderPort> create_render_port(XConnection& xconn) {
    return std::make_unique<X11RenderPort>(xconn);
}

} // namespace backend::x11
