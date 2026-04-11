#include <ws.hpp>
#include <log.hpp>
#include <algorithm>

// Always-on consistency check: logs an error in release, aborts in debug.
// Runs only after mutations (not on the hot read path), so the overhead is acceptable.
#ifdef NDEBUG
#define WS_ASSERT_CONSISTENT() do { \
    if (!indexes_consistent()) \
        LOG_ERR("WorkspaceManager: index consistency check FAILED at %s:%d — state is corrupt", \
            __FILE__, __LINE__); \
} while (0)
#else
#define WS_ASSERT_CONSISTENT() do { \
    if (!indexes_consistent()) { \
        LOG_ERR("WorkspaceManager: index consistency check FAILED at %s:%d — state is corrupt", \
            __FILE__, __LINE__); \
        std::abort(); \
    } \
} while (0)
#endif

// ────────────────────────────────────────────────────────────────────────────
// Workspace methods
// ────────────────────────────────────────────────────────────────────────────

void Workspace::add_window(std::shared_ptr<swm::Window> w) {
    windows.push_back(w);
    if (current == -1)
        current = 0;
}

bool Workspace::remove_window(WindowId win, std::shared_ptr<swm::Window>* removed) {
    for (int i = 0; i < (int)windows.size(); i++) {
        if (!windows[i] || windows[i]->id != win)
            continue;

        if (removed)
            *removed = windows[i];

        windows.erase(windows.begin() + i);

        if (windows.empty()) {
            current = -1;
        } else if (current > i) {
            current--;
        } else if (current == i) {
            current = std::min(i, (int)windows.size() - 1);
        } else if (current >= (int)windows.size()) {
            current = (int)windows.size() - 1;
        }
        return true;
    }
    return false;
}

std::shared_ptr<swm::Window> Workspace::focused() {
    bool any_visible = false;
    for (auto& w : windows)
        if (w && w->is_visible()) {
            any_visible = true;
            break;
        }

    if (current >= 0 && current < (int)windows.size()) {
        auto cur = windows[current];
        if (cur && (!any_visible || cur->is_visible()))
            return cur;
    }

    for (auto& w : windows) {
        if (!w)
            continue;
        if (any_visible && !w->is_visible())
            continue;
        return w;
    }

    return nullptr;
}

std::shared_ptr<swm::Window> Workspace::advance_focus() {
    bool any_visible = false;
    for (auto& w : windows)
        if (w && w->is_visible()) {
            any_visible = true;
            break;
        }

    if (current >= 0 && current < (int)windows.size()) {
        auto cur = windows[current];
        if (cur && (!any_visible || cur->is_visible()))
            return cur;
    }

    for (int i = 0; i < (int)windows.size(); i++) {
        auto& w = windows[i];
        if (!w)
            continue;
        if (any_visible && !w->is_visible())
            continue;
        current = i;
        return w;
    }

    return nullptr;
}

std::shared_ptr<const swm::Window> Workspace::focused() const {
    bool any_visible = false;
    for (const auto& w : windows)
        if (w && w->is_visible()) {
            any_visible = true;
            break;
        }

    if (current >= 0 && current < (int)windows.size()) {
        auto cur = windows[(size_t)current];
        if (cur && (!any_visible || cur->is_visible()))
            return cur;
    }

    for (const auto& w : windows) {
        if (!w)
            continue;
        if (any_visible && !w->is_visible())
            continue;
        return w;
    }

    return nullptr;
}

void Workspace::focus_next() {
    int  n = (int)windows.size();
    if (n == 0) return;
    bool any_visible = false;
    for (auto& w : windows)
        if (w && w->is_visible()) {
            any_visible = true;
            break;
        }
    for (int i = 1; i <= n; i++) {
        int idx = (current + i) % n;
        if (!windows[idx])
            continue;
        if (any_visible && !windows[idx]->is_visible())
            continue;
        if (!windows[idx]->floating) {
            current = idx; return;
        }
    }
    for (int i = 1; i <= n; i++) {
        int idx = (current + i) % n;
        if (!windows[idx])
            continue;
        if (any_visible && !windows[idx]->is_visible())
            continue;
        current = idx;
        return;
    }
}

void Workspace::focus_prev() {
    int  n = (int)windows.size();
    if (n == 0) return;
    bool any_visible = false;
    for (auto& w : windows)
        if (w && w->is_visible()) {
            any_visible = true;
            break;
        }
    for (int i = 1; i <= n; i++) {
        int idx = (current - i + n) % n;
        if (!windows[idx])
            continue;
        if (any_visible && !windows[idx]->is_visible())
            continue;
        if (!windows[idx]->floating) {
            current = idx; return;
        }
    }
    for (int i = 1; i <= n; i++) {
        int idx = (current - i + n) % n;
        if (!windows[idx])
            continue;
        if (any_visible && !windows[idx]->is_visible())
            continue;
        current = idx;
        return;
    }
}

