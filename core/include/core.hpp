#pragma once

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <optional>
#include <variant>

#include <backend/events.hpp>
#include <backend/commands.hpp>
#include <config_types.hpp>
#include <log.hpp>
#include <monitor.hpp>
#include <layout.hpp>
#include <ws.hpp>

using LayoutFn   = std::function<void (const std::vector<WindowId>&, const Monitor&, const layout::Config&,
    const layout::PlacementSink&)>;
using KeyHandler = std::function<void ()>;

struct Keybinding {
    uint16_t   mods;
    uint32_t   keysym;
    KeyHandler handler;
};

struct CoreSettings {
    std::vector<MonitorAlias> monitor_aliases;
    MonitorCompose            monitor_compose;
    std::vector<WorkspaceDef> workspace_defs;
    bool                      follow_moved_window = false;
    bool                      focus_new_window    = true;
    ThemeConfig               theme;
};

struct CoreReloadState {
    CoreSettings            settings;
    std::vector<Keybinding> keybindings;
    layout::Config          layout_cfg;
    std::string             active_layout;
};

using WindowStateRef    = std::shared_ptr<const WindowState>;
using WorkspaceStateRef = const WorkspaceState*;
using MonitorStateRef   = const MonitorState*;
using CoreDomainEvent   = std::variant<
    event::FocusChanged,
    event::WorkspaceSwitched,
    event::RaiseDocks,
    event::DisplayTopologyChanged,
    event::WindowAssignedToWorkspace,
    event::BorderlessActivated,
    event::BorderlessDeactivated
>;

enum class BackendEffectKind {
    MapWindow,
    UnmapWindow,
    FocusWindow,
    FocusRoot,
    UpdateWindow,
    WarpPointer,

};

struct BackendEffect {
    BackendEffectKind kind   = BackendEffectKind::UpdateWindow;
    WindowId          window = NO_WINDOW;
    int16_t           x      = 0;
    int16_t           y      = 0;
};

struct WindowFlush {
    WindowId window             = NO_WINDOW;
    bool     x_dirty            = false;
    bool     y_dirty            = false;
    bool     width_dirty        = false;
    bool     height_dirty       = false;
    bool     border_width_dirty = false;
    bool has_config_changes() const {
        return x_dirty || y_dirty || width_dirty || height_dirty || border_width_dirty;
    }
};

class Core {
    private:
        std::unordered_map<std::string, LayoutFn> layouts;
        std::vector<Keybinding> keybindings;

        std::string active_layout = "tile";
        layout::Config layout_cfg;

        WorkspaceManager wsman;
        std::vector<BackendEffect> pending_backend_effects;
        std::unordered_map<WindowId, WindowFlush> pending_window_flushes;
        std::vector<CoreDomainEvent> pending_core_events;
        CoreSettings settings;

        std::string config_path;
        bool runtime_started             = false;
        int monitor_top_inset_applied    = 0;
        int monitor_bottom_inset_applied = 0;

        void         emit_backend_effect(BackendEffectKind kind, WindowId window = NO_WINDOW);
        void         emit_warp_pointer(int16_t x, int16_t y);
        WindowFlush& ensure_window_flush(WindowId win);
        void         mark_window_x(WindowId win);
        void         mark_window_y(WindowId win);
        void         mark_window_width(WindowId win);
        void         mark_window_height(WindowId win);
        void         mark_window_border_width(WindowId win);
        void         sync_workspace_visibility();
        void         sync_current_focus();
        void         emit_focus_changed(WindowId window);
        void         emit_workspace_switched(int workspace_id);
        void         emit_raise_docks();
        void         emit_display_topology_changed();
        void         emit_window_assigned_to_workspace(WindowId window, int workspace_id);
        void         emit_borderless_activated(WindowId window, int monitor_index);
        void         emit_borderless_deactivated();

    public:
        Core() = default;

        void register_layout(const std::string& name, LayoutFn fn) {
            layouts[name] = std::move(fn);
        }

