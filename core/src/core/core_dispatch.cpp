#include <core.hpp>
#include <layout.hpp>
#include <log.hpp>
#include <lua_helpers.hpp>

// Apply WM_NORMAL_HINTS size constraints to a requested size.
// Clamps to [min, max] then snaps to the nearest increment grid.
static Vec2i apply_size_hints(const WindowState& w, Vec2i req) {
    // Clamp to min/max.
    if (w.size_min.x() > 0) req.x() = std::max(req.x(), w.size_min.x());
    if (w.size_min.y() > 0) req.y() = std::max(req.y(), w.size_min.y());
    if (w.size_max.x() > 0) req.x() = std::min(req.x(), w.size_max.x());
    if (w.size_max.y() > 0) req.y() = std::min(req.y(), w.size_max.y());

    // Snap to increment grid (base + N*inc).
    if (w.size_inc.x() > 1) {
        int base = (w.size_base.x() > 0) ? w.size_base.x()
                 : (w.size_min.x() > 0)  ? w.size_min.x() : 0;
        int n = (req.x() - base) / w.size_inc.x();
        req.x() = base + n * w.size_inc.x();
        if (w.size_min.x() > 0) req.x() = std::max(req.x(), w.size_min.x());
    }
    if (w.size_inc.y() > 1) {
        int base = (w.size_base.y() > 0) ? w.size_base.y()
                 : (w.size_min.y() > 0)  ? w.size_min.y() : 0;
        int n = (req.y() - base) / w.size_inc.y();
        req.y() = base + n * w.size_inc.y();
        if (w.size_min.y() > 0) req.y() = std::max(req.y(), w.size_min.y());
    }
    return req;
}

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

void Core::emit_warp_pointer(Vec2i16 pos) {
    BackendEffect e;
    e.kind = BackendEffectKind::WarpPointer;
    e.pos  = pos;
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
    auto n = (int)wsman.all_workspace_states().size();
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
                bool was_visible = !w->hidden_by_workspace && !w->hidden_explicitly && w->mapped;
                w->hidden_by_workspace = true;
                if (was_visible)
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
    if (window != NO_WINDOW) {
        int ws_id = wsman.workspace_of_window(window);
        if (ws_id >= 0)
            evaluate_workspace_fullscreen(ws_id);
    }
}

void Core::pin_fullscreen_to_monitor(swm::Window& w, int ws_id) {
    int         mon_idx = wsman.monitor_of_workspace(ws_id);
    if (mon_idx < 0) return;
    const auto& mons = wsman.all_monitor_states();
    if (mon_idx >= (int)mons.size()) return;
    const auto& mon = mons[(size_t)mon_idx];
    auto [phy_pos, phy_size] = mon.physical();
    // Self-managed clients control their own geometry — only pin non-self-managed.
    if (!w.is_self_managed()) {
        w.pos()  = phy_pos;
        w.size() = phy_size;
        mark_window_dirty(w.id, WindowFlush::Geometry);
    }
}

void Core::evaluate_workspace_fullscreen(int ws_id) {
    if (ws_id < 0 || ws_id >= workspace_count()) return;
    auto& ws = wsman.workspace(ws_id);

    bool  has_fs = ws.has_fullscreen_window();

    if (has_fs && ws.mode != WorkspaceMode::Fullscreen) {
        LOG_DEBUG("ws[%d]: entering Fullscreen mode", ws_id);
        ws.mode = WorkspaceMode::Fullscreen;
        emit_raise_docks();
    } else if (!has_fs && ws.mode == WorkspaceMode::Fullscreen) {
        LOG_DEBUG("ws[%d]: leaving Fullscreen mode", ws_id);
        ws.mode = WorkspaceMode::Normal;
        emit_raise_docks();
    }

    if (ws.mode != WorkspaceMode::Fullscreen) return;

    auto focused = ws.focused();
    if (!focused || !focused->is_visible()) return;

    bool focused_is_fs = focused->fullscreen || focused->borderless;
    LOG_DEBUG("ws[%d]: evaluate stacking, focused=%d is_fs=%d",
        ws_id, focused->id, (int)focused_is_fs);

    if (focused_is_fs) {
        // Focused window is fullscreen/borderless — pin and raise it.
        pin_fullscreen_to_monitor(*focused, ws_id);
        emit_backend_effect(BackendEffectKind::RaiseWindow, focused->id);
    } else {
        // Focused window is normal — raise it above fullscreen windows.
        emit_backend_effect(BackendEffectKind::RaiseWindow, focused->id);
        // Lower all fullscreen/borderless windows so focused is visible.
        for (auto& w : ws.windows) {
            if (w && w->is_visible() && (w->fullscreen || w->borderless) && w->id != focused->id)
                emit_backend_effect(BackendEffectKind::LowerWindow, w->id);
        }
    }
}

