#pragma once

// ---------------------------------------------------------------------------
// Port factory declarations for the Wayland backend.
// All returned objects are owned by the caller (unique_ptr).
// ---------------------------------------------------------------------------

#include <backend/monitor_port.hpp>
#include <backend/render_port.hpp>
#include <backend/input_port.hpp>
#include <backend/keyboard_port.hpp>

#include <memory>

extern "C" {
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/render/allocator.h>
}

class Runtime;

namespace backend::wl {

std::unique_ptr<MonitorPort>   create_monitor_port(wlr_output_layout* layout, Runtime& rt);
std::unique_ptr<RenderPort>    create_render_port(wlr_scene_tree* root, wlr_renderer* renderer,
                                                   wlr_allocator* allocator);
std::unique_ptr<InputPort>     create_input_port(wlr_seat* seat, wlr_cursor* cursor, bool& pointer_grabbed);
std::unique_ptr<KeyboardPort>  create_keyboard_port(wlr_seat* seat);

} // namespace backend::wl
