#pragma once

#include <string>
#include <backend/events.hpp>

class Core;
class Runtime;
class Config;

struct ModuleDeps {
    Runtime& runtime;
    Config&  config;
};

class Module {
    public:
        explicit Module(ModuleDeps deps) : deps_(deps) {}
        virtual ~Module()                = default;
        virtual std::string name() const = 0;

        // Called during Runtime::use() — register layouts, Lua namespaces.
        // X connection is NOT yet ready. No I/O, no xcb calls here.
        virtual void on_init(Core&)   {}

        // Called each time the Lua VM is (re)initialized, before init.lua runs.
        // Re-register Lua namespaces so they survive hot-reload.
        virtual void on_lua_init(Core&) {}

        // Called after all modules have been on_init()'d and X is ready.
        // Grab keys, create windows, open sockets, query monitors.
        virtual void on_start(Core&)  {}

        // Called when the WM is shutting down.
        // Release resources: ungrab keys, destroy windows, close sockets.
        // is_exec_restart=true means the process is about to execv() itself —
        // child processes that should survive (e.g. policy=once) must not be killed.
        virtual void on_stop(Core&, bool /*is_exec_restart*/ = false) {}

        // Called after init.lua has been re-executed (hot-reload).
        virtual void on_reload(Core&) {}

        // C++ doesn't support virtual template methods, so we use one overloaded
        // name (`on`) with distinct event types.

        virtual void on(Core&, event::WindowMapped) {}
        virtual void on(Core&, event::WindowUnmapped) {}
        virtual void on(Core&, event::FocusChanged) {}
        virtual void on(Core&, event::WorkspaceSwitched) {}
        virtual void on(Core&, event::ExposeWindow) {}
        virtual void on(Core&, event::RaiseDocks) {}
        virtual void on(Core&, event::DisplayTopologyChanged) {}
        virtual void on(Core&, event::ButtonEv) {}
        virtual void on(Core&, event::MotionEv) {}
        virtual void on(Core&, event::KeyPressEv) {}
        virtual void on(Core&, event::ManageWindowQuery&) {}
        virtual void on(Core&, event::ApplyWindowRules) {}
        virtual void on(Core&, event::DestroyNotify) {}
        virtual void on(Core&, event::ConfigureNotify) {}
        virtual void on(Core&, event::PropertyNotify) {}
        virtual void on(Core&, event::WindowAssignedToWorkspace) {}
        virtual void on(Core&, event::TrayIconDocked) {}
        virtual void on(Core&, event::KeyboardLayoutChanged) {}
        virtual void on(Core&, event::BorderlessActivated) {}
        virtual void on(Core&, event::BorderlessDeactivated) {}

        // Returns true if the event was handled (stops further dispatch).
        virtual bool on(Core&, event::ClientMessageEv) { return false; }
        virtual bool on(Core&, event::CloseWindowRequest) { return false; }

    protected:
        Runtime& runtime() { return deps_.runtime; }
        const Runtime& runtime() const { return deps_.runtime; }
        Config& config() { return deps_.config; }
        const Config& config() const { return deps_.config; }

    private:
        ModuleDeps deps_;
};