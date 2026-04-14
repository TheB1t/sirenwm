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
#include <event_queue.hpp>
#include <log.hpp>
#include <monitor.hpp>
#include <layout.hpp>
#include <lua_host.hpp>
#include <ws.hpp>

using LayoutFn = std::function<void (const std::vector<WindowId>&, const Monitor&, const layout::Config&,
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
    ThemeConfig               theme;
};

struct CoreReloadState {
    CoreSettings            settings;
    std::vector<Keybinding> keybindings;
    layout::Config          layout_cfg;
    std::string             active_layout;
};

using WindowRef         = std::shared_ptr<const swm::Window>;
using WorkspaceStateRef = const WorkspaceState*;
using MonitorStateRef   = const MonitorState*;

enum class BackendEffectKind {
    MapWindow,
    UnmapWindow,
    FocusWindow,
    FocusRoot,
    UpdateWindow,
    WarpPointer,
    RaiseWindow,           // raise window to top of X stacking order
    LowerWindow,           // lower window to bottom of X stacking order
    CloseWindow,           // request client to close (WM_DELETE_WINDOW / kill)
};

struct BackendEffect {
    BackendEffectKind kind   = BackendEffectKind::UpdateWindow;
    WindowId          window = NO_WINDOW;
    Vec2i16           pos;
};

struct FullscreenLikeDecision {
    bool  promote = false;
    Vec2i pos;
    Vec2i size;
};

struct WindowFlush {
    enum Dirty : uint8_t {
        X           = 1 << 0,
        Y           = 1 << 1,
        Width       = 1 << 2,
        Height      = 1 << 3,
        BorderWidth = 1 << 4,
        Geometry    = X | Y | Width | Height,
        Position    = X | Y,
        Size        = Width | Height,
        All         = Geometry | BorderWidth,
    };

    WindowId window = NO_WINDOW;
    uint8_t  dirty  = 0;

    bool has_config_changes() const { return dirty != 0; }
};

class Core {
    private:
        std::unordered_map<std::string, LayoutFn>       layouts;
        std::unordered_map<std::string, LuaRegistryRef> lua_layouts;
        std::vector<Keybinding> keybindings;

        std::string    active_layout = "tile";
        layout::Config layout_cfg;

        // Non-null only while a Lua layout callback is being invoked from arrange().
        // Used by siren.layout.place() to place windows without passing Core through Lua.
        layout::PlacementSink* active_lua_sink_ = nullptr;

        // RAII guard — sets active_lua_sink_ for the duration of a Lua layout call
        // and clears it on destruction, even if an exception is thrown.
        struct LuaSinkGuard {
            Core* core_;
            explicit LuaSinkGuard(Core* c, layout::PlacementSink* sink)
                : core_(c) { core_->active_lua_sink_ = sink; }
            ~LuaSinkGuard() { core_->active_lua_sink_ = nullptr; }
            LuaSinkGuard(const LuaSinkGuard&)            = delete;
            LuaSinkGuard& operator=(const LuaSinkGuard&) = delete;
        };

        WorkspaceManager wsman;
        std::vector<BackendEffect> pending_backend_effects;
        std::unordered_map<WindowId, WindowFlush> pending_window_flushes;
        IEventSink*  event_sink_ = &null_event_sink();
        CoreSettings settings;

        std::string config_path;
        LuaHost*    lua_host_       = nullptr;
        bool        runtime_started = false;

        void         emit_backend_effect(BackendEffectKind kind, WindowId window = NO_WINDOW);
        void         emit_warp_pointer(Vec2i16 pos);
        WindowFlush& ensure_window_flush(WindowId win);
        void         mark_window_dirty(WindowId win, uint8_t bits);
        void         sync_workspace_visibility();
        void         sync_current_focus();
        void         reconcile(); // sync_workspace_visibility + arrange + sync_current_focus

        // Fire-and-forget event onto the unified queue. Use directly at
        // call sites — no per-event wrapper methods needed.
        template<typename E>
        void post(E ev) { event_sink_->post_event(std::move(ev)); }

        void evaluate_workspace_fullscreen(int ws_id);
        void pin_fullscreen_to_monitor(swm::Window& w, int ws_id);

    public:
        Core() = default;

        // Post a FocusChanged event through the unified event queue. Use this
        // instead of a direct backend call so the event is ordered after any
        // older queued events — prevents stale focus from overwriting state.
        void emit_focus_changed(WindowId window);