// ────────────────────────────────────────────────────────────────────────────
// WorkspaceManager — private helpers
// ────────────────────────────────────────────────────────────────────────────

int WorkspaceManager::monitor_index_by_name(const std::string& name) const {
    for (int i = 0; i < (int)monitors.size(); i++)
        if (monitors[i].name == name)
            return i;
    return -1;
}

int WorkspaceManager::index_of_ws_in_pool(const std::vector<int>& pool, int ws_id) const {
    for (int i = 0; i < (int)pool.size(); i++)
        if (pool[i] == ws_id)
            return i;
    return -1;
}

int WorkspaceManager::monitor_of(int ws_id) const {
    if (!is_ws_valid(ws_id) || ws_id >= (int)ws_owner.size())
        return -1;
    if (is_mon_valid(ws_owner[ws_id]))
        return ws_owner[ws_id];
    return -1;
}

int WorkspaceManager::local_index_of(int mon_idx, int ws_id) const {
    if (mon_idx < 0 || mon_idx >= (int)monitor_pools.size())
        return -1;
    return index_of_ws_in_pool(monitor_pools[mon_idx], ws_id);
}

int WorkspaceManager::active_ws_of_monitor(int mon_idx) const {
    if (mon_idx < 0 || mon_idx >= (int)monitor_pools.size())
        return -1;
    const auto& pool = monitor_pools[mon_idx];
    if (pool.empty())
        return -1;
    int idx = (mon_idx < (int)monitor_active_local.size())
        ? monitor_active_local[mon_idx]
        : -1;
    if (idx < 0 || idx >= (int)pool.size())
        return pool.front();
    return pool[idx];
}

int WorkspaceManager::primary_monitor_index(const std::vector<MonitorAlias>& aliases,
    const MonitorCompose& compose) const {
    if (!compose.primary.empty()) {
        int mon = resolve_alias_monitor(compose.primary, aliases);
        if (mon >= 0)
            return mon;
    }
    return monitors.empty() ? -1 : 0;
}

void WorkspaceManager::sync_monitors_active_ws() {
    for (int i = 0; i < (int)monitors.size(); i++)
        monitors[i].active_ws = active_ws_of_monitor(i);
}

void WorkspaceManager::ensure_monitor_focus_size() {
    if ((int)monitor_focus_.size() < (int)monitors.size())
        monitor_focus_.resize(monitors.size());
}

void WorkspaceManager::sync_focus_state() {
    ensure_monitor_focus_size();
    focus_.monitor = focused_monitor_;
    focus_.ws_id   = active_ws_of_monitor(focused_monitor_);
    if (is_ws_valid(focus_.ws_id)) {
        auto w = workspaces[focus_.ws_id].focused();
        focus_.window = w ? w->id : NO_WINDOW;
    } else {
        focus_.window = NO_WINDOW;
    }
}

void WorkspaceManager::select_valid_focused_monitor() {
    if (monitors.empty()) {
        focused_monitor_ = 0;
        return;
    }
    if (!is_mon_valid(focused_monitor_))
        focused_monitor_ = 0;
    if (active_ws_of_monitor(focused_monitor_) >= 0)
        return;
    for (int i = 0; i < (int)monitors.size(); i++) {
        if (active_ws_of_monitor(i) >= 0) {
            focused_monitor_ = i;
            return;
        }
    }
    focused_monitor_ = 0;
}

WindowId WorkspaceManager::last_focused_window(int mon_idx, int ws_id) const {
    if (mon_idx < 0 || mon_idx >= (int)monitor_focus_.size())
        return NO_WINDOW;
    auto it = monitor_focus_[mon_idx].last_window_per_ws.find(ws_id);
    if (it == monitor_focus_[mon_idx].last_window_per_ws.end())
        return NO_WINDOW;
    WindowId win = it->second;
    // Validate that the window still exists in this workspace.
    auto     wit = window_workspace.find(win);
    if (wit == window_workspace.end() || wit->second != ws_id)
        return NO_WINDOW;
    return win;
}

void WorkspaceManager::set_focused_monitor(int mon_idx) {
    if (!is_mon_valid(mon_idx))
        return;
    focused_monitor_ = mon_idx;
    sync_focus_state();
}

int WorkspaceManager::resolve_alias_monitor(const std::string& alias,
    const std::vector<MonitorAlias>& aliases) const {
    for (auto& a : aliases) {
        if (a.alias == alias) {
            int mon = monitor_index_by_name(a.output);
            return mon;
        }
    }
    return monitor_index_by_name(alias);
}

