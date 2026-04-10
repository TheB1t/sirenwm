#include <wl_backend.hpp>
#include <wl_compat.hpp>
#include <wl_ports.hpp>
#include <wl_surface.hpp>

#include <core.hpp>
#include <log.hpp>
#include <runtime.hpp>

#include <cassert>
#include <cstdlib>

extern "C" {
#include <wlr/util/log.h>
#ifndef SIRENWM_NO_LAYER_SHELL
#  include <wlr/types/wlr_scene.h>  // wlr_scene_layer_surface_v1_create
#endif
}

#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <string>

// ---------------------------------------------------------------------------
// wlroots log bridge
// ---------------------------------------------------------------------------
static void wlr_log_handler(wlr_log_importance importance, const char* fmt, va_list args) {
    char buf[1024];
    vsnprintf(buf, sizeof(buf), fmt, args);
    switch (importance) {
        case WLR_ERROR: LOG_ERR("[wlr] %s", buf);   break;
        case WLR_INFO:  LOG_INFO("[wlr] %s", buf);  break;
        default:        LOG_DEBUG("[wlr] %s", buf);  break;
    }
}

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------
WaylandBackend::WaylandBackend(Core& core, Runtime& runtime)
    : core_(core)
    , runtime_(runtime)
    , backend_obj_(display_.ev_loop())
    , renderer_(backend_obj_.get(), display_.get())
    , scene_(display_.get()) {
    wlr_log_init(WLR_DEBUG, wlr_log_handler);

    // display_, backend_obj_, renderer_, scene_ are initialised by member ctors above.
    xdg_shell_ = wlr_xdg_shell_create(display_.get(), 3);
#ifndef SIRENWM_NO_LAYER_SHELL
    layer_shell_ = wlr_layer_shell_v1_create(display_.get(), 4);
    LOG_INFO("WaylandBackend: layer-shell enabled");
#else
    LOG_INFO("WaylandBackend: layer-shell disabled (xml not found at build time)");
#endif
    data_dev_mgr_ = wlr_data_device_manager_create(display_.get());

    seat_        = wlr_seat_create(display_.get(), "seat0");
    cursor_      = wlr_cursor_create();
    xcursor_mgr_ = wlr_xcursor_manager_create(nullptr, 24);
    wlr_xcursor_manager_load(xcursor_mgr_, 1.0f);
    // Attach cursor to output layout after xcursor theme is loaded.
    // Under WLR_BACKENDS=x11 (pixman renderer, no DRM) skip the attachment:
    // wlr_output_commit_state would trigger wlr_output_cursor_set_buffer which
    // asserts renderer != NULL.  X11 backend draws the cursor via XFixes, so
    // no wlr_cursor attachment is needed.
    software_renderer_ = renderer_.is_software();
    if (!software_renderer_)
        wlr_cursor_attach_output_layout(cursor_, scene_.output_layout());

    // Wire top-level backend signals
    on_new_output_.connect(&backend_obj_.new_output_signal(),
        [this](wlr_output* o) { handle_new_output(o); });
    on_new_input_.connect(&backend_obj_.new_input_signal(),
        [this](wlr_input_device* d) { handle_new_input(d); });
    on_new_xdg_surface_.connect(&xdg_shell_->events.new_surface,
        [this](wlr_xdg_surface* s) { handle_new_xdg_surface(s); });
#ifndef SIRENWM_NO_LAYER_SHELL
    on_new_layer_surface_.connect(&layer_shell_->events.new_surface,
        [this](wlr_layer_surface_v1* s) { handle_new_layer_surface(s); });
#endif

    // Cursor signals
    on_cursor_motion_.connect(&cursor_->events.motion,
        [this](wlr_pointer_motion_event* ev) { handle_cursor_motion(ev); });
    on_cursor_motion_abs_.connect(&cursor_->events.motion_absolute,
        [this](wlr_pointer_motion_absolute_event* ev) { handle_cursor_motion_abs(ev); });
    on_cursor_button_.connect(&cursor_->events.button,
        [this](wlr_pointer_button_event* ev) { handle_cursor_button(ev); });
    on_cursor_axis_.connect(&cursor_->events.axis,
        [this](wlr_pointer_axis_event* ev) { handle_cursor_axis(ev); });
    on_cursor_frame_.connect(&cursor_->events.frame,
        [this](void*) { handle_cursor_frame(); });

    // Seat signals
    on_request_cursor_.connect(&seat_->events.request_set_cursor,
        [this](wlr_seat_pointer_request_set_cursor_event* ev) { handle_request_cursor(ev); });
    on_request_set_selection_.connect(&seat_->events.request_set_selection,
        [this](wlr_seat_request_set_selection_event* ev) { handle_request_set_selection(ev); });

    // Create port implementations
    monitor_port_impl_  = backend::wl::create_monitor_port(scene_.output_layout(), runtime_);
    render_port_impl_   = backend::wl::create_render_port(scene_.root(), renderer_.renderer(), renderer_.allocator());
    input_port_impl_    = backend::wl::create_input_port(seat_, cursor_, pointer_grabbed_);
    keyboard_port_impl_ = backend::wl::create_keyboard_port(seat_);

    // Start the backend (opens DRM device, creates initial outputs)
    backend_obj_.start();

    // Socket is already initialised by WlDisplay constructor.
    LOG_INFO("WaylandBackend: initialised on %s", display_.socket_name().c_str());
}

