#include <x11_backend.hpp>
#include <backend/input_port.hpp>
#include <xconn.hpp>
#include <log.hpp>

#include <xcb/xcb.h>
#include <xcb/xcb_keysyms.h>
#include <cstdlib>

namespace {

class X11InputPort final : public backend::InputPort {
    XConnection& xconn;
    xcb_key_symbols_t*& key_syms; // reference to X11Backend-owned pointer

    xcb_key_symbols_t* ensure_key_syms() {
        if (!key_syms)
            key_syms = xconn.alloc_key_symbols();
        return key_syms;
    }

    public:
        X11InputPort(XConnection& xconn_ref, xcb_key_symbols_t*& syms_ref)
            : xconn(xconn_ref), key_syms(syms_ref) {}

        void grab_key(uint32_t keysym, uint16_t mods) override {
            auto* syms = ensure_key_syms();
            if (!syms) {
                LOG_ERR("InputPort: failed to allocate key symbols");
                return;
            }

            xcb_keycode_t* codes = xcb_key_symbols_get_keycode(syms, keysym);
            if (!codes)
                return;

            xcb_window_t root = xconn.root_window();
            for (xcb_keycode_t* kc = codes; *kc != XCB_NO_SYMBOL; kc++) {
                xconn.call(xcb_grab_key, 1, root, mods, *kc,
                    XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
                xconn.call(xcb_grab_key, 1, root, (uint16_t)(mods | XCB_MOD_MASK_2), *kc,
                    XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
                xconn.call(xcb_grab_key, 1, root, (uint16_t)(mods | XCB_MOD_MASK_LOCK), *kc,
                    XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
                xconn.call(xcb_grab_key, 1, root, (uint16_t)(mods | XCB_MOD_MASK_2 | XCB_MOD_MASK_LOCK), *kc,
                    XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
            }
            free(codes);
        }

        void ungrab_all_keys() override {
            xconn.call(xcb_ungrab_key, XCB_GRAB_ANY, xconn.root_window(), XCB_MOD_MASK_ANY);
        }

        void grab_button(WindowId window, uint8_t button, uint16_t mods) override {
            constexpr uint32_t evmask     =
                XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_POINTER_MOTION;
            const uint16_t     variants[] = {
                mods,
                (uint16_t)(mods | XCB_MOD_MASK_2),
                (uint16_t)(mods | XCB_MOD_MASK_LOCK),
                (uint16_t)(mods | XCB_MOD_MASK_2 | XCB_MOD_MASK_LOCK),
            };
            for (uint16_t v : variants) {
                xconn.call(xcb_grab_button, 0, (xcb_window_t)window, evmask,
                    XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC,
                    XCB_WINDOW_NONE, XCB_CURSOR_NONE, button, v);
            }
        }

        void ungrab_all_buttons(WindowId window) override {
            xconn.call(xcb_ungrab_button, XCB_BUTTON_INDEX_ANY, (xcb_window_t)window, XCB_MOD_MASK_ANY);
        }

        void grab_pointer() override {
            constexpr uint32_t mask =
                XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_POINTER_MOTION;
            xconn.grab_pointer(XCB_WINDOW_NONE, mask);
        }

        void ungrab_pointer() override {
            xconn.ungrab_pointer();
        }

        void warp_pointer(WindowId window, int16_t x, int16_t y) override {
            xconn.call(xcb_warp_pointer,
                XCB_WINDOW_NONE, (xcb_window_t)window,
                (int16_t)0, (int16_t)0, (uint16_t)0, (uint16_t)0,
                x, y);
        }

        void warp_pointer_abs(int16_t x, int16_t y) override {
            xconn.call(xcb_warp_pointer,
                XCB_WINDOW_NONE, xconn.root_window(),
                (int16_t)0, (int16_t)0, (uint16_t)0, (uint16_t)0,
                x, y);
        }

        void focus_window(WindowId window) override {
            xconn.focus_window((xcb_window_t)window);
        }

        void flush() override {
            xconn.flush();
        }
};

} // namespace

backend::InputPort* X11Backend::input_port() {
    return input_port_impl.get();
}

xcb_key_symbols_t* X11Backend::key_symbols() {
    if (!key_syms)
        key_syms = xconn.alloc_key_symbols();
    return key_syms;
}

namespace backend::x11 {

std::unique_ptr<backend::InputPort> create_input_port(XConnection& xconn, xcb_key_symbols_t*& key_syms) {
    return std::make_unique<X11InputPort>(xconn, key_syms);
}

} // namespace backend::x11