void WorkspaceManager::rebuild_pools_from_owner(const std::vector<std::vector<int>>& seed_pools,
    const std::vector<int>& preferred_active_ws) {
    monitor_pools.assign(monitors.size(), {});
    monitor_active_local.resize(monitors.size(), -1);

    std::vector<bool> placed(workspaces.size(), false);
    auto              append_if_owned = [&](int mon_idx, int ws_id) {
            if (!is_mon_valid(mon_idx) || !is_ws_valid(ws_id))
                return;
            if (ws_id >= (int)ws_owner.size())
                return;
            if (ws_owner[ws_id] != mon_idx)
                return;
            if (placed[ws_id])
                return;
            monitor_pools[mon_idx].push_back(ws_id);
            placed[ws_id] = true;
        };

    int seeded_n = std::min((int)seed_pools.size(), (int)monitors.size());
    for (int mon_idx = 0; mon_idx < seeded_n; mon_idx++)
        for (int ws_id : seed_pools[mon_idx])
            append_if_owned(mon_idx, ws_id);

    for (int ws_id = 0; ws_id < (int)workspaces.size(); ws_id++) {
        int mon_idx = monitor_of(ws_id);
        append_if_owned(mon_idx, ws_id);
    }

    for (int mon_idx = 0; mon_idx < (int)monitor_pools.size(); mon_idx++) {
        const auto& pool = monitor_pools[mon_idx];
        if (pool.empty()) {
            monitor_active_local[mon_idx] = -1;
            continue;
        }
        int preferred_ws = (mon_idx < (int)preferred_active_ws.size())
            ? preferred_active_ws[mon_idx]
            : -1;
        int li = index_of_ws_in_pool(pool, preferred_ws);
        monitor_active_local[mon_idx] = (li >= 0) ? li : 0;
    }
}

static Workspace& fallback_workspace_instance() {
    static Workspace fb(-1, "[fallback]");
    return fb;
}

Workspace& WorkspaceManager::fallback_workspace() {
    return fallback_workspace_instance();
}

const Workspace& WorkspaceManager::fallback_workspace() const {
    return fallback_workspace_instance();
}

void WorkspaceManager::clear_window_indexes() {
    window_index.clear();
    window_workspace.clear();
}

void WorkspaceManager::index_window(WindowId win, const std::shared_ptr<swm::Window>& w, int ws_id) {
    if (win == NO_WINDOW || !w || !is_ws_valid(ws_id))
        return;
    window_index[win]     = w;
    window_workspace[win] = ws_id;
}

void WorkspaceManager::unindex_window(WindowId win) {
    window_index.erase(win);
    window_workspace.erase(win);

    // Purge stale focus references so we never try to restore focus
    // to a window that no longer exists.
    for (auto& mf : monitor_focus_) {
        for (auto it = mf.last_window_per_ws.begin(); it != mf.last_window_per_ws.end(); ) {
            if (it->second == win)
                it = mf.last_window_per_ws.erase(it);
            else
                ++it;
        }
    }
    if (focus_.window == win)
        focus_.window = NO_WINDOW;
}

void WorkspaceManager::rebuild_window_indexes() {
    clear_window_indexes();
    for (auto& ws : workspaces)
        for (auto& w : ws.windows)
            if (w)
                index_window(w->id, w, ws.id);
}

// ────────────────────────────────────────────────────────────────────────────
// WorkspaceManager — public methods
// ────────────────────────────────────────────────────────────────────────────