        void register_layout(const std::string& name, LayoutFn fn) {
            layouts[name] = std::move(fn);
        }

        void register_lua_layout(const std::string& name, LuaRegistryRef ref) {
            lua_layouts[name] = std::move(ref);
        }

        // Called by siren.layout.place() during a Lua layout callback.
        bool lua_place_window(WindowId id, Vec2i pos, Vec2i size, uint32_t border) {
            if (!active_lua_sink_) return false;
            (*active_lua_sink_)(id, pos, size, border);
            return true;
        }

        void register_keybinding(uint16_t mods, uint32_t keysym, KeyHandler fn) {
            keybindings.push_back({ mods, keysym, std::move(fn) });
        }

        const std::vector<Keybinding>& get_keybindings() const {
            return keybindings;
        }

        void set_window_factory(WindowFactory factory) { wsman.set_window_factory(std::move(factory)); }
        void bind_lua_host(LuaHost& host) { lua_host_ = &host; }
        LuaHost& lua_host() const {
            if (!lua_host_) {
                LOG_ERR("lua_host() called before bind_lua_host()");
                std::abort();
            }
            return *lua_host_;
        }
        bool has_lua_host() const { return lua_host_ != nullptr; }

        void set_config_path(const std::string& path) { config_path = path; }
        const std::string& get_config_path() const { return config_path; }
        void apply_settings(CoreSettings next) {
            settings = std::move(next);
            wsman.update_workspace_defs(settings.workspace_defs);
            // Re-assign workspaces to monitors so newly added workspaces get a monitor
            // and deleted ones are cleaned up without waiting for a display change event.
            if (runtime_started)
                wsman.assign_workspaces(settings.monitor_aliases, settings.monitor_compose);
        }
        const CoreSettings& current_settings() const { return settings; }
        void mark_runtime_started(bool started) { runtime_started = started; }
        // Per-monitor reserved area at the given edge; max across monitors
        // when no index is given.
        int monitor_inset(MonitorEdge edge, int mon_idx = -1) const {
            const auto& mons = wsman.all_monitor_states();
            if (mon_idx >= 0 && mon_idx < (int)mons.size())
                return mons[mon_idx].inset(edge);
            int m = 0;
            for (const auto& mon : mons) m = std::max(m, mon.inset(edge));
            return m;
        }
        CoreReloadState snapshot_reload_state() const;
        void            restore_reload_state(const CoreReloadState& snapshot);
        void clear_reloadable_runtime_state() {
            keybindings.clear();
            lua_layouts.clear();
        }

        void init(std::vector<Monitor> initial_monitors = {});

        void arrange();

        const layout::Config& cfg() const { return layout_cfg; }

        // Two layered reducer entry points. Callers pick their layer
        // explicitly — there is no umbrella "Command" type.
        bool dispatch(const command::CommandAtom& cmd);
        bool dispatch(const command::CommandComposite& cmd);

        // Per-type atom overloads — direct primitive operations on the model.
        bool dispatch(const command::atom::FocusWindow& cmd);
        bool dispatch(const command::atom::SwitchWorkspace& cmd);
        bool dispatch(const command::atom::MoveWindowToWorkspace& cmd);
        bool dispatch(const command::atom::MapWindow& cmd);
        bool dispatch(const command::atom::UnmapWindow& cmd);
        bool dispatch(const command::atom::SetWindowFullscreen& cmd);
        bool dispatch(const command::atom::EnsureWindow& cmd);
        bool dispatch(const command::atom::AssignWindowWorkspace& cmd);
        bool dispatch(const command::atom::SetWindowMetadata& cmd);
        bool dispatch(const command::atom::SetWindowMapped& cmd);
        bool dispatch(const command::atom::SetWindowHiddenByWorkspace& cmd);
        bool dispatch(const command::atom::SetWindowSuppressFocusOnce& cmd);
        bool dispatch(const command::atom::SetWindowFloating& cmd);
        bool dispatch(const command::atom::SetWindowBorderless& cmd);
        bool dispatch(const command::atom::HideWindow& cmd);
        bool dispatch(const command::atom::ApplyMonitorTopology& cmd);
        bool dispatch(const command::atom::ReserveMonitorArea& cmd);
        bool dispatch(const command::atom::SetLayout& cmd);
        bool dispatch(const command::atom::SetMasterFactor& cmd);
        bool dispatch(const command::atom::FocusMonitor& cmd);
        bool dispatch(const command::atom::MoveWindowToMonitor& cmd);
        bool dispatch(const command::atom::ReconcileNow& cmd);
        bool dispatch(const command::atom::RemoveWindowFromAllWorkspaces& cmd);
        bool dispatch(const command::atom::SetWindowGeometry& cmd);
        bool dispatch(const command::atom::SetWindowPosition& cmd);
        bool dispatch(const command::atom::SetWindowSize& cmd);
        bool dispatch(const command::atom::SetWindowBorderWidth& cmd);
        bool dispatch(const command::atom::SyncWindowFromConfigureNotify& cmd);
        bool dispatch(const command::atom::CloseWindow& cmd);