WaylandBackend::~WaylandBackend() {
    // Disconnect all listeners before destroying objects.
    on_new_output_.disconnect();
    on_new_input_.disconnect();
    on_new_xdg_surface_.disconnect();
#ifndef SIRENWM_NO_LAYER_SHELL
    on_new_layer_surface_.disconnect();
#endif
    on_cursor_motion_.disconnect();
    on_cursor_motion_abs_.disconnect();
    on_cursor_button_.disconnect();
    on_cursor_axis_.disconnect();
    on_cursor_frame_.disconnect();
    on_request_cursor_.disconnect();
    on_request_set_selection_.disconnect();

    outputs_.clear();
    keyboards_.clear();
    pending_.clear();
    surfaces_.clear();

    if (xcursor_mgr_)   wlr_xcursor_manager_destroy(xcursor_mgr_);
    if (cursor_)        wlr_cursor_destroy(cursor_);
    // seat, compositor, xdg_shell are destroyed via wl_display_destroy (in WlDisplay dtor)
    // scene_, renderer_, backend_obj_, display_ dtors run after this in reverse declaration order
}

// ---------------------------------------------------------------------------
// on_start
// ---------------------------------------------------------------------------
void WaylandBackend::on_start(Core& core) {
    set_cursor("left_ptr");

    // Notify monitors discovered during wlr_backend_start
    runtime_.dispatch_display_change();
    LOG_INFO("WaylandBackend: on_start done, %zu outputs", outputs_.size());
}

void WaylandBackend::shutdown() {
    // Nothing to do — cleanup happens in the destructor.
    // Backends that override this (e.g. DRM) may need to stop the frame loop here.
}

void WaylandBackend::prepare_exec_restart() {
    display_.prepare_exec_restart();
}

void WaylandBackend::on_reload_applied() {
    // Re-raise bars and re-focus the active window.
    runtime_.emit(event::RaiseDocks{});
    if (auto focused = core_.focused_window_state(); focused && focused->is_visible()) {
        runtime_.emit(event::FocusChanged{ focused->id });
    }
}

// ---------------------------------------------------------------------------
// WindowId allocation + surface lookup
// ---------------------------------------------------------------------------
WindowId WaylandBackend::alloc_window_id() {
    return next_id_.fetch_add(1, std::memory_order_relaxed);
}

WlSurface* WaylandBackend::wl_surface(WindowId id) {
    auto it = surfaces_.find(id);
    return it != surfaces_.end() ? it->second : nullptr;
}

// ---------------------------------------------------------------------------
// Port accessors
// ---------------------------------------------------------------------------
backend::MonitorPort*  WaylandBackend::monitor_port()  { return monitor_port_impl_.get();  }
backend::RenderPort*   WaylandBackend::render_port()   { return render_port_impl_.get();   }
backend::InputPort*    WaylandBackend::input_port()    { return input_port_impl_.get();    }
backend::KeyboardPort* WaylandBackend::keyboard_port() { return keyboard_port_impl_.get(); }

