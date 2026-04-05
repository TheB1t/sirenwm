#include <core.hpp>
#include <log.hpp>

std::optional<WindowFlush> Core::take_window_flush(WindowId win) {
    auto it = pending_window_flushes.find(win);
    if (it == pending_window_flushes.end())
        return std::nullopt;

    WindowFlush out = it->second;
    pending_window_flushes.erase(it);

    if (!out.has_config_changes())
        return std::nullopt;

    return out;
}

void Core::emit_backend_effect(BackendEffectKind kind, WindowId window) {
    pending_backend_effects.push_back(BackendEffect{ kind, window });
}

void Core::emit_warp_pointer(int16_t x, int16_t y) {
    BackendEffect e;
    e.kind = BackendEffectKind::WarpPointer;
    e.x    = x;
    e.y    = y;
    pending_backend_effects.push_back(e);
}

WindowFlush& Core::ensure_window_flush(WindowId win) {
    auto& flush = pending_window_flushes[win];
    flush.window = win;
    return flush;
}


void Core::mark_window_dirty(WindowId win, uint8_t bits) {
    auto& flush = ensure_window_flush(win);
    flush.dirty |= bits;
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
                bool emit_unmap = !w->hidden_by_workspace && !w->hidden_explicitly && w->mapped;
                w->hidden_by_workspace = true;
                if (emit_unmap)
                    emit_backend_effect(BackendEffectKind::UnmapWindow, w->id);
                continue;
            }

            if (w->hidden_by_workspace) {
                w->hidden_by_workspace = false;
                if (!w->hidden_explicitly) {
                    w->mapped = true;
                    emit_backend_effect(BackendEffectKind::MapWindow, w->id);
                }
            }
        }
    }
}

