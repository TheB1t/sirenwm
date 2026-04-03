#include <core.hpp>
#include <log.hpp>

std::optional<WindowFlush> Core::take_window_flush(WindowId win) {
    auto it = pending_window_flushes.find(win);
    if (it == pending_window_flushes.end())
        return std::nullopt;

    WindowFlush out = it->second;
    pending_window_flushes.erase(it);

    if (!out.event_mask_dirty && !out.has_config_changes())
        return std::nullopt;

    return out;
}

void Core::emit_backend_effect(BackendEffectKind kind, WindowId window) {
    pending_backend_effects.push_back(BackendEffect{ kind, window });
}

WindowFlush& Core::ensure_window_flush(WindowId win) {
    auto& flush = pending_window_flushes[win];
    flush.window = win;
    return flush;
}

void Core::mark_window_event_mask(WindowId win) {
    auto& flush = ensure_window_flush(win);
    flush.event_mask_dirty = true;
    emit_backend_effect(BackendEffectKind::UpdateWindow, win);
}

void Core::mark_window_x(WindowId win) {
    auto& flush = ensure_window_flush(win);
    flush.x_dirty = true;
    emit_backend_effect(BackendEffectKind::UpdateWindow, win);
}

void Core::mark_window_y(WindowId win) {
    auto& flush = ensure_window_flush(win);
    flush.y_dirty = true;
    emit_backend_effect(BackendEffectKind::UpdateWindow, win);
}

void Core::mark_window_width(WindowId win) {
    auto& flush = ensure_window_flush(win);
    flush.width_dirty = true;
    emit_backend_effect(BackendEffectKind::UpdateWindow, win);
}

void Core::mark_window_height(WindowId win) {
    auto& flush = ensure_window_flush(win);
    flush.height_dirty = true;
    emit_backend_effect(BackendEffectKind::UpdateWindow, win);
}

void Core::mark_window_border_width(WindowId win) {
    auto& flush = ensure_window_flush(win);
    flush.border_width_dirty = true;
    emit_backend_effect(BackendEffectKind::UpdateWindow, win);
}

void Core::mark_window_sibling(WindowId win) {
    auto& flush = ensure_window_flush(win);
    flush.sibling_dirty = true;
    emit_backend_effect(BackendEffectKind::UpdateWindow, win);
}

void Core::mark_window_stack_mode(WindowId win) {
    auto& flush = ensure_window_flush(win);
    flush.stack_mode_dirty = true;
    emit_backend_effect(BackendEffectKind::UpdateWindow, win);
}

void Core::sync_workspace_visibility() {
    auto n = (int)wsman.all().size();
    if (n <= 0)
        return;

    std::vector<bool> visible((size_t)n, false);
    for (auto& mon : wsman.get_monitors()) {
        if (mon.active_ws >= 0 && mon.active_ws < n)
            visible[(size_t)mon.active_ws] = true;
    }

    for (int i = 0; i < n; i++) {
        auto& ws = wsman.workspace(i);
        bool  v  = visible[(size_t)i];
        for (auto& w : ws.windows) {
            if (!w) continue;
            if (!v) {
                if (w->visible) {
                    w->hidden_by_workspace = true;
                    w->visible             = false;
                    w->ignore_unmap_count += 2; // X sends two UnmapNotify per xcb_unmap_window
                    emit_backend_effect(BackendEffectKind::UnmapWindow, w->id);
                }
                continue;
            }

            if (w->hidden_by_workspace) {
                w->hidden_by_workspace = false;
                w->visible             = true;
                emit_backend_effect(BackendEffectKind::MapWindow, w->id);
            }
        }
    }
}

void Core::sync_current_focus() {
    auto f = wsman.current().advance_focus();
    if (f && f->visible) {
        wsman.focus_window(f->id);
        emit_backend_effect(BackendEffectKind::FocusWindow, f->id);
        emit_focus_changed(f->id);
    } else {
        emit_backend_effect(BackendEffectKind::FocusRoot);
        emit_focus_changed(NO_WINDOW);
    }
}