        // Per-type composite overloads — sugar scenarios that read state and
        // then emit one or more atoms under the hood.
        bool dispatch(const command::composite::ToggleWindowFloating& cmd);
        bool dispatch(const command::composite::MoveFocusedWindowToWorkspace& cmd);
        bool dispatch(const command::composite::FocusNextWindow& cmd);
        bool dispatch(const command::composite::FocusPrevWindow& cmd);
        bool dispatch(const command::composite::ToggleFocusedWindowFloating& cmd);
        bool dispatch(const command::composite::SwitchWorkspaceLocalIndex& cmd);
        bool dispatch(const command::composite::AdjustMasterFactor& cmd);
        bool dispatch(const command::composite::IncMaster& cmd);
        bool dispatch(const command::composite::Zoom& cmd);

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
        bool focus_monitor_at_point(int x, int y);
        int active_workspace_at_point(int x, int y) const {
            // Callers pass raw physical coordinates (WM_NORMAL_HINTS.PPosition from
            // Wine/Proton, pointer root coords, etc). Hit-test against the physical
            // monitor rect so points inside the bar strip still resolve.
            int mon = wsman.monitor_at_physical_point(x, y);
            return (mon >= 0) ? wsman.active_workspace(mon) : -1;
        }
        int workspace_count()         const { return (int)workspace_states().size(); }

        std::vector<WindowId> all_window_ids() const;

        WindowRef focused_window_state() const {
            int      mon = wsman.get_focused_monitor();
            int      ws  = wsman.active_workspace(mon);
            WindowId w   = wsman.last_focused_window(mon, ws);
            if (w == NO_WINDOW) {
                // Fall back to workspace cursor (covers fresh windows not yet recorded).
                w = wsman.get_focus_state().window;
            }
            if (w == NO_WINDOW)
                return nullptr;
            return wsman.find_window_in_all(w);
        }

        WindowRef window_state(WindowId win) const {
            return wsman.find_window(win);
        }

        WindowRef window_state_any(WindowId win) const {
            return wsman.find_window_in_all(win);
        }

        // Mutable access for backend — returns the live Window object.
        std::shared_ptr<swm::Window> window_mut(WindowId win) {
            return wsman.find_window_in_all(win);
        }

        std::vector<WindowId>      visible_window_ids() const;

        std::optional<WindowFlush> take_window_flush(WindowId win);

        std::vector<BackendEffect> take_backend_effects() {
            auto out = std::move(pending_backend_effects);
            pending_backend_effects.clear();
            return out;
        }

        // Install the sink Core pushes domain events to. Runtime wires this
        // at start() and restores the null sink at stop(). Pre-init calls
        // emit into the null sink and are silently dropped — this is
        // intentional for the tiny window between construction and
        // Runtime::start().
        void set_event_sink(IEventSink* sink) {
            event_sink_ = sink ? sink : &null_event_sink();
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

        bool monitor_has_visible_covering_window(int mon_idx) const {
            auto mon = monitor_state(mon_idx);
            if (!mon)
                return false;

            auto ws = workspace_state(mon->active_ws);
            if (!ws)
                return false;

            for (auto& w : ws->windows) {
                if (!w || !w->is_visible())
                    continue;
                if (w->fullscreen || w->borderless)
                    return true;
            }
            return false;
        }

        FullscreenLikeDecision evaluate_fullscreen_like_request(WindowId win,
            Vec2i requested_size, bool has_size) const;

        bool consume_window_suppress_focus_once(WindowId win);

        void update_window(WindowId win);

        // Read-only view for modules and scripts.
        const WorkspaceManager& ws() const { return wsman; }
};