        void register_keybinding(uint16_t mods, uint32_t keysym, KeyHandler fn) {
            keybindings.push_back({ mods, keysym, std::move(fn) });
        }

        const std::vector<Keybinding>& get_keybindings() const {
            return keybindings;
        }

        void set_config_path(const std::string& path) { config_path = path; }
        const std::string& get_config_path() const { return config_path; }
        void apply_settings(CoreSettings next) {
            settings = std::move(next);
            wsman.update_workspace_defs(settings.workspace_defs);
        }
        const CoreSettings& current_settings() const { return settings; }
        void mark_runtime_started(bool started) { runtime_started = started; }
        int  monitor_top_inset()    const { return monitor_top_inset_applied; }
        int  monitor_bottom_inset() const { return monitor_bottom_inset_applied; }
        CoreReloadState snapshot_reload_state() const;
        void            restore_reload_state(const CoreReloadState& snapshot);
        void clear_reloadable_runtime_state() {
            keybindings.clear();
        }

        void init(std::vector<Monitor> initial_monitors = {});

        void arrange();

        const layout::Config& cfg() const { return layout_cfg; }

        using Command = command::CoreCommand;

        bool dispatch(const Command& cmd);
        bool dispatch(const command::FocusWindow& cmd);
        bool dispatch(const command::SwitchWorkspace& cmd);
        bool dispatch(const command::MoveWindowToWorkspace& cmd);
        bool dispatch(const command::MoveFocusedWindowToWorkspace& cmd);
        bool dispatch(const command::MapWindow& cmd);
        bool dispatch(const command::UnmapWindow& cmd);
        bool dispatch(const command::SetWindowFullscreen& cmd);
        bool dispatch(const command::EnsureWindow& cmd);
        bool dispatch(const command::AssignWindowWorkspace& cmd);
        bool dispatch(const command::SetWindowMetadata& cmd);
        bool dispatch(const command::SetWindowVisible& cmd);
        bool dispatch(const command::SetWindowHiddenByWorkspace& cmd);
        bool dispatch(const command::SetWindowSuppressFocusOnce& cmd);
        bool dispatch(const command::SetWindowFloating& cmd);
        bool dispatch(const command::SetWindowBorderless& cmd);
        bool dispatch(const command::ToggleWindowFloating& cmd);
        bool dispatch(const command::FocusNextWindow& cmd);
        bool dispatch(const command::FocusPrevWindow& cmd);
        bool dispatch(const command::FocusMonitor& cmd);
        bool dispatch(const command::MoveWindowToMonitor& cmd);
        bool dispatch(const command::ToggleFocusedWindowFloating& cmd);
        bool dispatch(const command::SwitchWorkspaceLocalIndex& cmd);
        bool dispatch(const command::HideWindow& cmd);
        bool dispatch(const command::ApplyMonitorTopology& cmd);
        bool dispatch(const command::ApplyMonitorTopInset& cmd);
        bool dispatch(const command::ApplyMonitorBottomInset& cmd);
        bool dispatch(const command::SetLayout& cmd);
        bool dispatch(const command::SetMasterFactor& cmd);
        bool dispatch(const command::AdjustMasterFactor& cmd);
        bool dispatch(const command::IncMaster& cmd);
        bool dispatch(const command::Zoom& cmd);
        bool dispatch(const command::ReconcileNow& cmd);
        bool dispatch(const command::RemoveWindowFromAllWorkspaces& cmd);
        bool dispatch(const command::SetWindowGeometry& cmd);
        bool dispatch(const command::SetWindowPosition& cmd);
        bool dispatch(const command::SetWindowSize& cmd);
        bool dispatch(const command::SetWindowBorderWidth& cmd);
        bool dispatch(const command::SyncWindowFromConfigureNotify& cmd);

