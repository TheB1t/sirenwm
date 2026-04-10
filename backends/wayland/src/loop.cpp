#include <wl_backend.hpp>
#include <log.hpp>

#include <core.hpp>

extern "C" {
#include <wlr/types/wlr_output.h>
}

// ---------------------------------------------------------------------------
// event_fd — used by Runtime's epoll loop to know when to call pump_events()
// ---------------------------------------------------------------------------
int WaylandBackend::event_fd() const {
    return display_.event_fd();
}

// ---------------------------------------------------------------------------
// pump_events — drain the Wayland event loop (non-blocking, timeout=0)
// ---------------------------------------------------------------------------
void WaylandBackend::pump_events(std::size_t /*max_events_per_tick*/) {
    display_.dispatch_events();
    display_.flush_clients();

    apply_core_backend_effects();
}

// ---------------------------------------------------------------------------
// render_frame — render all dirty outputs via the scene graph
// ---------------------------------------------------------------------------
void WaylandBackend::render_frame() {
    apply_core_backend_effects();

    // Update managed window geometry from Core flush queue
    auto visible = core_.visible_window_ids();
    for (auto win : visible) {
        if (auto flush = core_.take_window_flush(win)) {
            auto* ws = wl_surface(win);
            if (!ws) continue;
            auto  state = core_.window_state_any(win);
            if (state)
                ws->set_geometry(state->x(), state->y(), state->width(), state->height());
        }
    }
}

// ---------------------------------------------------------------------------
// handle_output_frame — called by wlr_output::frame signal each vsync
// ---------------------------------------------------------------------------
void WaylandBackend::handle_output_frame(WlOutput* out) {
    if (!out->scene_output) return;

    wlr_scene_output_commit(out->scene_output, nullptr);

    // Send frame-done to surfaces visible on this output
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    wlr_scene_output_send_frame_done(out->scene_output, &now);
}

// ---------------------------------------------------------------------------
// StartupSnapshot — Wayland compositors have no pre-existing window list.
//                   Returns empty snapshot (no exec-restart support yet).
// ---------------------------------------------------------------------------
StartupSnapshot WaylandBackend::scan_existing_windows() {
    return {};
}