void WorkspaceManager::update_workspace_defs(const std::vector<WorkspaceDef>& defs) {
    const int old_count = (int)workspaces.size();
    const int new_count = defs.empty() ? old_count : (int)defs.size();

    // Update names and aliases for existing workspaces.
    for (int i = 0; i < std::min(new_count, old_count); ++i) {
        workspaces[i].name          = defs[i].name;
        workspaces[i].monitor_alias = defs[i].monitor;
    }

    // Add new workspaces if defs grew.
    for (int i = old_count; i < new_count; ++i) {
        workspaces.emplace_back(i, defs[i].name, defs[i].monitor);
        ws_owner.push_back(-1);
    }

    // Remove workspaces if defs shrank.
    if (new_count < old_count) {
        // Find a safe fallback workspace to receive displaced windows.
        // Prefer the first workspace on the same monitor; fall back to ws 0.
        auto fallback_for = [&](int ws_id) -> int {
                int mon = (ws_id < (int)ws_owner.size()) ? ws_owner[ws_id] : -1;
                if (mon >= 0 && mon < (int)monitor_pools.size()) {
                    for (int id : monitor_pools[mon]) {
                        if (id < new_count)
                            return id;
                    }
                }
                return 0;
            };

        for (int ws_id = new_count; ws_id < old_count; ++ws_id) {
            int dst = fallback_for(ws_id);
            // Move all windows to the fallback workspace.
            for (auto& w : workspaces[ws_id].windows) {
                if (!w) continue;
                workspaces[dst].add_window(w);
                index_window(w->id, w, dst);
            }
            workspaces[ws_id].windows.clear();
            workspaces[ws_id].current = -1;
        }

        // If any monitor's active workspace is being removed, switch it to fallback.
        for (int mon = 0; mon < (int)monitors.size(); ++mon) {
            int active = active_ws_of_monitor(mon);
            if (active >= new_count) {
                int   dst = fallback_for(active);
                // Find dst in this monitor's pool and activate it.
                auto& pool = monitor_pools[mon];
                auto  it   = std::find(pool.begin(), pool.end(), dst);
                if (it != pool.end())
                    monitor_active_local[mon] = (int)std::distance(pool.begin(), it);
                else if (!pool.empty())
                    monitor_active_local[mon] = 0;
            }
        }

        // Prune removed workspaces from all pools and parked state.
        for (auto& pool : monitor_pools) {
            pool.erase(std::remove_if(pool.begin(), pool.end(),
                [&](int id) {
                    return id >= new_count;
                }), pool.end());
        }
        for (auto& [name, pool] : parked_pools) {
            pool.erase(std::remove_if(pool.begin(), pool.end(),
                [&](int id) {
                    return id >= new_count;
                }), pool.end());
        }
        for (auto& [name, active] : parked_active_ws) {
            if (active >= new_count)
                active = 0;
        }

        workspaces.erase(workspaces.begin() + new_count, workspaces.end());
        ws_owner.resize(new_count);

        sync_focus_state();
    }
}

void WorkspaceManager::init_from_defs(const std::vector<WorkspaceDef>& defs,
    const std::vector<MonitorAlias>& aliases,
    const MonitorCompose& compose) {
    workspaces.clear();
    clear_window_indexes();
    ws_owner.clear();
    monitor_pools.clear();
    monitor_active_local.clear();
    parked_pools.clear();
    parked_active_ws.clear();

    if (defs.empty()) {
        for (int i = 0; i < 9; ++i)
            workspaces.emplace_back(i, "[" + std::to_string(i + 1) + "]");
    } else {
        for (int i = 0; i < (int)defs.size(); ++i)
            workspaces.emplace_back(i, defs[i].name, defs[i].monitor);
    }

    ws_owner.assign(workspaces.size(), -1);
    monitor_pools.assign(monitors.size(), {});
    monitor_active_local.assign(monitors.size(), -1);

    assign_workspaces(aliases, compose);
}

void WorkspaceManager::assign_workspaces(const std::vector<MonitorAlias>& aliases,
    const MonitorCompose& compose) {
    if ((int)ws_owner.size() != (int)workspaces.size())
        ws_owner.assign(workspaces.size(), -1);

    if (monitors.empty()) {
        std::fill(ws_owner.begin(), ws_owner.end(), -1);
        monitor_pools.clear();
        monitor_active_local.clear();
        return;
    }

    for (int ws_id = 0; ws_id < (int)ws_owner.size(); ws_id++)
        if (!is_mon_valid(ws_owner[ws_id]))
            ws_owner[ws_id] = -1;

    for (int ws_id = 0; ws_id < (int)workspaces.size(); ws_id++) {
        const auto& ws = workspaces[ws_id];
        if (ws.monitor_alias.empty())
            continue;
        int target_mon = resolve_alias_monitor(ws.monitor_alias, aliases);
        if (target_mon >= 0) {
            ws_owner[ws_id] = target_mon;
        } else if (!is_mon_valid(ws_owner[ws_id])) {
            ws_owner[ws_id] = -1;
        }
    }

    int rr = primary_monitor_index(aliases, compose);
    if (rr < 0)
        rr = 0;
    for (int ws_id = 0; ws_id < (int)workspaces.size(); ws_id++) {
        if (ws_owner[ws_id] != -1)
            continue;
        ws_owner[ws_id] = rr;
        rr              = (rr + 1) % (int)monitors.size();
    }

    std::vector<int> preferred_active_ws(monitors.size(), -1);
    for (int mon_idx = 0; mon_idx < (int)monitors.size(); mon_idx++)
        preferred_active_ws[mon_idx] = active_ws_of_monitor(mon_idx);

    auto seed_pools = monitor_pools;
    rebuild_pools_from_owner(seed_pools, preferred_active_ws);
    select_valid_focused_monitor();
    sync_monitors_active_ws();
    sync_focus_state();
}

