#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <backend/backend_ports.hpp>
#include <backend/commands.hpp>
#include <backend/events.hpp>
#include <backend/render_port.hpp>
#include <runtime/event_receiver.hpp>
#include <runtime/hook_registry.hpp>
#include <domain/window.hpp>

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
    std::unordered_map<MonitorId, WorkspaceId> monitor_active_ws;
};

struct ExistingWindowSnapshot {
    WindowId window = NO_WINDOW;

    // Current native visibility state observed during startup scan.
    bool currently_viewable = false;

    // Backend-level default manage heuristic for fresh scan candidates.
    bool default_manage = true;

    // Restart restore metadata (if loaded from restart snapshot file).
    bool from_restart              = false;
    WorkspaceId restart_workspace_id = NO_WORKSPACE;
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

class Backend : public IEventReceiver, public IHookReceiver {
    public:
        ~Backend() override = default;

        // Backend as adapter:
        // - expose native event fd
        // - pump/coalesce native events and emit typed runtime events
        // - render/flush frame side effects
        virtual int  event_fd() const                             = 0;
        virtual void pump_events(std::size_t max_events_per_tick) = 0;
        virtual void render_frame()                               = 0;
        virtual void on_reload_applied()                          = 0;
        virtual void shutdown() {}
        // Called by main() just before execv() on exec-restart.
        // Backend should drop O_CLOEXEC on any fds that must survive across exec.
        virtual void prepare_exec_restart() {}
        virtual StartupSnapshot scan_existing_windows() { return {}; }

        // Called by Runtime once, after core.init() and before module on_start callbacks.
        virtual void on_start(Core&) {}

        // Domain-event reactions come from IEventReceiver — override what you need.

        // Capability ports exposed to Core and modules.
        virtual backend::BackendPorts ports() = 0;

        // Window factory — backend overrides to create its own subclass (e.g. X11Window).
        virtual std::shared_ptr<swm::Window> create_window(WindowId id) {
            auto w = std::make_shared<swm::Window>();
            w->id = id;
            return w;
        }
};