// ---------------------------------------------------------------------------
// create_window — Core factory callback: called from EnsureWindow dispatch.
// Transfers the pre-created WlSurface from pending_ into surfaces_.
// ---------------------------------------------------------------------------
std::shared_ptr<swm::Window> WaylandBackend::create_window(WindowId id) {
    auto pit = pending_.find(id);
    if (pit != pending_.end()) {
        // Move the staged surface into the active map.
        auto surf_ptr = std::move(pit->second);
        pending_.erase(pit);
        surfaces_[id] = surf_ptr.get();
        return surf_ptr;   // Core takes ownership via shared_ptr
    }

    // Fallback: plain Window for non-Wayland-native windows (e.g. XWayland).
    auto w = std::make_shared<swm::Window>();
    w->id = id;
    return w;
}

// ---------------------------------------------------------------------------
// Window title / PID
// ---------------------------------------------------------------------------
std::string WaylandBackend::window_title(WindowId id) const {
    auto it = surfaces_.find(id);
    if (it == surfaces_.end())
        return {};
    const auto* surf = it->second;
    if (!surf || !surf->toplevel() || !surf->toplevel()->title)
        return {};
    return surf->toplevel()->title;
}

uint32_t WaylandBackend::window_pid(WindowId id) const {
    auto it = surfaces_.find(id);
    if (it == surfaces_.end())
        return 0;
    const auto* surf = it->second;
    if (!surf || !surf->xdg_surface() || !surf->xdg_surface()->surface)
        return 0;
    wl_client* client = wl_resource_get_client(surf->xdg_surface()->resource);
    if (!client)
        return 0;
    pid_t pid = 0;
    wl_client_get_credentials(client, &pid, nullptr, nullptr);
    return (uint32_t)pid;
}

bool WaylandBackend::close_window(WindowId id) {
    auto it = surfaces_.find(id);
    if (it == surfaces_.end())
        return false;
    auto* surf = it->second;
    if (!surf || !surf->toplevel())
        return false;
    wlr_xdg_toplevel_send_close(surf->toplevel());
    return true;
}

// ---------------------------------------------------------------------------
// handle_new_output
// ---------------------------------------------------------------------------
void WaylandBackend::handle_new_output(wlr_output* output) {
    LOG_INFO("WaylandBackend: new output '%s'", output->name);

    // Bind the renderer and allocator to this output before first commit.
    renderer_.init_output(output);

    // Configure the output with its preferred mode (wlroots 0.18 API).
    wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, true);
    if (!wl_list_empty(&output->modes)) {
        wlr_output_mode* mode = wlr_output_preferred_mode(output);
        wlr_output_state_set_mode(&state, mode);
    }
    if (!wlr_output_commit_state(output, &state)) {
        LOG_ERR("WaylandBackend: failed to commit initial mode for '%s'", output->name);
        wlr_output_state_finish(&state);
        return;
    }
    wlr_output_state_finish(&state);

    auto* out_state = new WlOutput();
    out_state->output = output;

    // Add to output_layout (auto-placed to the right of existing outputs)
    wlr_output_layout_add_auto(scene_.output_layout(), output);

    // Create scene output
    out_state->scene_output = wlr_scene_get_scene_output(scene_.scene(), output);
    if (!out_state->scene_output)
        out_state->scene_output = wlr_scene_output_create(scene_.scene(), output);

    // Wire frame + destroy signals
    out_state->on_frame_.connect(&output->events.frame, [this, out_state](void*) {
            handle_output_frame(out_state);
        });
    out_state->on_destroy_.connect(&output->events.destroy, [this, out_state](void*) {
            handle_output_destroy(out_state);
        });

    outputs_.push_back(std::unique_ptr<WlOutput>(out_state));

    // Notify runtime that display topology changed.
    runtime_.dispatch_display_change();
}

// ---------------------------------------------------------------------------
// handle_output_destroy
// ---------------------------------------------------------------------------
void WaylandBackend::handle_output_destroy(WlOutput* out) {
    LOG_INFO("WaylandBackend: output destroyed '%s'",
        out->output ? out->output->name : "?");
    out->on_frame_.disconnect();
    out->on_destroy_.disconnect();
    outputs_.erase(std::remove_if(outputs_.begin(), outputs_.end(),
        [out](const auto& p) {
            return p.get() == out;
        }), outputs_.end());
    runtime_.dispatch_display_change();
}