void WorkspaceManager::set_monitors(std::vector<Monitor> mons) {
    std::vector<Monitor>          old_monitors = monitors;
    std::vector<std::vector<int>> old_pools    = monitor_pools;
    std::vector<int>              old_owner    = ws_owner;
    std::vector<int>              old_active_ws((int)old_monitors.size(), -1);
    for (int i = 0; i < (int)old_monitors.size(); i++)
        old_active_ws[i] = (i < (int)old_pools.size() && i < (int)monitor_active_local.size())
            ? active_ws_of_monitor(i)
            : -1;

    std::string old_focused_name;
    if (focused_monitor_ >= 0 && focused_monitor_ < (int)old_monitors.size())
        old_focused_name = old_monitors[focused_monitor_].name;

    // Snapshot per-monitor focus keyed by monitor name for migration.
    std::unordered_map<std::string, MonitorFocusState> old_mfocus;
    for (int i = 0; i < (int)old_monitors.size() && i < (int)monitor_focus_.size(); i++)
        old_mfocus[old_monitors[i].name] = monitor_focus_[i];

    std::unordered_map<std::string, int> old_index;
    for (int i = 0; i < (int)old_monitors.size(); i++)
        old_index[old_monitors[i].name] = i;

    std::unordered_map<std::string, bool> new_has;
    for (auto& m : mons)
        new_has[m.name] = true;

    for (int i = 0; i < (int)old_monitors.size(); i++) {
        const auto& name = old_monitors[i].name;
        if (new_has.count(name))
            continue;
        parked_pools[name]     = (i < (int)old_pools.size()) ? old_pools[i] : std::vector<int>{};
        parked_active_ws[name] = (i < (int)old_active_ws.size()) ? old_active_ws[i] : -1;
    }

    monitors = std::move(mons);

    std::vector<int> new_owner((int)workspaces.size(), -1);
    for (int ws_id = 0; ws_id < (int)workspaces.size(); ws_id++) {
        if (ws_id >= (int)old_owner.size())
            continue;
        int old_mon = old_owner[ws_id];
        if (old_mon < 0 || old_mon >= (int)old_monitors.size())
            continue;
        const auto& old_name = old_monitors[old_mon].name;
        auto        it_new   = std::find_if(monitors.begin(), monitors.end(),
                [&](const Monitor& m) {
                    return m.name == old_name;
                });
        if (it_new != monitors.end())
            new_owner[ws_id] = (int)std::distance(monitors.begin(), it_new);
    }

    std::vector<std::vector<int>> seed_pools(monitors.size());
    std::vector<int>              preferred_active_ws((int)monitors.size(), -1);

    for (int i = 0; i < (int)monitors.size(); i++) {
        const auto& name   = monitors[i].name;
        auto        it_old = old_index.find(name);
        if (it_old != old_index.end()) {
            int oi = it_old->second;
            if (oi < (int)old_pools.size())
                seed_pools[i] = old_pools[oi];
            if (oi < (int)old_active_ws.size())
                preferred_active_ws[i] = old_active_ws[oi];
            continue;
        }

        auto it_pool = parked_pools.find(name);
        if (it_pool != parked_pools.end()) {
            seed_pools[i] = it_pool->second;
            for (int ws_id : it_pool->second)
                if (is_ws_valid(ws_id))
                    new_owner[ws_id] = i;

            int  active_ws = -1;
            auto it_aw     = parked_active_ws.find(name);
            if (it_aw != parked_active_ws.end())
                active_ws = it_aw->second;
            preferred_active_ws[i] = active_ws;
            parked_pools.erase(it_pool);
            parked_active_ws.erase(name);
        }
    }

    ws_owner = std::move(new_owner);
    rebuild_pools_from_owner(seed_pools, preferred_active_ws);

    // Migrate per-monitor focus state by monitor name.
    monitor_focus_.assign(monitors.size(), MonitorFocusState{});
    for (int i = 0; i < (int)monitors.size(); i++) {
        auto it = old_mfocus.find(monitors[i].name);
        if (it != old_mfocus.end())
            monitor_focus_[i] = std::move(it->second);
    }

    int focused_by_name = monitor_index_by_name(old_focused_name);
    if (focused_by_name >= 0)
        focused_monitor_ = focused_by_name;
    else if (focused_monitor_ >= (int)monitors.size())
        focused_monitor_ = 0;

    select_valid_focused_monitor();
    sync_monitors_active_ws();
    sync_focus_state();
}

