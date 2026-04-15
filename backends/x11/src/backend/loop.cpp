#include <x11_backend.hpp>

#include <domain/core.hpp>
#include <support/log.hpp>
#include <runtime/runtime.hpp>
#include <x11/xconn.hpp>

#include <cstdlib>
#include <vector>

namespace {

bool is_randr_event_type(const Runtime& runtime, uint8_t type) {
    int base = runtime.get_backend_extension_event_base();
    return base >= 0 &&
           (type == (uint8_t)(base + XCB_RANDR_SCREEN_CHANGE_NOTIFY) ||
               type == (uint8_t)(base + XCB_RANDR_NOTIFY));
}

void apply_window_flush(const WindowFlush& flush, X11Window& xw) {
    if (!flush.has_config_changes())
        return;

    uint16_t mask      = 0;
    uint32_t values[7] = {};
    int      n         = 0;

    if (flush.dirty & WindowFlush::X) {
        mask       |= XCB_CONFIG_WINDOW_X;
        values[n++] = static_cast<uint32_t>(xw.pos().x());
    }
    if (flush.dirty & WindowFlush::Y) {
        mask       |= XCB_CONFIG_WINDOW_Y;
        values[n++] = static_cast<uint32_t>(xw.pos().y());
    }
    if (flush.dirty & WindowFlush::Width) {
        mask       |= XCB_CONFIG_WINDOW_WIDTH;
        values[n++] = xw.size().x();
    }
    if (flush.dirty & WindowFlush::Height) {
        mask       |= XCB_CONFIG_WINDOW_HEIGHT;
        values[n++] = xw.size().y();
    }
    if (flush.dirty & WindowFlush::BorderWidth) {
        mask       |= XCB_CONFIG_WINDOW_BORDER_WIDTH;
        values[n++] = xw.border_width;
    }

    if (n > 0)
        xw.configure(mask, values);
}

} // namespace

void X11Backend::request_focus(WindowId win, FocusPriority priority) {
    if (priority >= pending_focus_priority_) {
        pending_focus_win_      = win;
        pending_focus_priority_ = priority;
    }
}

int X11Backend::event_fd() const {
    return xconn.fd();
}

void X11Backend::apply_core_backend_effects() {
    auto effects = core.take_backend_effects();
    for (const auto& e : effects) {
        switch (e.kind) {
            case BackendEffectKind::MapWindow: {
                auto* xw = x11_window(e.window);
                if (xw) {
                    // Borderless (non-self-managed) windows: repin geometry to
                    // physical monitor before mapping so stale coordinates from
                    // the arrange pass that ran before promote cannot leak.
                    if (xw->borderless && !xw->is_self_managed()) {
                        int         ws_id   = core.workspace_of_window(e.window);
                        MonitorId         mon_idx = core.monitor_of_workspace(ws_id);
                        const auto& mons    = core.monitor_states();
                        if (mon_idx >= 0 && mon_idx < (int)mons.size()) {
                            auto [phy_pos, phy_size] = mons[(size_t)mon_idx].physical();
                            (void)core.dispatch(command::atom::SetWindowGeometry{
                                e.window, phy_pos,
                                phy_size });
                            if (auto flush = core.take_window_flush(e.window))
                                apply_window_flush(*flush, *xw);
                        }
                    }
                    xw->set_wm_state_normal();
                    xw->map();
                    xw->send_expose();
                    ewmh_on_window_mapped(event::WindowMapped{ e.window });
                }
                break;
            }
            case BackendEffectKind::UnmapWindow: {
                auto* xw = x11_window(e.window);
                if (xw) {
                    if (e.window == barrier_window_) {
                        xconn.ungrab_pointer();
                        clear_pointer_barriers();
                    }
                    ewmh_on_window_unmapped(event::WindowUnmapped{ e.window, /*withdrawn=*/ false });
                    xw->note_wm_unmap();
                    xw->unmap();
                }
                break;
            }
            case BackendEffectKind::FocusWindow:
                if (e.window != NO_WINDOW)
                    request_focus(e.window, kFocusWorkspace);
                break;
            case BackendEffectKind::FocusRoot:
                // Focus root only if no higher-priority request is pending.
                if (root_window != NO_WINDOW && pending_focus_priority_ == kFocusNone)
                    xconn.focus_window(root_window);
                break;
            case BackendEffectKind::UpdateWindow:
                if (e.window == NO_WINDOW)
                    break;
                if (auto flush = core.take_window_flush(e.window)) {
                    if (auto* xw = x11_window(e.window))
                        apply_window_flush(*flush, *xw);
                }
                break;
            case BackendEffectKind::WarpPointer:
                if (input_port_impl)
                    input_port_impl->warp_pointer_abs(e.pos);
                break;
            case BackendEffectKind::RaiseWindow:
                if (auto* xw = x11_window(e.window))
                    xw->raise();
                break;
            case BackendEffectKind::LowerWindow:
                if (auto* xw = x11_window(e.window))
                    xw->lower();
                break;
            case BackendEffectKind::CloseWindow:
                if (auto* xw = x11_window(e.window)) {
                    if (xw->supports_delete())
                        xw->send_delete_message();
                    else
                        xw->kill();
                    xconn.flush();
                }
                break;
        }
    }
}

void X11Backend::pump_events(std::size_t max_events_per_tick) {
    pending_focus_win_      = NO_WINDOW;
    pending_focus_priority_ = kFocusNone;

    xcb_motion_notify_event_t*       latest_motion = nullptr;
    std::vector<xcb_expose_event_t*> pending_exposes;
    pending_exposes.reserve(32);
    bool                             randr_dirty = false;

    std::size_t                      processed_events = 0;
    xcb_generic_event_t*             ev;
    while (processed_events < max_events_per_tick && (ev = xconn.poll_event())) {
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

    if (randr_dirty)
        runtime.dispatch_display_change();

    if (processed_events >= max_events_per_tick)
        LOG_DEBUG("event-loop: burst capped at %zu events/tick", max_events_per_tick);
}

void X11Backend::render_frame() {
    apply_core_backend_effects();

    // Apply the highest-priority focus request accumulated this tick.
    // Runs after drain_events + apply_core_backend_effects so pointer focus
    // always wins over stale workspace-switch FocusWindow effects from the
    // same tick.
    if (pending_focus_win_ != NO_WINDOW) {
        focus_window(pending_focus_win_);
        core.emit_focus_changed(pending_focus_win_);
        pending_focus_win_      = NO_WINDOW;
        pending_focus_priority_ = kFocusNone;
    }

    auto visible_windows = core.visible_window_ids();
    for (auto win : visible_windows) {
        if (auto flush = core.take_window_flush(win)) {
            if (auto* xw = x11_window(win))
                apply_window_flush(*flush, *xw);
        }
    }

    xconn.flush();
}

void X11Backend::on_reload_applied() {
    key_down.fill(false);
    reload_border_colors();
    // Re-raise bars: borderless/fullscreen windows don't go through MapNotify on reload,
    // so RaiseDocks would never fire without this explicit call.
    runtime.post_event(event::RaiseDocks{});
    // Apply focus immediately (not via arbiter) — reload happens between pump_events
    // and drain_events, so the arbiter won't fire until the next tick.
    if (auto focused = core.focused_window_state(); focused && focused->is_visible()) {
        focus_window(focused->id);
        core.emit_focus_changed(focused->id);
    } else {
        xconn.focus_window(root_window);
        core.emit_focus_changed(NO_WINDOW);
    }
    xconn.flush();
}
