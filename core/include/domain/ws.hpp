#pragma once

#include <backend/events.hpp>
#include <config/config_types.hpp>
#include <domain/window.hpp>
#include <domain/monitor.hpp>
#include <algorithm>
#include <functional>
#include <unordered_map>
#include <cassert>

// Factory function for creating Window instances.
// Backend supplies its own factory to create subclassed windows (e.g. X11Window).
using WindowFactory = std::function<std::shared_ptr<swm::Window>(WindowId)>;

enum class WorkspaceMode { Normal, Fullscreen };

struct Workspace {
    int                                       id;
    std::vector<std::shared_ptr<swm::Window>> windows;
    int                                       current = -1;
    std::string                               name;
    std::string                               monitor_alias;  // which monitor alias this ws prefers
    WorkspaceMode                             mode = WorkspaceMode::Normal;

    // Returns true if any visible window on this workspace is fullscreen or borderless.
    bool has_fullscreen_window() const {
        for (auto& w : windows)
            if (w && w->is_visible() && (w->fullscreen || w->borderless))
                return true;
        return false;
    }

    Workspace(int id, std::string name, std::string monitor_alias = "")
        : id(id), name(std::move(name)), monitor_alias(std::move(monitor_alias)) {}

    void add_window(std::shared_ptr<swm::Window> w);
    bool remove_window(WindowId win, std::shared_ptr<swm::Window>* removed = nullptr);

    // Pure query: returns the window that would be focused, without changing state.
    std::shared_ptr<swm::Window>       focused();
    std::shared_ptr<const swm::Window> focused() const;

    // Finds a suitable focused window and advances the internal cursor.
    // Use when you actually want to update which window is "current".
    std::shared_ptr<swm::Window> advance_focus();

    void                         focus_next();
    void                         focus_prev();
};

using WorkspaceState = Workspace;

struct FocusState {
    MonitorId   monitor = MonitorId{ 0 };
    WorkspaceId ws_id   = NO_WORKSPACE;
    WindowId    window  = NO_WINDOW;
};

// Per-monitor focus: remembers the last focused window per workspace.
struct MonitorFocusState {
    std::unordered_map<WorkspaceId, WindowId> last_window_per_ws;
};

class WorkspaceManager {
    private:
        WindowFactory window_factory_;
        std::vector<Workspace> workspaces;
        std::vector<Monitor>   monitors;
        // Authoritative owner: workspace id -> monitor index (-1 when unowned).
        std::vector<int> ws_owner;
        // Ordered workspace ids per monitor.
        std::vector<std::vector<int>> monitor_pools;
        // Active local workspace index per monitor (index into monitor_pools[i]).
        std::vector<int> monitor_active_local;
        // Pools parked from disconnected monitors by monitor name.
        std::unordered_map<std::string, std::vector<int>> parked_pools;
        // Active workspace id parked with monitor pool.
        std::unordered_map<std::string, int> parked_active_ws;
        // O(1) window lookup and ownership index.
        std::unordered_map<WindowId, std::shared_ptr<swm::Window>> window_index;
        std::unordered_map<WindowId, WorkspaceId> window_workspace;

        FocusState focus_; // cache — always derived from focused_monitor_ + monitor_focus_

        MonitorId focused_monitor_{ 0 };
        std::vector<MonitorFocusState> monitor_focus_;

        void ensure_monitor_focus_size();

        inline bool is_ws_valid(WorkspaceId id) const {
            return id >= 0 && id < (int)workspaces.size();
        }

        inline bool is_mon_valid(MonitorId id) const {
            return id >= 0 && id < (int)monitors.size();
        }

        void             sync_focus_state();
        MonitorId        monitor_index_by_name(const std::string& name) const;
        int index_of_ws_in_pool(const std::vector<int>& pool, WorkspaceId ws_id) const;
        MonitorId        monitor_of(WorkspaceId ws_id) const;
        int              local_index_of(MonitorId mon_idx, WorkspaceId ws_id) const;
        WorkspaceId      active_ws_of_monitor(MonitorId mon_idx) const;
        MonitorId        primary_monitor_index(const std::vector<MonitorAlias>& aliases,
            const MonitorCompose& compose) const;
        void             sync_monitors_active_ws();
        void             select_valid_focused_monitor();
        MonitorId        resolve_alias_monitor(const std::string& alias,
            const std::vector<MonitorAlias>& aliases) const;
        void             rebuild_pools_from_owner(const std::vector<std::vector<int>>& seed_pools,
            const std::vector<int>& preferred_active_ws);
        Workspace&       fallback_workspace();
        const Workspace& fallback_workspace() const;
        void             clear_window_indexes();
        void             index_window(WindowId win, const std::shared_ptr<swm::Window>& w, WorkspaceId ws_id);
        void             unindex_window(WindowId win);
        void             rebuild_window_indexes();
        bool             window_belongs_to_workspace(WindowId win, WorkspaceId ws_id) const;
        bool             window_is_unique_across_workspaces(WindowId win) const;
        bool             monitor_workspace_pools_have_no_duplicates() const;
        size_t           indexed_window_count() const;
        bool             indexes_consistent() const;