void WorkspaceManager::adjust_monitor_inset(int mon_idx, MonitorEdge edge, int delta) {
    if (mon_idx < 0 || mon_idx >= (int)monitors.size() || delta == 0)
        return;
    auto& mon = monitors[mon_idx];
    switch (edge) {
        case MonitorEdge::Top:
            mon.y()        += delta;
            mon.height()    = std::max(0, mon.height() - delta);
            mon.top_inset_ += delta;
            break;
        case MonitorEdge::Bottom:
            mon.height()       = std::max(0, mon.height() - delta);
            mon.bottom_inset_ += delta;
            break;
        case MonitorEdge::Left:
            mon.x()         += delta;
            mon.width()      = std::max(0, mon.width() - delta);
            mon.left_inset_ += delta;
            break;
        case MonitorEdge::Right:
            mon.width()       = std::max(0, mon.width() - delta);
            mon.right_inset_ += delta;
            break;
    }
}

Workspace& WorkspaceManager::current() {
    if (workspaces.empty())
        return fallback_workspace();
    int ws_id = active_ws_of_monitor(focused_monitor_);
    if (!is_ws_valid(ws_id)) {
        for (int i = 0; i < (int)monitors.size(); i++) {
            ws_id = active_ws_of_monitor(i);
            if (is_ws_valid(ws_id))
                return workspaces[ws_id];
        }
        return workspaces.front();
    }
    return workspaces[ws_id];
}

const Workspace& WorkspaceManager::current() const {
    if (workspaces.empty())
        return fallback_workspace();
    int ws_id = active_ws_of_monitor(focused_monitor_);
    if (!is_ws_valid(ws_id)) {
        for (int i = 0; i < (int)monitors.size(); i++) {
            ws_id = active_ws_of_monitor(i);
            if (is_ws_valid(ws_id))
                return workspaces[ws_id];
        }
        return workspaces.front();
    }
    return workspaces[ws_id];
}

int WorkspaceManager::workspace_of_window(WindowId win) const {
    if (win == NO_WINDOW)
        return -1;
    auto it = window_workspace.find(win);
    if (it == window_workspace.end())
        return -1;
    if (!is_ws_valid(it->second))
        return -1;
    return it->second;
}

bool WorkspaceManager::window_belongs_to_workspace(WindowId win, int ws_id) const {
    auto it = window_workspace.find(win);
    return it != window_workspace.end() && it->second == ws_id;
}

bool WorkspaceManager::window_is_unique_across_workspaces(WindowId win) const {
    if (win == NO_WINDOW)
        return true;
    int found = 0;
    for (const auto& ws : workspaces) {
        for (const auto& w : ws.windows) {
            if (w && w->id == win)
                found++;
            if (found > 1)
                return false;
        }
    }
    return true;
}

bool WorkspaceManager::monitor_workspace_pools_have_no_duplicates() const {
    std::vector<int> seen(workspaces.size(), 0);
    for (const auto& pool : monitor_pools) {
        for (int ws_id : pool) {
            if (!is_ws_valid(ws_id))
                return false;
            seen[(size_t)ws_id]++;
            if (seen[(size_t)ws_id] > 1)
                return false;
        }
    }
    return true;
}

size_t WorkspaceManager::indexed_window_count() const {
    return window_index.size();
}

bool WorkspaceManager::indexes_consistent() const {
    if (window_index.size() != window_workspace.size())
        return false;
    for (const auto& [win, ptr] : window_index) {
        if (!ptr || ptr->id != win)
            return false;
        auto it_ws = window_workspace.find(win);
        if (it_ws == window_workspace.end() || !is_ws_valid(it_ws->second))
            return false;
    }
    return true;
}

bool WorkspaceManager::switch_to(int ws_id,
    const std::vector<MonitorAlias>& aliases,
    int monitor_hint,
    const MonitorCompose& compose) {
    if (!is_ws_valid(ws_id) || monitors.empty())
        return false;

    int target_mon = monitor_of(ws_id);
    if (target_mon < 0) {
        if (is_mon_valid(monitor_hint)) {
            target_mon = monitor_hint;
        } else {
            const auto& ws = workspaces[ws_id];
            if (!ws.monitor_alias.empty())
                target_mon = resolve_alias_monitor(ws.monitor_alias, aliases);
            if (!is_mon_valid(target_mon))
                target_mon = primary_monitor_index(aliases, compose);
            if (!is_mon_valid(target_mon))
                target_mon = is_mon_valid(focused_monitor_) ? focused_monitor_ : 0;
        }
        ws_owner[ws_id] = target_mon;
    }

    int li = local_index_of(target_mon, ws_id);
    if (li < 0) {
        monitor_pools[target_mon].push_back(ws_id);
        li = (int)monitor_pools[target_mon].size() - 1;
    }
    monitor_active_local[target_mon] = li;
    // Do NOT update focused_monitor_ here: switching a workspace on any monitor
    // must not steal focus from the monitor the user is currently on.
    sync_monitors_active_ws();
    sync_focus_state();
    return true;
}

