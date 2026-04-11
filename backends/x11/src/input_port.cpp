#include <x11_backend.hpp>
#include <backend/input_port.hpp>
#include <xconn.hpp>
#include <log.hpp>

#include <xcb/xcb.h>
#include <xcb/xcb_keysyms.h>
#include <xkbcommon/xkbcommon.h>
#include <cstdlib>

namespace {

class X11InputPort final : public backend::InputPort {
    XConnection&        xconn;
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
            constexpr uint32_t evmask =
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

        void grab_button_any(WindowId window) override {
            constexpr uint32_t evmask =
                XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE;
            // GrabModeSync freezes pointer on ButtonPress until xcb_allow_events().
            // The WM focuses the window then calls allow_events(replay=true) so
            // the click is re-delivered to the client — exact dwm click-to-focus model.
            xconn.call(xcb_grab_button, 0, (xcb_window_t)window, evmask,
                XCB_GRAB_MODE_SYNC, XCB_GRAB_MODE_ASYNC,
                XCB_WINDOW_NONE, XCB_CURSOR_NONE,
                XCB_BUTTON_INDEX_ANY, XCB_MOD_MASK_ANY);
        }

        void allow_events(bool replay) override {
            uint8_t mode = replay ? XCB_ALLOW_REPLAY_POINTER : XCB_ALLOW_ASYNC_POINTER;
            xconn.call(xcb_allow_events, mode, XCB_CURRENT_TIME);
            xconn.flush();
        }

        void grab_pointer() override {
            constexpr uint32_t mask =
                XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_POINTER_MOTION;
            xconn.grab_pointer(XCB_WINDOW_NONE, mask);
        }

        void ungrab_pointer() override {
            xconn.ungrab_pointer();
        }

        void warp_pointer(WindowId window, Vec2i16 pos) override {
            xconn.call(xcb_warp_pointer,
                XCB_WINDOW_NONE, (xcb_window_t)window,
                (int16_t)0, (int16_t)0, (uint16_t)0, (uint16_t)0,
                pos.x(), pos.y());
        }

        void warp_pointer_abs(Vec2i16 pos) override {
            xconn.call(xcb_warp_pointer,
                XCB_WINDOW_NONE, xconn.root_window(),
                (int16_t)0, (int16_t)0, (uint16_t)0, (uint16_t)0,
                pos.x(), pos.y());
        }

        void flush() override {
            xconn.flush();
        }
};

} // namespace

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

uint32_t backend::keysym_from_name(const std::string& name) {
    xkb_keysym_t sym = xkb_keysym_from_name(name.c_str(), XKB_KEYSYM_NO_FLAGS);
    return (sym == XKB_KEY_NoSymbol) ? 0 : sym;
}