// ---------------------------------------------------------------------------
// handle_new_input
// ---------------------------------------------------------------------------
void WaylandBackend::handle_new_input(wlr_input_device* device) {
    switch (device->type) {
        case WLR_INPUT_DEVICE_KEYBOARD: handle_new_keyboard(device); break;
        case WLR_INPUT_DEVICE_POINTER:  handle_new_pointer(device);  break;
        default: break;
    }

    // Update seat capabilities
    uint32_t caps = 0;
    for (const auto& kb : keyboards_) (void)kb, caps |= WL_SEAT_CAPABILITY_KEYBOARD;
    for (const auto& out : outputs_)  (void)out, caps |= WL_SEAT_CAPABILITY_POINTER;
    if (!keyboards_.empty()) caps |= WL_SEAT_CAPABILITY_KEYBOARD;
    wlr_seat_set_capabilities(seat_, caps);
}

void WaylandBackend::handle_new_keyboard(wlr_input_device* device) {
    auto* kb = new WlKeyboard();
    kb->device = device;

    wlr_keyboard* keyboard = wlr_keyboard_from_input_device(device);

    // Set up XKB keymap from environment (XKBLAYOUT, XKBOPTIONS, etc.)
    xkb_context*   ctx    = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    xkb_rule_names rules  = {};
    xkb_keymap*    keymap = xkb_keymap_new_from_names(ctx, &rules, XKB_KEYMAP_COMPILE_NO_FLAGS);
    wlr_keyboard_set_keymap(keyboard, keymap);
    xkb_keymap_unref(keymap);
    xkb_context_unref(ctx);

    wlr_keyboard_set_repeat_info(keyboard, 25, 600);

    kb->on_key_.connect(&keyboard->events.key,
        [this, kb](wlr_keyboard_key_event* ev) { handle_keyboard_key(kb, ev); });
    kb->on_modifiers_.connect(&keyboard->events.modifiers,
        [this, kb](void*) { handle_keyboard_modifiers(kb); });
    kb->on_destroy_.connect(&device->events.destroy,
        [this, kb](void*) { handle_keyboard_destroy(kb); });

    wlr_seat_set_keyboard(seat_, keyboard);

    keyboards_.push_back(std::unique_ptr<WlKeyboard>(kb));
    LOG_INFO("WaylandBackend: keyboard '%s' added", device->name);
}

void WaylandBackend::handle_new_pointer(wlr_input_device* device) {
    wlr_cursor_attach_input_device(cursor_, device);
    LOG_INFO("WaylandBackend: pointer '%s' added", device->name);
}

void WaylandBackend::handle_keyboard_key(WlKeyboard* kb, wlr_keyboard_key_event* ev) {
    wlr_keyboard* keyboard = wlr_keyboard_from_input_device(kb->device);
    xkb_keycode_t keycode  = ev->keycode + 8;
    xkb_keysym_t  keysym   = xkb_state_key_get_one_sym(keyboard->xkb_state, keycode);

    bool          pressed = (ev->state == WL_KEYBOARD_KEY_STATE_PRESSED);
    if (!pressed)
        return;

    event::KeyPressEv kev;
    kev.mods    = (uint16_t)(keyboard->modifiers.depressed | keyboard->modifiers.locked);
    kev.keycode = (uint8_t)(ev->keycode & 0xFF);
    kev.keysym  = (uint32_t)keysym;

    runtime_.emit(kev);

    // Pass through to focused client
    wlr_seat_set_keyboard(seat_, keyboard);
    wlr_seat_keyboard_notify_key(seat_, ev->time_msec, ev->keycode, ev->state);
}

void WaylandBackend::handle_keyboard_modifiers(WlKeyboard* kb) {
    wlr_keyboard* keyboard = wlr_keyboard_from_input_device(kb->device);
    wlr_seat_set_keyboard(seat_, keyboard);
    wlr_seat_keyboard_notify_modifiers(seat_, &keyboard->modifiers);
}

void WaylandBackend::handle_keyboard_destroy(WlKeyboard* kb) {
    keyboards_.erase(std::remove_if(keyboards_.begin(), keyboards_.end(),
        [kb](const auto& p) {
            return p.get() == kb;
        }), keyboards_.end());
}