void WorkspaceManager::add_window(std::shared_ptr<swm::Window> w, int ws_id) {
    if (!w || workspaces.empty())
        return;
    int target = is_ws_valid(ws_id) ? ws_id : active_ws_of_monitor(focused_monitor_);
    if (!is_ws_valid(target))
        target = 0;

    WindowId win       = w->id;
    auto     it_old_ws = window_workspace.find(win);
    if (it_old_ws != window_workspace.end()) {
        int old_ws = it_old_ws->second;
        if (is_ws_valid(old_ws) && old_ws == target)
            return;
        if (is_ws_valid(old_ws))
            workspaces[old_ws].remove_window(win);
    }

    workspaces[target].add_window(w);
    index_window(win, w, target);
    WS_ASSERT_CONSISTENT();
}

std::shared_ptr<swm::Window> WorkspaceManager::create_window(WindowId win, int ws_id) {
    std::shared_ptr<swm::Window> window;
    if (window_factory_) {
        window = window_factory_(win);
    } else {
        window     = std::make_shared<swm::Window>();
        window->id = win;
    }
    add_window(window, ws_id);
    return window;
}

void WorkspaceManager::remove_window(WindowId win, int ws_id) {
    if (is_ws_valid(ws_id)) {
        if (workspaces[ws_id].remove_window(win))
            unindex_window(win);
        WS_ASSERT_CONSISTENT();
        sync_focus_state();
        return;
    }
    int active = active_ws_of_monitor(focused_monitor_);
    if (is_ws_valid(active))
        if (workspaces[active].remove_window(win))
            unindex_window(win);
    WS_ASSERT_CONSISTENT();
    sync_focus_state();
}

std::shared_ptr<swm::Window> WorkspaceManager::find_window(WindowId win, int ws_id) {
    if (win == NO_WINDOW)
        return nullptr;

    if (is_ws_valid(ws_id)) {
        auto it = window_workspace.find(win);
        if (it == window_workspace.end() || it->second != ws_id)
            return nullptr;
        auto wit = window_index.find(win);
        if (wit != window_index.end())
            return wit->second;
        for (auto& window : workspaces[ws_id].windows)
            if (window && window->id == win)
                return window;
        return nullptr;
    }

    return find_window_in_all(win);
}

std::shared_ptr<const swm::Window> WorkspaceManager::find_window(WindowId win, int ws_id) const {
    if (win == NO_WINDOW)
        return nullptr;

    if (is_ws_valid(ws_id)) {
        auto it = window_workspace.find(win);
        if (it == window_workspace.end() || it->second != ws_id)
            return nullptr;
        auto wit = window_index.find(win);
        if (wit != window_index.end())
            return wit->second;
        for (const auto& window : workspaces[(size_t)ws_id].windows)
            if (window && window->id == win)
                return window;
        return nullptr;
    }

    return find_window_in_all(win);
}

std::shared_ptr<swm::Window> WorkspaceManager::find_window_in_all(WindowId win) {
    auto it = window_index.find(win);
    return (it == window_index.end()) ? nullptr : it->second;
}

std::shared_ptr<const swm::Window> WorkspaceManager::find_window_in_all(WindowId win) const {
    auto it = window_index.find(win);
    return (it == window_index.end()) ? nullptr : it->second;
}

void WorkspaceManager::remove_window_from_all(WindowId win) {
    auto it = window_workspace.find(win);
    if (it != window_workspace.end() && is_ws_valid(it->second)) {
        workspaces[it->second].remove_window(win);
        unindex_window(win);
        WS_ASSERT_CONSISTENT();
        sync_focus_state();
        return;
    }
    for (auto& ws : workspaces)
        ws.remove_window(win);
    unindex_window(win);
    WS_ASSERT_CONSISTENT();
    sync_focus_state();
}

void WorkspaceManager::move_window_to(int ws_id, std::shared_ptr<swm::Window> w) {
    if (!w || !is_ws_valid(ws_id))
        return;
    WindowId win = w->id;
    auto     it  = window_workspace.find(win);
    if (it != window_workspace.end() && is_ws_valid(it->second)) {
        if (it->second == ws_id)
            return;
        workspaces[it->second].remove_window(win);
    } else {
        for (auto& ws : workspaces)
            ws.remove_window(win);
    }
    workspaces[ws_id].add_window(w);
    index_window(win, w, ws_id);
    WS_ASSERT_CONSISTENT();
    sync_focus_state();
}