void Core::emit_focus_changed(WindowId window) {
    pending_core_events.push_back(event::FocusChanged{ window });
}

void Core::emit_workspace_switched(int workspace_id) {
    pending_core_events.push_back(event::WorkspaceSwitched{ workspace_id });
}

void Core::emit_raise_docks() {
    pending_core_events.push_back(event::RaiseDocks{});
}

void Core::emit_display_topology_changed() {
    pending_core_events.push_back(event::DisplayTopologyChanged{});
}

void Core::emit_window_assigned_to_workspace(WindowId window, int workspace_id) {
    pending_core_events.push_back(event::WindowAssignedToWorkspace{ window, workspace_id });
}

void Core::init(std::vector<Monitor> initial_monitors) {
    if (!initial_monitors.empty()) {
        wsman.set_monitors(std::move(initial_monitors));
    } else {
        LOG_WARN("Core::init(): no monitors provided — workspaces will have no monitor assignments");
    }
    wsman.init_from_defs(settings.workspace_defs,
        settings.monitor_aliases,
        settings.monitor_compose);
}

void Core::arrange() {
    if (!layouts.count(active_layout)) return;
    for (auto& mon : wsman.get_monitors()) {
        if (mon.active_ws < 0) continue;
        auto& ws = wsman.workspace(mon.active_ws);
        std::vector<WindowId> tiled;
        for (auto& w : ws.windows)
            if (w && w->visible && !w->floating) tiled.push_back(w->id);

        layout::PlacementSink place = [this](WindowId win, int32_t x, int32_t y,
            uint32_t width, uint32_t height,
            uint32_t border_width) {
                (void)dispatch(command::SetWindowGeometry{ win, x, y, width, height });
                (void)dispatch(command::SetWindowBorderWidth{ win, border_width });
                emit_backend_effect(BackendEffectKind::UpdateWindow, win);
            };

        layout::Config cfg_from_theme = layout_cfg;
        cfg_from_theme.border = settings.theme.border_thickness;
        cfg_from_theme.gap    = settings.theme.gap;
        layouts[active_layout](tiled, mon, cfg_from_theme, place);

        // Apply theme border to floating windows — layout skips them, so their
        // border_width would otherwise stay at 0 (no border drawn by X).
        // Fullscreen windows keep border_width=0 (set in fullscreen dispatch path).
        for (auto& w : ws.windows) {
            if (!w || !w->visible || !w->floating || w->fullscreen) continue;
            if (w->border_width == settings.theme.border_thickness) continue;
            (void)dispatch(command::SetWindowBorderWidth{ w->id,
                                                          (uint32_t)settings.theme.border_thickness });
            emit_backend_effect(BackendEffectKind::UpdateWindow, w->id);
        }
    }
}

bool Core::dispatch(const Command& cmd) {
    return std::visit([this](const auto& c) {
                   return this->dispatch(c);
               }, cmd);
}

bool Core::dispatch(const command::FocusWindow& cmd) {
    return wsman.focus_window(cmd.window);
}

bool Core::dispatch(const command::SwitchWorkspace& cmd) {
    bool switched = wsman.switch_to(cmd.workspace_id, settings.monitor_aliases,
            cmd.monitor_index.has_value() ? *cmd.monitor_index : -1,
            settings.monitor_compose);
    if (!switched)
        return false;
    sync_workspace_visibility();
    arrange();
    sync_current_focus();
    emit_workspace_switched(cmd.workspace_id);
    return true;
}