void Core::sync_current_focus() {
    auto f = wsman.current().advance_focus();
    if (f && f->is_visible()) {
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

void Core::emit_borderless_activated(WindowId window, int monitor_index) {
    pending_core_events.push_back(event::BorderlessActivated{ window, monitor_index });
}

void Core::emit_borderless_deactivated() {
    pending_core_events.push_back(event::BorderlessDeactivated{});
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

void Core::reconcile() {
    sync_workspace_visibility();
    arrange();
    sync_current_focus();
}

void Core::arrange() {
    if (!layouts.count(active_layout)) return;
    for (auto& mon : wsman.get_monitors()) {
        if (mon.active_ws < 0) continue;
        auto& ws = wsman.workspace(mon.active_ws);
        std::vector<WindowId> tiled;
        for (auto& w : ws.windows)
            if (w && w->is_visible() && !w->floating && !w->borderless && !w->is_self_managed())
                tiled.push_back(w->id);

        layout::PlacementSink place = [this](WindowId win, int32_t x, int32_t y,
            uint32_t width, uint32_t height,
            uint32_t border_width) {
                // Fixed-size windows (min==max hints): honour their own size,
                // only let the layout set the position.
                auto w = wsman.find_window_in_all(win);
                if (w && w->wm_fixed_size && w->width > 0 && w->height > 0) {
                    width  = w->width;
                    height = w->height;
                }
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
        // Fullscreen and borderless windows keep border_width=0.
        for (auto& w : ws.windows) {
            if (!w || !w->is_visible() || !w->floating || w->fullscreen || w->borderless) continue;
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
    reconcile();
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
    if (w->preserve_position) {
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

    int src_mon_idx = monitor_of_workspace(workspace_of_window(cmd.window));
    int dst_mon_idx = monitor_of_workspace(cmd.workspace_id);
    wsman.move_window_to(cmd.workspace_id, w);
    emit_window_assigned_to_workspace(cmd.window, cmd.workspace_id);

    // Fullscreen window moved to a different monitor: pin geometry to the
    // destination monitor (same logic as SetWindowFullscreen).
    if (w->fullscreen && dst_mon_idx != src_mon_idx) {
        const auto& mons = monitor_states();
        if (dst_mon_idx >= 0 && dst_mon_idx < (int)mons.size()) {
            const auto& dm           = mons[(size_t)dst_mon_idx];
            int         top_inset    = std::max(0, monitor_top_inset_applied);
            int         bottom_inset = std::max(0, monitor_bottom_inset_applied);
            w->x      = std::max(0, dm.x);
            w->y      = std::max(0, dm.y - top_inset);
            w->width  = (uint32_t)std::max(1, dm.width);
            w->height = (uint32_t)std::max(1, dm.height + top_inset + bottom_inset);
            mark_window_dirty(cmd.window, WindowFlush::Geometry);
        }
    }

    sync_workspace_visibility();
    arrange();

    bool moved_ws_visible = is_workspace_visible(cmd.workspace_id);
    bool focus_moved      = moved_ws_visible && w->is_visible() && !w->suppress_focus_once;
    if (focus_moved) {
        wsman.focus_window(w->id);
        emit_backend_effect(BackendEffectKind::FocusWindow, w->id);
        emit_focus_changed(w->id);
    } else {
        bool follow_hidden = settings.follow_moved_window;
        if (follow_hidden && !w->suppress_focus_once) {
            (void)dispatch(command::SwitchWorkspace{ cmd.workspace_id, std::nullopt });
            if (w->is_visible()) {
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
    if (!w->mapped) {
        w->mapped = true;
        if (w->is_visible())
            emit_backend_effect(BackendEffectKind::MapWindow, w->id);
    }
    return true;
}

bool Core::dispatch(const command::UnmapWindow& cmd) {
    auto w = wsman.find_window_in_all(cmd.window);
    if (!w)
        return false;
    if (w->mapped) {
        w->mapped = false;
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
        const auto& mons  = wsman.get_monitors();
        // Prefer the monitor that owns the workspace (may be inactive).
        int         owner = wsman.monitor_of_workspace(ws_id);
        if (owner >= 0 && owner < (int)mons.size())
            mon = &mons[(size_t)owner];
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
        mark_window_dirty(cmd.window, WindowFlush::BorderWidth);
        if (!cmd.preserve_geometry && mon) {
            int top_inset    = std::max(0, monitor_top_inset_applied);
            int bottom_inset = std::max(0, monitor_bottom_inset_applied);
            w->x      = std::max(0, mon->x);
            w->y      = std::max(0, mon->y - top_inset);
            w->width  = (uint32_t)std::max(1, mon->width);
            w->height = (uint32_t)std::max(1, mon->height + top_inset + bottom_inset);
            mark_window_dirty(cmd.window, WindowFlush::Geometry);
        }
        arrange();
        emit_raise_docks();
        if (ws_id >= 0 && is_workspace_visible(ws_id) && w->is_visible()) {
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
    mark_window_dirty(cmd.window, WindowFlush::BorderWidth);
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
    w->type              = cmd.type;

    // Classify from geometry hints supplied by the backend.
    // Self-managed: client had _NET_WM_STATE_FULLSCREEN before MapRequest and
    //   covers the monitor (not XEMBED) — client controls its own geometry.
    // WM-borderless: WM pins geometry to monitor (MOTIF no-decos or fixed-size covers monitor).
    const auto& h = cmd.hints;
    bool self_managed      = h.pre_fullscreen && !h.is_xembed && h.covers_monitor;
    bool wm_borderless     = !self_managed && h.covers_monitor && (h.no_decorations || h.fixed_size);
    bool will_be_borderless = self_managed || wm_borderless;

    w->wm_fixed_size     = h.fixed_size;
    w->wm_never_focus    = h.never_focus;
    w->preserve_position = h.static_gravity;

    w->self_managed          = self_managed;
    w->promote_to_borderless = will_be_borderless;

    // Transient routing: assign to parent's workspace and float.
    if (cmd.transient_for != NO_WINDOW) {
        int parent_ws = wsman.workspace_of_window(cmd.transient_for);
        if (parent_ws >= 0) {
            dispatch(command::SetWindowSuppressFocusOnce{ cmd.window, true });
            dispatch(command::AssignWindowWorkspace{ cmd.window, parent_ws });
        }
        if (!w->floating)
            dispatch(command::SetWindowFloating{ cmd.window, true });
    }

    // Dialog/utility/splash windows float by default.
    if (!w->floating && (w->is_dialog() || cmd.transient_for != NO_WINDOW))
        dispatch(command::SetWindowFloating{ cmd.window, true });

    // Fixed-size non-borderless windows float.
    if (!w->floating && !w->borderless && w->wm_fixed_size && !will_be_borderless)
        dispatch(command::SetWindowFloating{ cmd.window, true });

    return true;
}


bool Core::dispatch(const command::SetWindowMapped& cmd) {
    auto w = wsman.find_window_in_all(cmd.window);
    if (!w)
        return false;
    w->mapped = cmd.mapped;
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

bool Core::dispatch(const command::SetWindowBorderless& cmd) {
    auto w = wsman.find_window_in_all(cmd.window);
    if (!w)
        return false;
    w->borderless          = cmd.borderless;
    w->promote_to_borderless = false; // consumed
    if (cmd.borderless && w->border_width != 0) {
        w->border_width = 0;
        emit_backend_effect(BackendEffectKind::UpdateWindow, w->id);
    }
    if (cmd.borderless) {
        int ws_id   = wsman.workspace_of_window(cmd.window);
        int mon_idx = wsman.monitor_of_workspace(ws_id);
        if (mon_idx >= 0)
            emit_borderless_activated(cmd.window, mon_idx);
    } else {
        // Check if any borderless window remains on any monitor.
        const auto& mons = wsman.all_monitor_states();
        bool any = false;
        for (int i = 0; i < (int)mons.size() && !any; ++i)
            any = monitor_has_visible_borderless(i);
        if (!any)
            emit_borderless_deactivated();
    }
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
    if (!w || !w->is_visible()) {
        emit_focus_changed(NO_WINDOW);
        return true;
    }
    emit_backend_effect(BackendEffectKind::FocusWindow, w->id);
    emit_focus_changed(w->id);
    return true;
}

bool Core::dispatch(const command::FocusPrevWindow&) {
    auto w = wsman.focus_prev();
    if (!w || !w->is_visible()) {
        emit_focus_changed(NO_WINDOW);
        return true;
    }
    emit_backend_effect(BackendEffectKind::FocusWindow, w->id);
    emit_focus_changed(w->id);
    return true;
}

bool Core::dispatch(const command::FocusMonitor& cmd) {
    int  n   = cmd.monitor_index;
    auto mon = monitor_state(n);
    if (!mon || mon->active_ws < 0)
        return false;
    bool ok = dispatch(command::SwitchWorkspace{ mon->active_ws, n });
    if (ok)
        emit_warp_pointer(
            (int16_t)(mon->x + mon->width  / 2),
            (int16_t)(mon->y + mon->height / 2));
    return ok;
}

bool Core::dispatch(const command::MoveWindowToMonitor& cmd) {
    int  n   = cmd.monitor_index;
    auto mon = monitor_state(n);
    if (!mon || mon->active_ws < 0)
        return false;
    WindowId win = cmd.window;
    if (win == NO_WINDOW) {
        auto w = wsman.current().focused();
        if (!w) return false;
        win = w->id;
    }
    return dispatch(command::MoveWindowToWorkspace{ win, mon->active_ws });
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
    reconcile();
    if (ws_id >= 0)
        emit_workspace_switched(ws_id);
    return true;
}

bool Core::dispatch(const command::HideWindow& cmd) {
    auto w = wsman.find_window_in_all(cmd.window);
    if (!w)
        return false;
    bool was_visible = w->is_visible();
    w->hidden_explicitly = true;
    if (was_visible)
        emit_backend_effect(BackendEffectKind::UnmapWindow, w->id);
    return true;
}

bool Core::dispatch(const command::ApplyMonitorTopology& cmd) {
    wsman.set_monitors(cmd.monitors);
    monitor_top_inset_applied    = 0;
    monitor_bottom_inset_applied = 0;
    wsman.assign_workspaces(settings.monitor_aliases,
        settings.monitor_compose);
    emit_display_topology_changed();
    reconcile();
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
    mark_window_dirty(cmd.window, WindowFlush::Geometry);
    return true;
}

bool Core::dispatch(const command::SetWindowPosition& cmd) {
    auto w = wsman.find_window_in_all(cmd.window);
    if (!w)
        return false;
    w->x = cmd.x;
    w->y = cmd.y;
    mark_window_dirty(cmd.window, WindowFlush::Position);
    return true;
}

bool Core::dispatch(const command::SetWindowSize& cmd) {
    auto w = wsman.find_window_in_all(cmd.window);
    if (!w)
        return false;
    w->width  = cmd.width;
    w->height = cmd.height;
    mark_window_dirty(cmd.window, WindowFlush::Size);
    return true;
}

bool Core::dispatch(const command::SetWindowBorderWidth& cmd) {
    auto w = wsman.find_window_in_all(cmd.window);
    if (!w)
        return false;
    w->border_width = cmd.border_width;
    mark_window_dirty(cmd.window, WindowFlush::BorderWidth);
    return true;
}


bool Core::dispatch(const command::SyncWindowFromConfigureNotify& cmd) {
    auto w = wsman.find_window_in_all(cmd.window);
    if (!w)
        return false;

    auto               it      = pending_window_flushes.find(cmd.window);
    const WindowFlush* pending = (it == pending_window_flushes.end()) ? nullptr : &it->second;
    if (!pending || !(pending->dirty & WindowFlush::X))           w->x = cmd.x;
    if (!pending || !(pending->dirty & WindowFlush::Y))           w->y = cmd.y;
    if (!pending || !(pending->dirty & WindowFlush::Width))       w->width = cmd.width;
    if (!pending || !(pending->dirty & WindowFlush::Height))      w->height = cmd.height;
    if (!pending || !(pending->dirty & WindowFlush::BorderWidth)) w->border_width = cmd.border_width;
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
            if (!w || !w->is_visible())
                continue;
            out.push_back(w->id);
        }
    }
    return out;
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