bool Core::focus_monitor_at_point(int x, int y) {
    bool changed = wsman.focus_monitor_at_point(x, y);
    if (!changed)
        return false;

    // Clear X focus on the old monitor: send focus to root.
    emit_backend_effect(BackendEffectKind::FocusRoot);
    emit_focus_changed(NO_WINDOW);

    // Restore the last focused window on the new monitor, if any.
    int      mon = wsman.get_focused_monitor();
    int      ws  = wsman.active_workspace(mon);
    WindowId win = wsman.last_focused_window(mon, ws);
    if (win != NO_WINDOW && wsman.find_window_in_all(win)) {
        wsman.focus_window(win);
        emit_backend_effect(BackendEffectKind::FocusWindow, win);
        emit_focus_changed(win);
    }
    return true;
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
    register_layout("tile",      layout::tile);
    register_layout("monocle",   layout::monocle);
    // "unmanaged": no tiling — windows keep their own geometry (pure floating behaviour).
    register_layout("unmanaged", [](const std::vector<WindowId>&, const Monitor&,
        const layout::Config&, const layout::PlacementSink&) {
        });

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
    bool is_cpp_layout = layouts.count(active_layout) > 0;
    bool is_lua_layout = !is_cpp_layout && lua_layouts.count(active_layout) > 0;
    if (!is_cpp_layout && !is_lua_layout) return;

    for (auto& mon : wsman.get_monitors()) {
        if (mon.active_ws < 0) continue;
        auto&                 ws = wsman.workspace(mon.active_ws);
        std::vector<WindowId> tiled;
        for (auto& w : ws.windows)
            if (w && w->is_visible() && !w->floating && !w->fullscreen && !w->borderless && !w->is_self_managed())
                tiled.push_back(w->id);

        layout::PlacementSink place = [this](WindowId win, Vec2i pos, Vec2i size,
            uint32_t border_width) {
                // Fixed-size windows (min==max hints): honour their own size,
                // only let the layout set the position.
                auto w = wsman.find_window_in_all(win);
                if (w && w->size_locked && w->width() > 0 && w->height() > 0)
                    size = w->size();
                (void)dispatch(command::SetWindowGeometry{ win, pos, size });
                (void)dispatch(command::SetWindowBorderWidth{ win, border_width });
                emit_backend_effect(BackendEffectKind::UpdateWindow, win);
            };

        layout::Config cfg_from_theme = layout_cfg;
        cfg_from_theme.border = settings.theme.border_thickness;
        cfg_from_theme.gap    = settings.theme.gap;

        if (is_cpp_layout) {
            layouts[active_layout](tiled, mon, cfg_from_theme, place);
        } else {
            // Lua layout: call fn({ windows={...}, monitor={x,y,width,height},
            //                       gap, master_factor, nmaster })
            // During the call, siren.layout.place() is routed through active_lua_sink_.
            if (!has_lua_host()) {
                LOG_ERR("arrange: Lua layout '%s' requested but no Lua host bound", active_layout.c_str());
                continue;
            }
            active_lua_sink_ = &place;
            auto lua_it = lua_layouts.find(active_layout);
            if (lua_it == lua_layouts.end()) {
                LOG_ERR("arrange: Lua layout '%s' not found", active_layout.c_str());
                active_lua_sink_ = nullptr;
                continue;
            }
            const auto& ref = lua_it->second;
            LuaContext  ctx = lua_host().context();

            lua_host().push_ref(ref);

            // Build the context table argument.
            ctx.new_table();

            // windows = { id, id, ... }
            ctx.new_table();
            for (int i = 0; i < (int)tiled.size(); ++i) {
                ctx.push_integer((int64_t)tiled[i]);
                ctx.raw_seti(-2, i + 1);
            }
            ctx.set_field(-2, "windows");

            // monitor = { pos = Vec2, size = Vec2 }
            ctx.new_table();

            push_vec2(ctx, mon.pos());
            ctx.set_field(-2, "pos");

            push_vec2(ctx, mon.size());
            ctx.set_field(-2, "size");

            ctx.set_field(-2, "monitor");

            ctx.push_number(cfg_from_theme.master_factor); ctx.set_field(-2, "master_factor");
            ctx.push_integer(cfg_from_theme.nmaster);      ctx.set_field(-2, "nmaster");
            ctx.push_integer(cfg_from_theme.gap);          ctx.set_field(-2, "gap");
            ctx.push_integer(cfg_from_theme.border);       ctx.set_field(-2, "border");

            lua_host().pcall(1, 0, active_layout.c_str());
            active_lua_sink_ = nullptr;
        }

        // Apply theme border to floating windows — layout skips them, so their
        // border_width would otherwise stay at 0 (no border drawn by X).
        // Fullscreen and borderless windows keep border_width=0.
        for (auto& w : ws.windows) {
            if (!w || !w->is_visible() || !w->floating || w->fullscreen || w->borderless) continue;
            if (w->border_width == (uint32_t)settings.theme.border_thickness) continue;
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
    // Capture target monitor before switch_to so we know if it's the focused one.
    int target_mon = cmd.monitor_index.has_value() ? *cmd.monitor_index
                   : wsman.monitor_of_workspace(cmd.workspace_id);
    if (target_mon < 0)
        target_mon = wsman.get_focused_monitor();

    bool switched = wsman.switch_to(cmd.workspace_id, settings.monitor_aliases,
            cmd.monitor_index.has_value() ? *cmd.monitor_index : -1,
            settings.monitor_compose);
    if (!switched)
        return false;

    sync_workspace_visibility();
    arrange();

    // Only update X focus when switching workspace on the focused monitor.
    // Switching on a background monitor must not steal keyboard input.
    if (target_mon == wsman.get_focused_monitor())
        sync_current_focus();

    evaluate_workspace_fullscreen(cmd.workspace_id);
    emit_workspace_switched(cmd.workspace_id);
    return true;
}

bool Core::dispatch(const command::MoveWindowToWorkspace& cmd) {
    if (cmd.workspace_id < 0 || cmd.workspace_id >= workspace_count())
        return false;

    auto w = wsman.find_window_in_all(cmd.window);
    if (!w)
        return false;

    int  src_ws_id = workspace_of_window(cmd.window);
    bool is_fs     = w->borderless || w->fullscreen;

    wsman.move_window_to(cmd.workspace_id, w);
    emit_window_assigned_to_workspace(cmd.window, cmd.workspace_id);

    sync_workspace_visibility();
    arrange();

    // Re-pin fullscreen/borderless geometry to the destination monitor
    // and evaluate stacking on both source and destination workspaces.
    if (is_fs) {
        pin_fullscreen_to_monitor(*w, cmd.workspace_id);
        evaluate_workspace_fullscreen(src_ws_id);
        evaluate_workspace_fullscreen(cmd.workspace_id);
    }

    bool moved_ws_visible = is_workspace_visible(cmd.workspace_id);
    bool focus_moved      = moved_ws_visible && w->is_visible() && !w->suppress_focus_once;
    if (focus_moved) {
        wsman.focus_window(w->id);
        emit_backend_effect(BackendEffectKind::FocusWindow, w->id);
        emit_focus_changed(w->id);
    } else {
        sync_current_focus();
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

    const Monitor* mon = nullptr;
    for (auto& m : wsman.get_monitors()) {
        if (m.active_ws == ws_id) {
            mon = &m;
            break;
        }
    }

    if (!mon) {
        const auto& mons = wsman.get_monitors();
        // Prefer the monitor that owns the workspace (may be inactive).
        int         owner = wsman.monitor_of_workspace(ws_id);
        if (owner >= 0 && owner < (int)mons.size())
            mon = &mons[(size_t)owner];
        else if (!mons.empty())
            mon = &mons.front();
    }

    if (cmd.enabled) {
        if (!w->fullscreen) {
            w->border_before_fullscreen = w->border_width;
            w->pos_before_fullscreen    = w->pos();
            w->size_before_fullscreen   = w->size();
        }
        w->fullscreen   = true;
        w->border_width = 0;
        mark_window_dirty(cmd.window, WindowFlush::BorderWidth);
        if (!cmd.preserve_geometry && mon) {
            auto [phy_pos, phy_size] = mon->physical();
            w->pos()                 = phy_pos;
            w->size()                = phy_size;
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
    w->border_width = w->border_before_fullscreen;
    mark_window_dirty(cmd.window, WindowFlush::BorderWidth);
    if (w->floating) {
        w->pos()  = w->pos_before_fullscreen;
        w->size() = w->size_before_fullscreen;
        mark_window_dirty(cmd.window, WindowFlush::Geometry);
    }
    arrange();
    if (ws_id >= 0)
        evaluate_workspace_fullscreen(ws_id);
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
    w->wm_instance = cmd.wm_instance;
    w->wm_class    = cmd.wm_class;
    w->type        = cmd.type;

    // Classify from geometry hints supplied by the backend.
    // Self-managed: client had _NET_WM_STATE_FULLSCREEN before MapRequest and
    //   covers the monitor (not XEMBED) — client controls its own geometry.
    // WM-borderless: fixed-size window that covers the entire monitor — the WM
    //   pins it to physical monitor bounds (no bar inset, no border).
    //   no_decorations is NOT required: Java/LibGDX games (Slay the Spire) use
    //   fixed-size fullscreen without MOTIF hints.
    const auto& h                  = cmd.hints;
    bool        self_managed       = h.pre_fullscreen && !h.is_xembed && h.covers_monitor;
    bool        wm_borderless      = !self_managed && h.covers_monitor && h.fixed_size;
    bool        will_be_borderless = self_managed || wm_borderless;

    w->size_locked       = h.fixed_size;
    w->no_input_focus    = h.never_focus;
    w->urgent            = h.urgent;
    w->preserve_position = h.static_gravity;
    w->size_min          = h.size_min;
    w->size_max          = h.size_max;
    w->size_inc          = h.size_inc;
    w->size_base         = h.size_base;

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
    if (!w->floating && (w->is_dialog() || w->type == WindowType::Utility
        || w->type == WindowType::Splash || cmd.transient_for != NO_WINDOW))
        dispatch(command::SetWindowFloating{ cmd.window, true });

    // Fixed-size non-borderless windows float.
    if (!w->floating && !w->borderless && w->size_locked && !will_be_borderless)
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
    w->borderless            = cmd.borderless;
    w->promote_to_borderless = false; // consumed
    // Self-managed clients (Wine/Proton) position render children relative to
    // inner origin (outer + border_width).  Zeroing the X border shifts the
    // inner origin and causes a 1px render offset — leave their border alone.
    if (cmd.borderless && w->border_width != 0 && !w->is_self_managed()) {
        w->border_width = 0;
        emit_backend_effect(BackendEffectKind::UpdateWindow, w->id);
    }
    {
        int ws_id = wsman.workspace_of_window(cmd.window);
        if (ws_id >= 0)
            evaluate_workspace_fullscreen(ws_id);
    }
    if (cmd.borderless) {
        int ws_id   = wsman.workspace_of_window(cmd.window);
        int mon_idx = wsman.monitor_of_workspace(ws_id);
        if (mon_idx >= 0)
            emit_borderless_activated(cmd.window, mon_idx);
    } else {
        // Check if any borderless window remains on any monitor.
        const auto& mons = wsman.all_monitor_states();
        bool        any  = false;
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

    int old_mon = wsman.get_focused_monitor();
    if (n != old_mon) {
        // Clear X focus on the old monitor.
        emit_backend_effect(BackendEffectKind::FocusRoot);
        emit_focus_changed(NO_WINDOW);

        wsman.set_focused_monitor(n);

        // Restore last focused window on the new monitor.
        WindowId win = wsman.last_focused_window(n, mon->active_ws);
        if (win != NO_WINDOW && wsman.find_window_in_all(win)) {
            wsman.focus_window(win);
            emit_backend_effect(BackendEffectKind::FocusWindow, win);
            emit_focus_changed(win);
        } else {
            sync_current_focus();
        }
    }

    emit_warp_pointer(mon->center());
    return true;
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
    // New monitors come in fresh with top/bottom insets = 0; no reset needed.
    wsman.assign_workspaces(settings.monitor_aliases,
        settings.monitor_compose);
    emit_display_topology_changed();
    reconcile();
    return true;
}

bool Core::dispatch(const command::ApplyMonitorTopInset& cmd) {
    const auto& mons = wsman.all_monitor_states();
    auto        apply_to = [&](int i) {
            int delta = cmd.inset_px - mons[i].top_inset();
            if (delta != 0)
                wsman.adjust_monitor_inset(i, delta, 0);
        };
    if (cmd.monitor_idx < 0) {
        for (int i = 0; i < (int)mons.size(); i++) apply_to(i);
    } else if (cmd.monitor_idx < (int)mons.size()) {
        apply_to(cmd.monitor_idx);
    }
    return true;
}

bool Core::dispatch(const command::ApplyMonitorBottomInset& cmd) {
    const auto& mons = wsman.all_monitor_states();
    auto        apply_to = [&](int i) {
            int delta = cmd.inset_px - mons[i].bottom_inset();
            if (delta != 0)
                wsman.adjust_monitor_inset(i, 0, delta);
        };
    if (cmd.monitor_idx < 0) {
        for (int i = 0; i < (int)mons.size(); i++) apply_to(i);
    } else if (cmd.monitor_idx < (int)mons.size()) {
        apply_to(cmd.monitor_idx);
    }
    return true;
}

bool Core::dispatch(const command::SetLayout& cmd) {
    if (!layouts.count(cmd.name)) {
        LOG_ERR("set_layout: unknown layout '%s'", cmd.name.c_str());
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
    int ws_id = wsman.workspace_of_window(cmd.window);
    wsman.remove_window_from_all(cmd.window);
    pending_window_flushes.erase(cmd.window);
    if (ws_id >= 0)
        evaluate_workspace_fullscreen(ws_id);
    return true;
}

bool Core::dispatch(const command::SetWindowGeometry& cmd) {
    auto w = wsman.find_window_in_all(cmd.window);
    if (!w)
        return false;
    w->pos()  = cmd.pos;
    w->size() = w->floating ? apply_size_hints(*w, cmd.size) : cmd.size;
    mark_window_dirty(cmd.window, WindowFlush::Geometry);
    return true;
}

bool Core::dispatch(const command::SetWindowPosition& cmd) {
    auto w = wsman.find_window_in_all(cmd.window);
    if (!w)
        return false;
    w->pos() = cmd.pos;
    mark_window_dirty(cmd.window, WindowFlush::Position);
    return true;
}

bool Core::dispatch(const command::SetWindowSize& cmd) {
    auto w = wsman.find_window_in_all(cmd.window);
    if (!w)
        return false;
    // Apply WM_NORMAL_HINTS constraints only for floating/borderless windows.
    // Tiled windows are sized by the layout engine which already respects borders.
    w->size() = (w->floating || w->borderless) ? apply_size_hints(*w, cmd.size) : cmd.size;
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

    if (!pending || !(pending->dirty & WindowFlush::X))           w->x() = cmd.pos.x();
    if (!pending || !(pending->dirty & WindowFlush::Y))           w->y() = cmd.pos.y();
    if (!pending || !(pending->dirty & WindowFlush::Width))       w->width() = cmd.size.x();
    if (!pending || !(pending->dirty & WindowFlush::Height))      w->height() = cmd.size.y();
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
