#pragma once

#include <cstdlib>
#include <string>
#include <backend/events.hpp>
#include <log.hpp>
#include <runtime_state.hpp>

class Backend;
class Core;
class Runtime;
class Config;

struct ModuleDeps {
    Runtime& runtime;
    Config&  config;
    Core&    core;
};

class Module {
    public:
        explicit Module(ModuleDeps deps) : deps_(deps) {}
        virtual ~Module()                = default;
        virtual std::string name() const = 0;

        // Called during Runtime::use() — register layouts, Lua namespaces.
        // X connection is NOT yet ready. No I/O, no xcb calls here.
        virtual void on_init()   {}

        // Called each time the Lua VM is (re)initialized, before init.lua runs.
        // Re-register Lua namespaces so they survive hot-reload.
        // Guarded by lua_inited_ so it runs once per VM epoch even if
        // emit_lua_init() is called multiple times (e.g. via require()).
        void lua_init_once() {
            if (lua_inited_) return;
            lua_inited_ = true;
            on_lua_init();
        }
        void lua_init_reset() { lua_inited_ = false; }
        virtual void on_lua_init() {}

        // Called after all modules have been on_init()'d and X is ready.
        // Grab keys, create windows, open sockets, query monitors.
        virtual void on_start()  {}

        // Called when the WM is shutting down.
        // Release resources: ungrab keys, destroy windows, close sockets.
        // is_exec_restart=true means the process is about to execv() itself —
        // child processes that should survive (e.g. policy=once) must not be killed.
        virtual void on_stop(bool /*is_exec_restart*/ = false) {}

        // Called after init.lua has been re-executed (hot-reload).
        virtual void on_reload() {}

        // C++ doesn't support virtual template methods, so we use one overloaded
        // name (`on`) with distinct event types.

        virtual void on(event::WindowMapped) {}
        virtual void on(event::WindowUnmapped) {}
        virtual void on(event::FocusChanged) {}
        virtual void on(event::WorkspaceSwitched) {}
        virtual void on(event::ExposeWindow) {}
        virtual void on(event::RaiseDocks) {}
        virtual void on(event::DisplayTopologyChanged) {}
        virtual void on(event::RuntimeStarted) {}
        virtual void on(event::ButtonEv) {}
        virtual void on(event::MotionEv) {}
        virtual void on(event::KeyPressEv) {}
        virtual void on(event::ManageWindowQuery&) {}
        virtual void on(event::ApplyWindowRules) {}
        virtual void on(event::DestroyNotify) {}
        virtual void on(event::ConfigureNotify) {}
        virtual void on(event::PropertyNotify) {}
        virtual void on(event::WindowAssignedToWorkspace) {}
        virtual void on(event::TrayIconDocked) {}
        virtual void on(event::KeyboardLayoutChanged) {}
        virtual void on(event::BorderlessActivated) {}
        virtual void on(event::BorderlessDeactivated) {}

        // Returns true if the event was handled (stops further dispatch).
        virtual bool on(event::ClientMessageEv) { return false; }
        virtual bool on(event::CloseWindowRequest) { return false; }

    protected:
        Core&    core()    { return deps_.core; }
        const Core& core() const { return deps_.core; }
        Runtime& runtime() { return deps_.runtime; }
        const Runtime& runtime() const { return deps_.runtime; }
        Config& config() { return deps_.config; }
        const Config& config() const { return deps_.config; }
        Backend& backend() {
            if (!backend_) {
                LOG_ERR("backend() called before on_start() (runtime state: %s)",
                    runtime_state_name(runtime_state()));
                std::abort();
            }
            return *backend_;
        }
        const Backend& backend() const {
            if (!backend_) {
                LOG_ERR("backend() called before on_start() (runtime state: %s)",
                    runtime_state_name(runtime_state()));
                std::abort();
            }
            return *backend_;
        }

        // Returns the current FSM state of the owning Runtime.
        // Defined in module.cpp to avoid a circular include with runtime.hpp.
        RuntimeState runtime_state() const;

    private:
        friend class Runtime;

        // Called by Runtime::start() before on_start(). Not accessible externally.
        void bind_backend(Backend& b) { backend_ = &b; }

        ModuleDeps deps_;
        Backend*   backend_    = nullptr;
        bool       lua_inited_ = false;
};