bool Core::dispatch(const command::MoveWindowToWorkspace& cmd) {
    if (cmd.workspace_id < 0 || cmd.workspace_id >= workspace_count())
        return false;

    auto w = wsman.find_window_in_all(cmd.window);
    if (!w)
        return false;

    // Borderless fullscreen windows must not be moved to a workspace on a different
    // monitor — the game's D3D context is bound to the original monitor.
    // Only block if the window actually covers its monitor (true fullscreen),
    // not just any borderless window (dialogs, remote-viewer, etc.).
    if (w->wm_no_decorations) {
        int src_mon = monitor_of_workspace(workspace_of_window(cmd.window));
        int dst_mon = monitor_of_workspace(cmd.workspace_id);
        if (src_mon != dst_mon) {
            const auto& mons = monitor_states();
            if (src_mon >= 0 && src_mon < (int)mons.size()) {
                const auto& mon = mons[(size_t)src_mon];
                if ((int)w->width >= mon.width && (int)w->height >= mon.height)
                    return false;
            }
        }
    }

    wsman.move_window_to(cmd.workspace_id, w);
    emit_window_assigned_to_workspace(cmd.window, cmd.workspace_id);
    sync_workspace_visibility();
    arrange();

    bool moved_ws_visible = is_workspace_visible(cmd.workspace_id);
    bool focus_moved      = moved_ws_visible && w->visible && !w->suppress_focus_once;
    if (focus_moved) {
        wsman.focus_window(w->id);
        emit_backend_effect(BackendEffectKind::FocusWindow, w->id);
        emit_focus_changed(w->id);
    } else {
        bool follow_hidden = settings.follow_moved_window;
        if (follow_hidden && !w->suppress_focus_once) {
            (void)dispatch(command::SwitchWorkspace{ cmd.workspace_id, std::nullopt });
            if (w->visible) {
                wsman.focus_window(w->id);
                emit_backend_effect(BackendEffectKind::FocusWindow, w->id);
                emit_focus_changed(w->id);
            }
        } else {
            sync_current_focus();
        }
    }
    return true;
}

bool Core::dispatch(const command::MoveFocusedWindowToWorkspace& cmd) {
    auto w = wsman.current().focused();
    if (!w)
        return false;
    return dispatch(command::MoveWindowToWorkspace{ w->id, cmd.workspace_id });
}

bool Core::dispatch(const command::MapWindow& cmd) {
    auto w = wsman.find_window_in_all(cmd.window);
    if (!w)
        return false;
    if (!w->visible) {
        w->visible = true;
        emit_backend_effect(BackendEffectKind::MapWindow, w->id);
    }
    return true;
}

bool Core::dispatch(const command::UnmapWindow& cmd) {
    auto w = wsman.find_window_in_all(cmd.window);
    if (!w)
        return false;
    if (w->visible) {
        w->visible             = false;
        w->ignore_unmap_count += 2;
        emit_backend_effect(BackendEffectKind::UnmapWindow, w->id);
    }
    return true;
}

bool Core::dispatch(const command::SetWindowFullscreen& cmd) {
    auto w = wsman.find_window_in_all(cmd.window);
    if (!w)
        return false;

    int            ws_id = wsman.workspace_of_window(cmd.window);

    const Monitor* mon   = nullptr;
    for (auto& m : wsman.get_monitors()) {
        if (m.active_ws == ws_id) {
            mon = &m;
            break;
        }
    }

    if (!mon) {
        const auto& mons = wsman.get_monitors();
        int         f    = wsman.get_focused_monitor();
        if (f >= 0 && f < (int)mons.size())
            mon = &mons[(size_t)f];
        else if (!mons.empty())
            mon = &mons.front();
    }

    if (cmd.enabled) {
        if (!w->fullscreen) {
            w->floating_before_fullscreen = w->floating;
            w->border_before_fullscreen   = w->border_width;
        }
        w->fullscreen   = true;
        w->floating     = true;
        w->border_width = 0;
        mark_window_border_width(cmd.window);
        if (mon) {
            int top_inset = std::max(0, monitor_top_inset_applied);
            w->x      = std::max(0, mon->x);
            w->y      = std::max(0, mon->y - top_inset);
            w->width  = (uint32_t)std::max(1, mon->width);
            w->height = (uint32_t)std::max(1, mon->height + top_inset);
            mark_window_x(cmd.window);
            mark_window_y(cmd.window);
            mark_window_width(cmd.window);
            mark_window_height(cmd.window);
        }
        arrange();
        emit_raise_docks();
        if (ws_id >= 0 && is_workspace_visible(ws_id) &&
            w->visible && !w->hidden_by_workspace) {
            wsman.focus_window(cmd.window);
            emit_backend_effect(BackendEffectKind::FocusWindow, cmd.window);
            emit_focus_changed(cmd.window);
        }
        return true;
    }

    if (!w->fullscreen)
        return true;

    w->fullscreen   = false;
    w->floating     = w->floating_before_fullscreen;
    w->border_width = w->border_before_fullscreen;
    mark_window_border_width(cmd.window);
    arrange();
    emit_raise_docks();
    return true;
}