// ---------------------------------------------------------------------------
// Cursor handlers
// ---------------------------------------------------------------------------
void WaylandBackend::handle_cursor_motion(wlr_pointer_motion_event* ev) {
    wlr_cursor_move(cursor_, &ev->pointer->base, ev->delta_x, ev->delta_y);
    process_cursor_motion(ev->time_msec);
}

void WaylandBackend::handle_cursor_motion_abs(wlr_pointer_motion_absolute_event* ev) {
    wlr_cursor_warp_absolute(cursor_, &ev->pointer->base, ev->x, ev->y);
    process_cursor_motion(ev->time_msec);
}

void WaylandBackend::handle_cursor_button(wlr_pointer_button_event* ev) {
    event::ButtonEv bev;
    bev.button   = (uint8_t)(ev->button & 0xFF);
    bev.state    = (uint16_t)mod_state_;
    bev.release  = (ev->state == WLR_BUTTON_RELEASED);
    bev.root_pos = { (int16_t)cursor_->x, (int16_t)cursor_->y };
    runtime_.emit(bev);

    wlr_seat_pointer_notify_button(seat_, ev->time_msec, ev->button, ev->state);
}

void WaylandBackend::handle_cursor_axis(wlr_pointer_axis_event* ev) {
    wlr_seat_pointer_notify_axis(seat_, ev->time_msec, ev->orientation,
        ev->delta, ev->delta_discrete, ev->source, ev->relative_direction);
}

void WaylandBackend::handle_cursor_frame() {
    wlr_seat_pointer_notify_frame(seat_);
}

void WaylandBackend::handle_request_cursor(
    wlr_seat_pointer_request_set_cursor_event* ev) {
    wlr_cursor_set_surface(cursor_, ev->surface, ev->hotspot_x, ev->hotspot_y);
}

void WaylandBackend::handle_request_set_selection(
    wlr_seat_request_set_selection_event* ev) {
    wlr_seat_set_selection(seat_, ev->source, ev->serial);
}

