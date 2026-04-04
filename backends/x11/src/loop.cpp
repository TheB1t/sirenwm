#include <x11_backend.hpp>

#include <core.hpp>
#include <log.hpp>
#include <runtime.hpp>
#include <xconn.hpp>

#include <cstdlib>
#include <vector>

namespace {

bool is_randr_event_type(const Runtime& runtime, uint8_t type) {
    int base = runtime.get_backend_extension_event_base();
    return base >= 0 &&
           (type == (uint8_t)(base + XCB_RANDR_SCREEN_CHANGE_NOTIFY) ||
               type == (uint8_t)(base + XCB_RANDR_NOTIFY));
}

void apply_window_flush(XConnection& xconn, const WindowFlush& flush, const WindowState& state) {
    if (!flush.has_config_changes())
        return;

    uint16_t mask      = 0;
    uint32_t values[7] = {};
    int      n         = 0;

    if (flush.x_dirty) {
        mask       |= XCB_CONFIG_WINDOW_X;
        values[n++] = static_cast<uint32_t>(state.x);
    }
    if (flush.y_dirty) {
        mask       |= XCB_CONFIG_WINDOW_Y;
        values[n++] = static_cast<uint32_t>(state.y);
    }
    if (flush.width_dirty) {
        mask       |= XCB_CONFIG_WINDOW_WIDTH;
        values[n++] = state.width;
    }
    if (flush.height_dirty) {
        mask       |= XCB_CONFIG_WINDOW_HEIGHT;
        values[n++] = state.height;
    }
    if (flush.border_width_dirty) {
        mask       |= XCB_CONFIG_WINDOW_BORDER_WIDTH;
        values[n++] = state.border_width;
    }

    if (n > 0)
        xconn.configure_window(flush.window, mask, values);
}

} // namespace

int X11Backend::event_fd() const {
    return xconn.fd();
}

void X11Backend::apply_core_backend_effects() {
    auto effects = core.take_backend_effects();
    for (const auto& e : effects) {
        switch (e.kind) {
            case BackendEffectKind::MapWindow:
                if (e.window != NO_WINDOW)
                    xconn.map_window(e.window);
                break;
            case BackendEffectKind::UnmapWindow:
                if (e.window != NO_WINDOW) {
                    note_wm_unmap(e.window);
                    xconn.unmap_window(e.window);
                }
                break;
            case BackendEffectKind::FocusWindow:
                if (e.window != NO_WINDOW)
                    focus_window(e.window);
                break;
            case BackendEffectKind::FocusRoot:
                if (root_window != NO_WINDOW)
                    xconn.focus_window(root_window);
                break;
            case BackendEffectKind::UpdateWindow:
                if (e.window == NO_WINDOW)
                    break;
                if (auto flush = core.take_window_flush(e.window)) {
                    auto window = core.window_state_any(e.window);
                    if (window)
                        apply_window_flush(xconn, *flush, *window);
                }
                break;
            case BackendEffectKind::WarpPointer:
                if (input_port_impl)
                    input_port_impl->warp_pointer_abs(e.x, e.y);
                break;

        }
    }
}

void X11Backend::pump_events(std::size_t max_events_per_tick) {
    xcb_motion_notify_event_t*       latest_motion = nullptr;
    std::vector<xcb_expose_event_t*> pending_exposes;
    pending_exposes.reserve(32);
    bool                 randr_dirty      = false;

    std::size_t          processed_events = 0;
    xcb_generic_event_t* ev;
    while (processed_events < max_events_per_tick && (ev = xconn.poll())) {
        processed_events++;
        uint8_t type = ev->response_type & ~0x80;

        if (is_randr_event_type(runtime, type)) {
            randr_dirty = true;
            free(ev);
            continue;
        }

        if (type == XCB_MOTION_NOTIFY) {
            if (latest_motion)
                free(latest_motion);
            latest_motion = (xcb_motion_notify_event_t*)ev;
            continue;
        }

        if (type == XCB_EXPOSE) {
            auto* expose   = (xcb_expose_event_t*)ev;
            bool  replaced = false;
            for (auto*& pending : pending_exposes) {
                if (pending && pending->window == expose->window) {
                    free(pending);
                    pending  = expose;
                    replaced = true;
                    break;
                }
            }
            if (!replaced)
                pending_exposes.push_back(expose);
            continue;
        }

        handle_generic_event(ev);
        free(ev);
    }

    if (latest_motion) {
        handle_generic_event((xcb_generic_event_t*)latest_motion);
        free(latest_motion);
        latest_motion = nullptr;
    }

    for (auto* expose : pending_exposes) {
        if (!expose)
            continue;
        handle_generic_event((xcb_generic_event_t*)expose);
        free(expose);
    }
    pending_exposes.clear();

    apply_core_backend_effects();

    if (randr_dirty)
        runtime.dispatch_display_change();

    if (processed_events >= max_events_per_tick)
        LOG_DEBUG("event-loop: burst capped at %zu events/tick", max_events_per_tick);
}

void X11Backend::render_frame() {
    apply_core_backend_effects();

    auto visible_windows = core.visible_window_ids();
    for (auto win : visible_windows) {
        if (auto flush = core.take_window_flush(win)) {
            auto window = core.window_state_any(win);
            if (window)
                apply_window_flush(xconn, *flush, *window);
        }
    }

    xconn.flush();
}

void X11Backend::on_reload_applied() {
    key_down.fill(false);
    reload_border_colors();
}