bool Core::dispatch(const command::EnsureWindow& cmd) {
    if (cmd.window == NO_WINDOW)
        return false;

    auto w = wsman.find_window_in_all(cmd.window);
    if (!w) {
        wsman.create_window(cmd.window, cmd.workspace_id);
        return true;
    }

    if (cmd.workspace_id >= 0 && cmd.workspace_id < workspace_count())
        wsman.move_window_to(cmd.workspace_id, w);
    return true;
}

bool Core::dispatch(const command::AssignWindowWorkspace& cmd) {
    auto w = wsman.find_window_in_all(cmd.window);
    if (!w)
        return false;
    wsman.move_window_to(cmd.workspace_id, w);
    emit_window_assigned_to_workspace(cmd.window, cmd.workspace_id);
    return true;
}

bool Core::dispatch(const command::SetWindowMetadata& cmd) {
    auto w = wsman.find_window_in_all(cmd.window);
    if (!w)
        return false;
    w->wm_instance       = cmd.wm_instance;
    w->wm_class          = cmd.wm_class;
    w->wm_type_dialog    = cmd.wm_type_dialog;
    w->wm_type_utility   = cmd.wm_type_utility;
    w->wm_type_splash    = cmd.wm_type_splash;
    w->wm_type_modal     = cmd.wm_type_modal;
    w->wm_fixed_size     = cmd.wm_fixed_size;
    w->wm_never_focus    = cmd.wm_never_focus;
    w->wm_no_decorations = cmd.wm_no_decorations;
    return true;
}

bool Core::dispatch(const command::SetWindowEventMask& cmd) {
    auto w = wsman.find_window_in_all(cmd.window);
    if (!w)
        return false;
    w->event_mask = cmd.mask;
    mark_window_event_mask(cmd.window);
    return true;
}

bool Core::dispatch(const command::SetWindowVisible& cmd) {
    auto w = wsman.find_window_in_all(cmd.window);
    if (!w)
        return false;
    w->visible = cmd.visible;
    return true;
}

bool Core::dispatch(const command::SetWindowHiddenByWorkspace& cmd) {
    auto w = wsman.find_window_in_all(cmd.window);
    if (!w)
        return false;
    w->hidden_by_workspace = cmd.hidden;
    return true;
}

bool Core::dispatch(const command::SetWindowSuppressFocusOnce& cmd) {
    auto w = wsman.find_window_in_all(cmd.window);
    if (!w)
        return false;
    w->suppress_focus_once = cmd.suppress;
    return true;
}

bool Core::dispatch(const command::SetWindowFloating& cmd) {
    auto w = wsman.find_window_in_all(cmd.window);
    if (!w)
        return false;
    w->floating = cmd.floating;
    return true;
}

bool Core::dispatch(const command::ToggleWindowFloating& cmd) {
    auto w = wsman.find_window_in_all(cmd.window);
    if (!w)
        return false;
    w->floating = !w->floating;
    return true;
}

bool Core::dispatch(const command::FocusNextWindow&) {
    auto w = wsman.focus_next();
    if (!w || !w->visible) {
        emit_focus_changed(NO_WINDOW);
        return true;
    }
    emit_backend_effect(BackendEffectKind::FocusWindow, w->id);
    emit_focus_changed(w->id);
    return true;
}

bool Core::dispatch(const command::FocusPrevWindow&) {
    auto w = wsman.focus_prev();
    if (!w || !w->visible) {
        emit_focus_changed(NO_WINDOW);
        return true;
    }
    emit_backend_effect(BackendEffectKind::FocusWindow, w->id);
    emit_focus_changed(w->id);
    return true;
}

