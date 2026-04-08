#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <backend/commands.hpp>
#include <backend/events.hpp>
#include <backend/render_port.hpp>
#include <backend/tray_host.hpp>
#include <window.hpp>

class Core;
class Runtime;

namespace backend {
class InputPort;
class MonitorPort;
class KeyboardPort;
class GLPort;
} // namespace backend

struct StartupSnapshot {
    std::vector<struct ExistingWindowSnapshot> windows;
    // monitor_idx -> active_ws_id from exec-restart state file; empty on first start.
    std::unordered_map<int, int>               monitor_active_ws;
};

struct ExistingWindowSnapshot {
    WindowId window = NO_WINDOW;

    // Current native visibility state observed during startup scan.
    bool currently_viewable = false;

    // Backend-level default manage heuristic for fresh scan candidates.
    bool default_manage = true;

    // Restart restore metadata (if loaded from restart snapshot file).
    bool from_restart              = false;
    int  restart_workspace_id      = -1;
    bool restart_floating          = false;
    bool restart_fullscreen        = false;
    bool restart_hidden_explicitly = false;
    bool restart_borderless        = false;

    // Actual X geometry at scan time — used to seed WindowState so floating
    // windows have correct coordinates before the first ConfigureNotify arrives.
    bool  has_geometry = false;
    Vec2i geo_pos;
    Vec2i geo_size;

    // Metadata snapshot used by rules/policy.
    std::string          wm_instance;
    std::string          wm_class;
    WindowType           type = WindowType::Normal;
    command::WindowHints hints;
};

class Backend {
    public:
        virtual ~Backend() = default;

        // Backend as adapter:
        // - expose native event fd
        // - pump/coalesce native events and emit typed runtime events
        // - render/flush frame side effects
        virtual int  event_fd() const                             = 0;
        virtual void pump_events(std::size_t max_events_per_tick) = 0;
        virtual void render_frame()                               = 0;
        virtual void on_reload_applied()                          = 0;
        virtual void shutdown() {}
        virtual StartupSnapshot scan_existing_windows() { return {}; }

        // Called by Runtime once, after core.init() and before module on_start callbacks.
        virtual void on_start(Core&) {}

        // Called by Runtime for each core-emitted domain event, after module dispatch.
        virtual void on(event::WorkspaceSwitched) {}
        virtual void on(event::WindowAssignedToWorkspace) {}
        virtual void on(event::FocusChanged) {}
        virtual void on(event::RaiseDocks) {}
        virtual void on(event::DisplayTopologyChanged) {}
        virtual void on(event::WindowAdopted) {}
        virtual void on(event::BorderlessActivated) {}
        virtual void on(event::BorderlessDeactivated) {}

        // Close a window using platform-specific protocol (e.g. WM_DELETE_WINDOW / kill).
        virtual bool close_window(WindowId) { return false; }

        // Optional capabilities for UI modules.
        virtual backend::InputPort*    input_port()    { return nullptr; }
        virtual backend::MonitorPort*  monitor_port()  { return nullptr; }
        virtual backend::RenderPort*   render_port()   { return nullptr; }
        virtual backend::KeyboardPort* keyboard_port() { return nullptr; }
        virtual backend::GLPort*       gl_port()       { return nullptr; }
        virtual std::unique_ptr<backend::TrayHost>
        create_tray_host(WindowId, int, int, int, bool) { return nullptr; }
        virtual std::string window_title(WindowId) const { return {}; }
        virtual uint32_t window_pid(WindowId) const { return 0; }

        // Window factory — backend overrides to create its own subclass (e.g. X11Window).
        virtual std::shared_ptr<swm::Window> create_window(WindowId id) {
            auto w = std::make_shared<swm::Window>();
            w->id = id;
            return w;
        }
};