bool WorkspaceManager::focus_window(WindowId win) {
    auto it_ws = window_workspace.find(win);
    if (it_ws == window_workspace.end() || !is_ws_valid(it_ws->second))
        return false;

    auto& ws = workspaces[it_ws->second];
    for (int i = 0; i < (int)ws.windows.size(); i++) {
        if (!ws.windows[i] || ws.windows[i]->id != win)
            continue;

        ws.current = i;
        // Clear urgency when the window receives focus (ICCCM §4.1.2.4).
        ws.windows[i]->urgent = false;
        int mon = monitor_of(ws.id);
        if (mon >= 0) {
            int li = local_index_of(mon, ws.id);
            if (li >= 0)
                monitor_active_local[mon] = li;
            // Record last focused window per monitor/workspace.
            ensure_monitor_focus_size();
            monitor_focus_[mon].last_window_per_ws[ws.id] = win;
            // Do NOT update focused_monitor_ here — focus_window() syncs state
            // but the active monitor is determined by user input, not window focus events.
            sync_monitors_active_ws();
            sync_focus_state();
        }
        return true;
    }
    return false;
}

std::shared_ptr<swm::Window> WorkspaceManager::focus_next() {
    current().focus_next();
    sync_focus_state();
    return current().focused();
}

std::shared_ptr<swm::Window> WorkspaceManager::focus_prev() {
    current().focus_prev();
    sync_focus_state();
    return current().focused();
}

bool WorkspaceManager::switch_local_index(int mon_idx, int local_idx) {
    if (mon_idx < 0 || mon_idx >= (int)monitor_pools.size())
        return false;
    auto& pool = monitor_pools[mon_idx];
    if (local_idx < 0 || local_idx >= (int)pool.size())
        return false;
    monitor_active_local[mon_idx] = local_idx;
    sync_monitors_active_ws();
    sync_focus_state();
    return true;
}

int WorkspaceManager::workspace_for_local_index(int mon_idx, int local_idx) const {
    if (mon_idx < 0 || mon_idx >= (int)monitor_pools.size())
        return -1;
    const auto& pool = monitor_pools[mon_idx];
    if (local_idx < 0 || local_idx >= (int)pool.size())
        return -1;
    return pool[local_idx];
}

int WorkspaceManager::monitor_at_point(int x, int y) const {
    for (int i = 0; i < (int)monitors.size(); i++) {
        const auto& m = monitors[i];
        if (m.contains({ x, y }))
            return i;
    }
    return -1;
}

int WorkspaceManager::monitor_at_physical_point(int x, int y) const {
    for (int i = 0; i < (int)monitors.size(); i++) {
        if (monitors[i].physical_contains({ x, y }))
            return i;
    }
    return -1;
}

bool WorkspaceManager::focus_monitor_at_point(int x, int y) {
    // Backends supply root (physical) coordinates — hit-test against physical rect
    // so clicks inside the top/bottom bar strip still pick the underlying monitor.
    int mon = monitor_at_physical_point(x, y);
    if (mon < 0)
        return false;
    bool changed = (mon != focused_monitor_);
    focused_monitor_ = mon;
    sync_focus_state();
    return changed;
}

Workspace& WorkspaceManager::workspace(int id) {
    if (workspaces.empty() || id < 0 || id >= (int)workspaces.size())
        return fallback_workspace();
    return workspaces[id];
}

const Workspace& WorkspaceManager::workspace(int id) const {
    if (workspaces.empty() || id < 0 || id >= (int)workspaces.size())
        return fallback_workspace();
    return workspaces[id];
}

bool WorkspaceManager::zoom_focused() {
    auto& ws      = current();
    auto  focused = ws.focused();
    if (!focused || focused->floating)
        return false;

    std::vector<int> tiled_idx;
    for (int i = 0; i < (int)ws.windows.size(); i++) {
        if (ws.windows[i] && !ws.windows[i]->floating)
            tiled_idx.push_back(i);
    }

    if (tiled_idx.size() < 2)
        return false;

    int focused_pos = -1;
    for (int i = 0; i < (int)tiled_idx.size(); i++) {
        if (ws.windows[tiled_idx[i]] == focused) {
            focused_pos = i;
            break;
        }
    }

    if (focused_pos < 0)
        return false;

    if (focused_pos == 0) {
        std::swap(ws.windows[tiled_idx[0]], ws.windows[tiled_idx[1]]);
        ws.current = tiled_idx[1];
    } else {
        std::swap(ws.windows[tiled_idx[0]], ws.windows[tiled_idx[focused_pos]]);
        ws.current = tiled_idx[0];
    }
    return true;
}