bool Core::dispatch(const command::ToggleFocusedWindowFloating&) {
    auto w = wsman.current().focused();
    if (!w)
        return false;
    (void)dispatch(command::ToggleWindowFloating{ w->id });
    arrange();
    return true;
}

bool Core::dispatch(const command::SwitchWorkspaceLocalIndex& cmd) {
    int mon = wsman.get_focused_monitor();
    if (!wsman.switch_local_index(mon, cmd.local_index))
        return false;
    int ws_id = wsman.active_workspace(mon);
    sync_workspace_visibility();
    arrange();
    sync_current_focus();
    if (ws_id >= 0)
        emit_workspace_switched(ws_id);
    return true;
}

bool Core::dispatch(const command::HideWindow& cmd) {
    auto w = wsman.find_window_in_all(cmd.window);
    if (!w)
        return false;
    w->hidden_by_workspace = true;
    if (w->visible) {
        w->visible             = false;
        w->ignore_unmap_count += 2;
        emit_backend_effect(BackendEffectKind::UnmapWindow, w->id);
    }
    return true;
}

bool Core::dispatch(const command::ApplyMonitorTopology& cmd) {
    wsman.set_monitors(cmd.monitors);
    monitor_top_inset_applied    = 0;
    monitor_bottom_inset_applied = 0;
    wsman.assign_workspaces(settings.monitor_aliases,
        settings.monitor_compose);
    sync_workspace_visibility();
    emit_display_topology_changed();
    arrange();
    sync_current_focus();
    return true;
}

bool Core::dispatch(const command::ApplyMonitorTopInset& cmd) {
    int delta = cmd.inset_px - monitor_top_inset_applied;
    if (delta == 0)
        return true;

    for (auto& mon : wsman.get_monitors()) {
        mon.y     += delta;
        mon.height = std::max(0, mon.height - delta);
    }
    monitor_top_inset_applied = cmd.inset_px;
    return true;
}

bool Core::dispatch(const command::ApplyMonitorBottomInset& cmd) {
    int delta = cmd.inset_px - monitor_bottom_inset_applied;
    if (delta == 0)
        return true;

    for (auto& mon : wsman.get_monitors())
        mon.height = std::max(0, mon.height - delta);
    monitor_bottom_inset_applied = cmd.inset_px;
    return true;
}

bool Core::dispatch(const command::SetLayout& cmd) {
    if (!layouts.count(cmd.name)) {
        if (runtime_started)
            LOG_ERR("set_layout: unknown layout '%s'", cmd.name.c_str());
        else {
            // Deferred: layout module not loaded yet; store name and arrange later.
            active_layout = cmd.name;
            LOG_DEBUG("set_layout: deferred '%s' (layout not registered yet)", cmd.name.c_str());
        }
        return false;
    }
    active_layout = cmd.name;
    arrange();
    return true;
}

bool Core::dispatch(const command::SetMasterFactor& cmd) {
    layout_cfg.master_factor = std::clamp(cmd.value, 0.1f, 0.9f);
    arrange();
    return true;
}

bool Core::dispatch(const command::AdjustMasterFactor& cmd) {
    layout_cfg.master_factor = std::clamp(layout_cfg.master_factor + cmd.delta, 0.1f, 0.9f);
    arrange();
    return true;
}

bool Core::dispatch(const command::IncMaster& cmd) {
    layout_cfg.nmaster = std::max(1, layout_cfg.nmaster + cmd.delta);
    arrange();
    return true;
}

bool Core::dispatch(const command::Zoom&) {
    if (!wsman.zoom_focused())
        return false;
    arrange();
    return true;
}

bool Core::dispatch(const command::ReconcileNow&) {
    arrange();
    return true;
}

bool Core::dispatch(const command::RemoveWindowFromAllWorkspaces& cmd) {
    wsman.remove_window_from_all(cmd.window);
    pending_window_flushes.erase(cmd.window);
    return true;
}

bool Core::dispatch(const command::SetWindowGeometry& cmd) {
    auto w = wsman.find_window_in_all(cmd.window);
    if (!w)
        return false;
    w->x      = cmd.x;
    w->y      = cmd.y;
    w->width  = cmd.width;
    w->height = cmd.height;
    mark_window_x(cmd.window);
    mark_window_y(cmd.window);
    mark_window_width(cmd.window);
    mark_window_height(cmd.window);
    return true;
}

