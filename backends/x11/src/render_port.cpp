#include <x11_ports.hpp>

#include <xconn.hpp>
#include <xcb/atom.hpp>
#include <xcb/visual.hpp>

#include <cairo/cairo.h>
#include <cairo/cairo-xcb.h>

#include <algorithm>
#include <memory>

namespace {

class X11RenderWindow final : public backend::RenderWindow {
    public:
        X11RenderWindow(XConnection& xc, const backend::RenderWindowCreateInfo& info)
            : xc_(xc),
              monitor_index_(info.monitor_index),
              x_(info.pos.x()),
              y_(info.pos.y()),
              w_(std::max(1, info.size.x())),
              h_(std::max(1, info.size.y())) {
            if (!xc_.raw()) return;

            auto atoms = xc_.intern_atoms({
                "_NET_WM_WINDOW_TYPE",
                "_NET_WM_WINDOW_TYPE_DOCK",
                "_NET_WM_STATE",
                "_NET_WM_STATE_ABOVE",
            });
            NET_WM_WINDOW_TYPE_      = atoms["_NET_WM_WINDOW_TYPE"];
            NET_WM_WINDOW_TYPE_DOCK_ = atoms["_NET_WM_WINDOW_TYPE_DOCK"];
            NET_WM_STATE_            = atoms["_NET_WM_STATE"];
            NET_WM_STATE_ABOVE_      = atoms["_NET_WM_STATE_ABOVE"];

            win_ = xc_.generate_id();
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

            xc_.create_window(win_, xc_.root_window(),
                (int16_t)x_, (int16_t)y_, (uint16_t)w_, (uint16_t)h_,
                XCB_WINDOW_CLASS_INPUT_OUTPUT,
                XCB_COPY_FROM_PARENT, mask, vals);

            buf_ = xc_.create_pixmap(xc_.screen_root_depth(), win_,
                    (uint16_t)w_, (uint16_t)h_);

            if (info.hints.dock) {
                xc_.change_property(win_, NET_WM_WINDOW_TYPE_, XCB_ATOM_ATOM,
                    32, 1, &NET_WM_WINDOW_TYPE_DOCK_);
            }
            if (info.hints.keep_above) {
                xc_.change_property(win_, NET_WM_STATE_, XCB_ATOM_ATOM,
                    32, 1, &NET_WM_STATE_ABOVE_);
            }

            auto* visual = xcb::find_visual(xc_.raw_screen(), xc_.raw_screen()->root_visual);
            if (visual) {
                surface_ = cairo_xcb_surface_create(xc_.raw_conn(), buf_, visual, w_, h_);
                cr_      = cairo_create(surface_);
            }

            xc_.map_window(win_);
            xc_.force_flush();
        }

        ~X11RenderWindow() override {
            if (cr_)
                cairo_destroy(cr_);
            if (surface_)
                cairo_surface_destroy(surface_);
            if (buf_ != XCB_PIXMAP_NONE)
                xc_.free_pixmap(buf_);
            if (win_ != XCB_WINDOW_NONE)
                xc_.destroy_window(win_);
            xc_.force_flush();
        }

        WindowId id() const override { return win_; }
        int monitor_index() const override { return monitor_index_; }
        int x() const override { return x_; }
        int y() const override { return y_; }
        int width() const override { return w_; }
        int height() const override { return h_; }

        cairo_t* cairo() override { return cr_; }

        void present() override {
            if (win_ == XCB_WINDOW_NONE || buf_ == XCB_PIXMAP_NONE)
                return;
            if (surface_)
                cairo_surface_flush(surface_);
            xc_.copy_area(buf_, win_, 0, 0, 0, 0, (uint16_t)w_, (uint16_t)h_);
            xc_.force_flush();
        }

        void set_visible(bool visible) override {
            if (win_ == XCB_WINDOW_NONE) return;
            if (visible)
                xc_.map_window(win_);
            else
                xc_.unmap_window(win_);
            xc_.force_flush();
        }

        void raise() override {
            if (win_ == XCB_WINDOW_NONE) return;
            uint32_t vals[] = { XCB_STACK_MODE_ABOVE };
            xc_.configure_window(win_, XCB_CONFIG_WINDOW_STACK_MODE, vals);
            xc_.force_flush();
        }

        void lower() override {
            if (win_ == XCB_WINDOW_NONE) return;
            uint32_t vals[] = { XCB_STACK_MODE_BELOW };
            xc_.configure_window(win_, XCB_CONFIG_WINDOW_STACK_MODE, vals);
            xc_.force_flush();
        }

    private:
        XConnection& xc_;

        xcb_window_t win_           = XCB_WINDOW_NONE;
        xcb_pixmap_t buf_           = XCB_PIXMAP_NONE;
        int          monitor_index_ = -1;
        int          x_             = 0;
        int          y_             = 0;
        int          w_             = 1;
        int          h_             = 1;

        cairo_surface_t* surface_ = nullptr;
        cairo_t*         cr_      = nullptr;

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