    public:
        WorkspaceManager() = default;

        // Set a custom window factory (backend subclass creation).
        // Must be called before any create_window() calls.
        void set_window_factory(WindowFactory factory) { window_factory_ = std::move(factory); }

        void init_from_defs(const std::vector<WorkspaceDef>& defs,
            const std::vector<MonitorAlias>& aliases,
            const MonitorCompose& compose = {});

        // Update workspace names/monitor aliases from new defs without
        // disturbing existing windows or assignments. Safe to call on reload.
        void update_workspace_defs(const std::vector<WorkspaceDef>& defs);

        void assign_workspaces(const std::vector<MonitorAlias>& aliases,
            const MonitorCompose& compose = {});

        void             set_monitors(std::vector<Monitor> mons);
        void             adjust_monitor_inset(MonitorId mon_idx, MonitorEdge edge, int delta);

        Workspace&       current();
        const Workspace& current() const;

        WorkspaceId workspace_of_window(WindowId win) const;

        bool             switch_to(WorkspaceId ws_id,
            const std::vector<MonitorAlias>& aliases = {},
            MonitorId monitor_hint                   = NO_MONITOR,
            const MonitorCompose& compose            = {});

        void                               add_window(std::shared_ptr<swm::Window> w, WorkspaceId ws_id = NO_WORKSPACE);
        std::shared_ptr<swm::Window>       create_window(WindowId win, WorkspaceId ws_id                = NO_WORKSPACE);
        void                               remove_window(WindowId win, WorkspaceId ws_id                = NO_WORKSPACE);

        std::shared_ptr<swm::Window>       find_window(WindowId win, WorkspaceId ws_id = NO_WORKSPACE);
        std::shared_ptr<const swm::Window> find_window(WindowId win, WorkspaceId ws_id = NO_WORKSPACE) const;
        std::shared_ptr<swm::Window>       find_window_in_all(WindowId win);
        std::shared_ptr<const swm::Window> find_window_in_all(WindowId win) const;

        void                               remove_window_from_all(WindowId win);
        void                               move_window_to(WorkspaceId ws_id, std::shared_ptr<swm::Window> w);

        bool                               focus_window(WindowId win);
        std::shared_ptr<swm::Window>       focus_next();
        std::shared_ptr<swm::Window>       focus_prev();
        bool                               zoom_focused();

        bool                               switch_local_index(MonitorId mon_idx, int local_idx);
        WorkspaceId workspace_for_local_index(MonitorId mon_idx, int local_idx) const;

        const std::vector<int>& monitor_workspace_ids(MonitorId mon_idx) const {
            static const std::vector<int> empty;
            if (mon_idx < 0 || mon_idx >= (int)monitor_pools.size())
                return empty;
            return monitor_pools[mon_idx];
        }

        WorkspaceId active_workspace(MonitorId mon_idx) const {
            return active_ws_of_monitor(mon_idx);
        }

        MonitorId monitor_of_workspace(WorkspaceId ws_id) const {
            return monitor_of(ws_id);
        }

        MonitorId        monitor_at_point(int x, int y) const;
        MonitorId        monitor_at_physical_point(int x, int y) const;
        bool             focus_monitor_at_point(int x, int y);

        Workspace&       workspace(WorkspaceId id);
        const Workspace& workspace(WorkspaceId id) const;

        MonitorId get_focused_monitor() const { return focused_monitor_; }
        const FocusState& get_focus_state() const { return focus_; }

        // Returns the last focused window on mon_idx/ws_id, or NO_WINDOW.
        // Validates that the window still exists before returning.
        WindowId last_focused_window(MonitorId mon_idx, WorkspaceId ws_id) const;

        // Explicitly set focused monitor (used by FocusMonitor command).
        void set_focused_monitor(MonitorId mon_idx);

        const MonitorState* monitor_state(MonitorId idx) const {
            if (!is_mon_valid(idx))
                return nullptr;
            return &monitors[(size_t)idx];
        }
        const WorkspaceState* workspace_state(WorkspaceId id) const {
            if (!is_ws_valid(id))
                return nullptr;
            return &workspaces[(size_t)id];
        }
        const std::vector<MonitorState>& all_monitor_states() const { return monitors; }
        const std::vector<WorkspaceState>& all_workspace_states() const { return workspaces; }
        const std::vector<Monitor>& get_monitors() const { return monitors; }
};