bool Core::dispatch(const command::SetWindowPosition& cmd) {
    auto w = wsman.find_window_in_all(cmd.window);
    if (!w)
        return false;
    w->x = cmd.x;
    w->y = cmd.y;
    mark_window_x(cmd.window);
    mark_window_y(cmd.window);
    return true;
}

bool Core::dispatch(const command::SetWindowSize& cmd) {
    auto w = wsman.find_window_in_all(cmd.window);
    if (!w)
        return false;
    w->width  = cmd.width;
    w->height = cmd.height;
    mark_window_width(cmd.window);
    mark_window_height(cmd.window);
    return true;
}

bool Core::dispatch(const command::SetWindowBorderWidth& cmd) {
    auto w = wsman.find_window_in_all(cmd.window);
    if (!w)
        return false;
    w->border_width = cmd.border_width;
    mark_window_border_width(cmd.window);
    return true;
}

bool Core::dispatch(const command::SetWindowSibling& cmd) {
    auto w = wsman.find_window_in_all(cmd.window);
    if (!w)
        return false;
    w->sibling = cmd.sibling;
    mark_window_sibling(cmd.window);
    return true;
}

bool Core::dispatch(const command::SetWindowStackMode& cmd) {
    auto w = wsman.find_window_in_all(cmd.window);
    if (!w)
        return false;
    w->stack_mode = cmd.stack_mode;
    mark_window_stack_mode(cmd.window);
    return true;
}

bool Core::dispatch(const command::SyncWindowFromConfigureNotify& cmd) {
    auto w = wsman.find_window_in_all(cmd.window);
    if (!w)
        return false;

    auto               it      = pending_window_flushes.find(cmd.window);
    const WindowFlush* pending = (it == pending_window_flushes.end()) ? nullptr : &it->second;
    if (!pending || !pending->x_dirty)            w->x = cmd.x;
    if (!pending || !pending->y_dirty)            w->y = cmd.y;
    if (!pending || !pending->width_dirty)        w->width = cmd.width;
    if (!pending || !pending->height_dirty)       w->height = cmd.height;
    if (!pending || !pending->border_width_dirty) w->border_width = cmd.border_width;
    return true;
}

CoreReloadState Core::snapshot_reload_state() const {
    return CoreReloadState{
        .settings      = settings,
        .keybindings   = keybindings,
        .layout_cfg    = layout_cfg,
        .active_layout = active_layout,
    };
}

void Core::restore_reload_state(const CoreReloadState& snapshot) {
    settings      = snapshot.settings;
    keybindings   = snapshot.keybindings;
    layout_cfg    = snapshot.layout_cfg;
    active_layout = snapshot.active_layout;
}

std::vector<WindowId> Core::all_window_ids() const {
    std::vector<WindowId> out;
    for (const auto& ws : workspace_states()) {
        for (const auto& w : ws.windows) {
            if (w)
                out.push_back(w->id);
        }
    }
    return out;
}

std::vector<WindowId> Core::visible_window_ids() const {
    std::vector<WindowId> out;
    for (const auto& ws : workspace_states()) {
        for (const auto& w : ws.windows) {
            if (!w || !w->visible)
                continue;
            out.push_back(w->id);
        }
    }
    return out;
}

bool Core::consume_wm_unmap(WindowId win) {
    auto w = wsman.find_window_in_all(win);
    if (!w || w->ignore_unmap_count <= 0)
        return false;
    w->ignore_unmap_count--;
    return true;
}

bool Core::consume_window_suppress_focus_once(WindowId win) {
    auto w = wsman.find_window_in_all(win);
    if (!w)
        return false;
    bool out = w->suppress_focus_once;
    w->suppress_focus_once = false;
    return out;
}

void Core::update_window(WindowId win) {
    auto w = wsman.find_window_in_all(win);
    if (!w)
        return;
    emit_backend_effect(BackendEffectKind::UpdateWindow, w->id);
}