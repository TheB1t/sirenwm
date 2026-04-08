#pragma once

#include <memory>
#include <xcb/xcb.h>
#include <xcb/xcb_keysyms.h>

#include <backend/input_port.hpp>
#include <backend/keyboard_port.hpp>
#include <backend/monitor_port.hpp>
#include <backend/render_port.hpp>
#ifdef SIRENWM_DEBUG_UI
#include <backend/gl_port.hpp>
#endif
#include <backend/tray_host.hpp>

class XConnection;
class Runtime;

namespace backend::x11 {

std::unique_ptr<backend::InputPort>    create_input_port(XConnection& xconn, xcb_key_symbols_t*& key_syms);
std::unique_ptr<backend::KeyboardPort> create_keyboard_port(XConnection& xconn);
std::unique_ptr<backend::MonitorPort>  create_monitor_port(XConnection& xconn, Runtime& runtime);
std::unique_ptr<backend::RenderPort>   create_render_port(XConnection& xconn);
#ifdef SIRENWM_DEBUG_UI
std::unique_ptr<backend::GLPort>       create_gl_port();
#endif

std::unique_ptr<backend::TrayHost>
create_tray_host(XConnection& xconn,
    WindowId owner_bar_window,
    int bar_x, int bar_y, int bar_h,
    uint32_t bg_pixel,
    bool own_selection);

} // namespace backend::x11