void WaylandBackend::process_cursor_motion(uint32_t time_ms) {
    // Find surface under cursor; notify seat
    double             sx = 0, sy = 0;
    wlr_scene_node*    node    = wlr_scene_node_at(&scene_.root()->node, cursor_->x, cursor_->y, &sx, &sy);
    wlr_scene_surface* ss      = nullptr;
    wlr_surface*       surface = nullptr;

    if (node && node->type == WLR_SCENE_NODE_BUFFER) {
        ss = wlr_scene_surface_try_from_buffer(wlr_scene_buffer_from_node(node));
        if (ss)
            surface = ss->surface;
    }

    if (pointer_grabbed_) {
        // Pointer is grabbed (drag/resize): keep cursor shape but don't
        // forward focus or motion to clients.
        return;
    }

    if (!surface) {
        set_cursor("left_ptr");
        wlr_seat_pointer_clear_focus(seat_);
    } else {
        wlr_seat_pointer_notify_enter(seat_, surface, sx, sy);
        wlr_seat_pointer_notify_motion(seat_, time_ms, sx, sy);

        // Focus-follows-mouse: find the managed window under the cursor and
        // dispatch a FocusWindow command to Core (mirrors X11 EnterNotify).
        // wlr_xdg_surface_try_from_wlr_surface returns nullptr for non-xdg surfaces.
        if (wlr_xdg_surface* xdg = wlr_xdg_surface_try_from_wlr_surface(surface)) {
            for (auto& [id, wls] : surfaces_) {
                if (wls && wls->xdg_surface() == xdg) {
                    core_.dispatch(command::FocusWindow{ id });
                    break;
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Domain event handlers
// ---------------------------------------------------------------------------
void WaylandBackend::on(event::WorkspaceSwitched ev) {
    runtime_.emit(ev);
}

void WaylandBackend::on(event::FocusChanged ev) {
    // Deactivate previously focused window.
    if (focused_window_ != NO_WINDOW && focused_window_ != ev.window) {
        auto prev = surfaces_.find(focused_window_);
        if (prev != surfaces_.end() && prev->second && prev->second->xdg_surface())
            wlr_xdg_toplevel_set_activated(prev->second->xdg_surface()->toplevel, false);
    }

    if (ev.window == NO_WINDOW) {
        wlr_seat_keyboard_notify_clear_focus(seat_);
        focused_window_ = NO_WINDOW;
        return;
    }

    auto it = surfaces_.find(ev.window);
    if (it == surfaces_.end())
        return;
    WlSurface* surf = it->second;
    if (!surf || !surf->xdg_surface())
        return;

    wlr_xdg_toplevel_set_activated(surf->xdg_surface()->toplevel, true);
    focused_window_ = ev.window;

    // Notify seat that keyboard focus goes to this surface.
    if (!keyboards_.empty()) {
        wlr_keyboard* keyboard = wlr_keyboard_from_input_device(keyboards_[0]->device);
        wlr_seat_set_keyboard(seat_, keyboard);
        wlr_seat_keyboard_notify_enter(seat_,
            surf->xdg_surface()->surface,
            keyboard->keycodes,
            keyboard->num_keycodes,
            &keyboard->modifiers);
    }
}

void WaylandBackend::on(event::WindowAssignedToWorkspace ev) {
    (void)ev;
}

void WaylandBackend::on(event::WindowAdopted ev) {
    runtime_.emit(event::WindowMapped{ ev.window });
    if (!ev.currently_visible)
        runtime_.emit(event::WindowUnmapped{ ev.window, false });
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------
void WaylandBackend::set_cursor(const char* name) {
    // wlr_cursor_set_xcursor uploads cursor textures to each output via the
    // output's software cursor path.  When running nested under X11
    // (WLR_BACKENDS=x11) wlroots uses a pixman (software) renderer with no
    // DRM device, and wlr_output_cursor_set_buffer asserts that a renderer is
    // present for buffer upload — which fails.
    //
    // Detect the software-renderer case by checking wlr_renderer_get_drm_fd.
    // Returns -1 when there is no backing DRM device (pixman/software path).
    // In that case skip the xcursor call: the host X11 window provides its
    // own cursor decoration.
    if (renderer_.is_software()) return;
    wlr_cursor_set_xcursor(cursor_, xcursor_mgr_, name);
}

WlOutput* WaylandBackend::output_at(double x, double y) {
    wlr_output* o = wlr_output_layout_output_at(scene_.output_layout(), x, y);
    if (!o) return nullptr;
    for (auto& out : outputs_)
        if (out->output == o) return out.get();
    return nullptr;
}

// ---------------------------------------------------------------------------
// apply_core_backend_effects — mirrors X11Backend pattern
// ---------------------------------------------------------------------------
void WaylandBackend::apply_core_backend_effects() {
    auto effects = core_.take_backend_effects();
    for (const auto& e : effects) {
        switch (e.kind) {
            case BackendEffectKind::MapWindow: {
                auto it = surfaces_.find(e.window);
                if (it != surfaces_.end()) {
                    it->second->mapped = true;
                    if (it->second->scene_node())
                        wlr_scene_node_set_enabled(&it->second->scene_node()->node, true);
                    runtime_.emit(event::WindowMapped{ e.window });
                }
                break;
            }
            case BackendEffectKind::UnmapWindow: {
                auto it = surfaces_.find(e.window);
                if (it != surfaces_.end()) {
                    it->second->mapped = false;
                    if (it->second->scene_node())
                        wlr_scene_node_set_enabled(&it->second->scene_node()->node, false);
                    runtime_.emit(event::WindowUnmapped{ e.window, false });
                }
                break;
            }
            case BackendEffectKind::FocusWindow:
                runtime_.emit(event::FocusChanged{ e.window });
                break;
            case BackendEffectKind::FocusRoot:
                wlr_seat_keyboard_notify_clear_focus(seat_);
                break;
            case BackendEffectKind::UpdateWindow: {
                if (e.window == NO_WINDOW)
                    break;
                auto* ws = wl_surface(e.window);
                if (!ws)
                    break;
                if (auto flush = core_.take_window_flush(e.window)) {
                    auto state = core_.window_state_any(e.window);
                    if (state)
                        ws->set_geometry(state->x(), state->y(), state->width(), state->height());
                }
                break;
            }
            case BackendEffectKind::RaiseWindow: {
                auto* ws = wl_surface(e.window);
                if (ws && ws->scene_node())
                    wlr_scene_node_raise_to_top(&ws->scene_node()->node);
                break;
            }
            case BackendEffectKind::LowerWindow: {
                auto* ws = wl_surface(e.window);
                if (ws && ws->scene_node())
                    wlr_scene_node_lower_to_bottom(&ws->scene_node()->node);
                break;
            }
            case BackendEffectKind::WarpPointer:
                wlr_cursor_warp_absolute(cursor_, nullptr,
                    (double)e.pos.x() / 1.0, (double)e.pos.y() / 1.0);
                break;
        }
    }
}

#ifndef SIRENWM_NO_LAYER_SHELL
// ---------------------------------------------------------------------------
// Layer shell — external clients (waybar, swaybar, mako, etc.)
// ---------------------------------------------------------------------------

// Recalculate usable area for an output and send configure to all mapped
// layer surfaces on that output.
void WaylandBackend::arrange_layers(wlr_output* output) {
    struct wlr_box usable {};
    wlr_output_effective_resolution(output, &usable.width, &usable.height);

    // Process layers from bottom to top; background/bottom/top/overlay.
    // Only BOTTOM and TOP layers typically set exclusive zones.
    static const zwlr_layer_shell_v1_layer layer_order[] = {
        ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND,
        ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM,
        ZWLR_LAYER_SHELL_V1_LAYER_TOP,
        ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
    };

    for (auto layer : layer_order) {
        for (auto& ls : layer_surfaces_) {
            if (!ls->surface || !ls->surface->output || ls->surface->output != output)
                continue;
            if (ls->surface->current.layer != layer)
                continue;
            wlr_scene_layer_surface_v1_configure(ls->scene_layer, nullptr, &usable);
        }
    }
}

void WaylandBackend::handle_new_layer_surface(wlr_layer_surface_v1* surface) {
    LOG_INFO("WaylandBackend: new layer-shell surface layer=%u namespace='%s'",
        (unsigned)surface->current.layer,
        surface->namespace_ ? surface->namespace_ : "");

    // Assign output: use the client's requested output or fall back to first.
    if (!surface->output) {
        if (!outputs_.empty())
            surface->output = outputs_.front()->output;
        else {
            LOG_WARN("WaylandBackend: layer surface with no output available, closing");
            wlr_layer_surface_v1_destroy(surface);
            return;
        }
    }

    auto ls = std::make_unique<WlLayerSurface>();
    ls->surface = surface;

    // Place in scene graph at the appropriate layer.
    ls->scene_layer = wlr_scene_layer_surface_v1_create(scene_.root(), surface);

    WlLayerSurface* raw = ls.get();
    layer_surfaces_.push_back(std::move(ls));

    raw->on_map_.connect(&surface->surface->events.map, [this, raw](void*) {
            LOG_INFO("WaylandBackend: layer surface mapped");
            arrange_layers(raw->surface->output);
            wlr_scene_node_set_enabled(&raw->scene_layer->tree->node, true);
        });

    raw->on_unmap_.connect(&surface->surface->events.unmap, [this, raw](void*) {
            LOG_INFO("WaylandBackend: layer surface unmapped");
            wlr_scene_node_set_enabled(&raw->scene_layer->tree->node, false);
            arrange_layers(raw->surface->output);
        });

    raw->on_commit_.connect(&surface->surface->events.commit, [this, raw](void*) {
            // Re-arrange if the exclusive zone or anchor changed.
            if (raw->surface->output)
                arrange_layers(raw->surface->output);
        });

    raw->on_destroy_.connect(&surface->events.destroy, [this, raw](void*) {
            handle_layer_surface_destroy(raw);
        });

    // Send initial configure so the client knows its dimensions.
    arrange_layers(surface->output);
}

void WaylandBackend::handle_layer_surface_destroy(WlLayerSurface* ls) {
    LOG_INFO("WaylandBackend: layer surface destroyed");
    wlr_output* output = ls->surface ? ls->surface->output : nullptr;

    layer_surfaces_.erase(
        std::remove_if(layer_surfaces_.begin(), layer_surfaces_.end(),
        [ls](const auto& p) {
            return p.get() == ls;
        }),
        layer_surfaces_.end());

    if (output) arrange_layers(output);
}
#endif
