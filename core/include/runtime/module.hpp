#pragma once

#include <cstdlib>
#include <string>
#include <runtime/event_receiver.hpp>
#include <support/log.hpp>
#include <runtime/runtime_state.hpp>

class Core;
class Runtime;
class RuntimeStore;
class LuaHost;
class Backend;

struct ModuleDeps {
    Runtime& runtime;
    Core&    core;
};

class Module : public IEventReceiver {
    public:
        explicit Module(ModuleDeps deps);
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

        // Event handlers are inherited from IEventReceiver.
        // Bring all base on() overloads into scope so partial overrides
        // in concrete modules don't hide the rest.
        using IEventReceiver::on;

    protected:
        Runtime&      runtime;
        Core&         core;
        Backend&      backend;
        RuntimeStore& store;
        LuaHost&      lua;

        // Returns the current FSM state of the owning Runtime.
        // Defined in module.cpp to avoid a circular include with runtime.hpp.
        RuntimeState runtime_state() const;

    private:
        friend class Runtime;

        bool       lua_inited_ = false;
};