        const std::vector<MonitorState>& monitor_states() const {
            return wsman.all_monitor_states();
        }
        MonitorStateRef monitor_state(int mon_idx) const {
            return wsman.monitor_state(mon_idx);
        }
        WorkspaceStateRef workspace_state(int ws_id) const {
            return wsman.workspace_state(ws_id);
        }
        const std::vector<WorkspaceState>& workspace_states() const {
            return wsman.all_workspace_states();
        }

        const std::vector<int>& monitor_workspace_ids(int mon_idx) const {
            return wsman.monitor_workspace_ids(mon_idx);
        }
        int active_workspace_on_monitor(int mon_idx) const {
            return wsman.active_workspace(mon_idx);
        }
        const FocusState& focus_state()       const { return wsman.get_focus_state(); }
        int focused_monitor_index()           const { return wsman.get_focused_monitor(); }
        bool focus_monitor_at_point(int x, int y) {
            return wsman.focus_monitor_at_point(x, y);
        }
        int workspace_count()         const { return (int)workspace_states().size(); }

        std::vector<WindowId> all_window_ids() const;

        WindowStateRef focused_window_state() const {
            WindowId w = wsman.get_focus_state().window;
            if (w == NO_WINDOW)
                return nullptr;
            return wsman.find_window_in_all(w);
        }

        WindowStateRef window_state(WindowId win) const {
            return wsman.find_window(win);
        }

        WindowStateRef window_state_any(WindowId win) const {
            return wsman.find_window_in_all(win);
        }

        std::vector<WindowId>      visible_window_ids() const;

        std::optional<WindowFlush> take_window_flush(WindowId win);

        std::vector<BackendEffect> take_backend_effects() {
            auto out = std::move(pending_backend_effects);
            pending_backend_effects.clear();
            return out;
        }

        std::vector<CoreDomainEvent> take_core_events() {
            auto out = std::move(pending_core_events);
            pending_core_events.clear();
            return out;
        }

        int workspace_of_window(WindowId win) const {
            return wsman.workspace_of_window(win);
        }

        int monitor_of_workspace(int ws_id) const {
            return wsman.monitor_of_workspace(ws_id);
        }

        bool is_workspace_visible(int ws_id) const {
            for (const auto& mon : monitor_states())
                if (mon.active_ws == ws_id) return true;
            return false;
        }

        bool monitor_has_visible_fullscreen(int mon_idx) const {
            auto mon = monitor_state(mon_idx);
            if (!mon)
                return false;

            auto ws = workspace_state(mon->active_ws);
            if (!ws)
                return false;

            for (auto& w : ws->windows) {
                if (!w)
                    continue;
                if (w->fullscreen && w->visible)
                    return true;
                // Self-managed: fullscreen_self_managed+borderless, fullscreen flag stays false.
                if (w->fullscreen_self_managed && w->borderless && w->visible)
                    return true;
            }
            return false;
        }

        bool monitor_has_visible_borderless(int mon_idx) const {
            auto mon = monitor_state(mon_idx);
            if (!mon)
                return false;

            auto ws = workspace_state(mon->active_ws);
            if (!ws)
                return false;

            for (auto& w : ws->windows) {
                if (!w)
                    continue;
                if (w->borderless && w->visible)
                    return true;
            }
            return false;
        }

        bool is_window_fullscreen(WindowId win) const {
            auto w = wsman.find_window_in_all(win);
            return w && w->fullscreen;
        }

        bool is_window_visible(WindowId win) const {
            auto w = wsman.find_window_in_all(win);
            return w && w->visible;
        }

        bool is_window_hidden_by_workspace(WindowId win) const {
            auto w = wsman.find_window_in_all(win);
            return w && w->hidden_by_workspace;
        }

        bool is_window_floating(WindowId win) const {
            auto w = wsman.find_window_in_all(win);
            return w && w->floating;
        }

        bool consume_window_suppress_focus_once(WindowId win);
        // Returns true if this UnmapNotify was WM-initiated (counter > 0) and consumes it.
        bool consume_wm_unmap(WindowId win);

        void update_window(WindowId win);

        // Read-only view for modules and scripts.
        const WorkspaceManager& ws() const { return wsman; }
};