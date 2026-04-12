#pragma once

#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>
#include <atomic>
#include <sys/epoll.h>

#include <backend/backend_ports.hpp>
#include <backend/events.hpp>
#include <backend/hooks.hpp>
#include <core.hpp>
#include <core_config.hpp>
#include <event_emitter.hpp>
#include <event_queue.hpp>
#include <hook_registry.hpp>
#include <lua_host.hpp>
#include <module.hpp>
#include <runtime_store.hpp>
#include <surface.hpp>

#include <runtime_state.hpp>

#include <unordered_map>
#include <unordered_set>

namespace backend {
class TrayHost;
} // namespace backend

class Backend;
class ModuleRegistry;
struct TestHarness;

class Runtime : public IEventEmitter, public IEventSink {
    private:
        // Test harness needs direct drain_events() access to assert synchronous
        // module reactions. Production code must never drain outside tick().
        friend struct ::TestHarness;

        enum class ReloadRequest {
            None,
            ReloadConfig,
            SoftRestart,
        };

        ModuleRegistry& module_registry_;
        Core         core_;
        LuaHost      lua_host_{core_}; // must be declared after core_
        RuntimeStore store_;
        CoreConfig   core_config_;
        Backend*     backend_ = nullptr; // non-null between start() and stop()

        std::vector<std::unique_ptr<Module>> modules;

        int backend_extension_event_base = -1;

        // SIGCHLD self-pipe: child exit writes a byte, event loop reaps and
        // emits "child_exit" Lua events.
        int sigchld_pipe_rd_ = -1;
        int sigchld_pipe_wr_ = -1;

        struct WatchedFd {
            int                   fd;
            std::function<void()> cb;
        };
        std::vector<WatchedFd> watched_fds;
        int epoll_fd_ = -1;

        RuntimeState state_ = RuntimeState::Idle;

        std::atomic_bool reload_pending { false };
        std::atomic_bool soft_restart_pending { false };
        std::atomic_bool exec_restart_pending { false };
        std::atomic_bool stop_requested { false };

    public:
        explicit Runtime(ModuleRegistry& module_registry);

        // Non-copyable, non-movable (address stability for references).
        Runtime(const Runtime&)            = delete;
        Runtime& operator=(const Runtime&) = delete;
        Runtime(Runtime&&)                 = delete;
        Runtime& operator=(Runtime&&)      = delete;
        virtual ~Runtime()                 = default;

        Core&       core()       { return core_; }
        const Core& core() const { return core_; }

        template<typename T, typename... Args>
        Runtime& use(Args&&... args) {
            auto mod = std::make_unique<T>(ModuleDeps{ *this, core_ },
                    std::forward<Args>(args)...);
            mod->on_init();
            modules.push_back(std::move(mod));
            return *this;
        }

        Runtime& use(const std::string& name);

        template<typename T>
        T* get_module() {
            for (auto& mod : modules)
                if (auto* p = dynamic_cast<T*>(mod.get()))
                    return p;
            return nullptr;
        }

        Module* get_module_by_name(const std::string& name);

        void    emit_lua_init();
        void    reset_lua_init();

        // Drive the full lifecycle: Idle→Configured→Starting→Running→Stopping→Stopped.
        // If config_path fails to load, tries fallback_config_path (if non-empty).
        // Returns when the WM shuts down. Call consume_exec_restart_request() after.
        void run(const std::string& config_path,
            const std::string& fallback_config_path = {});

        void stop(bool is_exec_restart = false);
        void request_reload();
        void request_soft_restart();
        void request_exec_restart();
        bool consume_exec_restart_request();
        void    request_stop() { stop_requested = true; }

        RuntimeState state() const { return state_; }

        // For test harnesses that set up backend/core manually without load_config().
        void mark_configured() {
            if (state_ == RuntimeState::Idle)
                state_ = RuntimeState::Configured;
        }
        // Used by test harnesses directly; prefer run() in production.
        void           start();
        bool           load_config(const std::string& path);
        backend::BackendPorts ports();
        void           prepare_exec_restart();
        ModuleRegistry& module_registry() { return module_registry_; }
        const ModuleRegistry& module_registry() const { return module_registry_; }

        // IEventEmitter
        void add_receiver(IEventReceiver* receiver) override;
        void remove_receiver(IEventReceiver* receiver) override;

        // Hook registry — synchronous filter dispatch.
        void add_hook_receiver(IHookReceiver* receiver) { hook_registry_.add(receiver); }
        void remove_hook_receiver(IHookReceiver* receiver) { hook_registry_.remove(receiver); }

        // Synchronous hook dispatch. Two overloads:
        //  - lvalue: caller owns the hook struct and reads out-fields in place.
        //    Use when the same hook is invoked multiple times or inspected
        //    after the call via the original variable.
        //  - rvalue: fire-and-forget shorthand. Returns the mutated hook by
        //    value so out-fields can be read inline:
        //      if (!runtime.invoke_hook(hook::ShouldManageWindow{win}).manage)
        //          return;
        template<typename H>
        void invoke_hook(H& h) {
            hook_registry_.invoke(h);
        }

        template<typename H>
        H invoke_hook(H&& h) {
            hook_registry_.invoke(h);
            return std::move(h);
        }

        LuaHost& lua() { return lua_host_; }
        const LuaHost& lua() const { return lua_host_; }

        RuntimeStore& store() { return store_; }
        const RuntimeStore& store() const { return store_; }

        CoreConfig&       core_config()       { return core_config_; }
        const CoreConfig& core_config() const { return core_config_; }

        CoreSettings             build_core_settings() const;
        std::vector<std::string> validate_settings() const;

        // Fire-and-forget event: push onto the queue, delivered on next
        // drain_events() pass. Inherited post_event<Ev>() from IEventSink
        // wraps the event in TypedEvent<Ev> and calls post_queued().
        using IEventSink::post_event;

        // Resolve a backend WindowId to a Surface registered with this
        // Runtime. Used by backends to decide whether an Expose/Button event
        // should be posted as the window-scoped variant or the surface-scoped
        // variant. Returns nullptr if the window is not a surface.
        Surface* resolve_surface(WindowId win);

        // Stoppable query: synchronous, returns as soon as any module reports
        // it handled the event. Used only for event::ClientMessageEv where
        // the X11 EWMH handler needs a consumed/ignored signal back. Other
        // synchronous filter cases should use invoke_hook() instead.
        template<typename Ev>
        bool emit_until_handled(Ev ev) {
            for (auto& m : modules)
                if (m->on(ev))
                    return true;
            return false;
        }

        void set_backend_extension_event_base(int base) { backend_extension_event_base = base; }
        int get_backend_extension_event_base() const { return backend_extension_event_base; }
        void dispatch_display_change();

        void watch_fd(int fd, std::function<void()> cb);
        void unwatch_fd(int fd);
        void dispatch_ready_fds(struct epoll_event* events, int count);

        // Bind a concrete backend. Called by RuntimeOf<B> constructor and test harnesses.
        void bind_backend(Backend& b) { backend_ = &b; }

        // Module-facing UI resource factories.
        // Surface owns its backend window; destroying the Surface unregisters
        // and releases the window. create_tray wires an X11 TrayHost against
        // a previously-created Surface — returns nullptr on backends without
        // TrayHostPort (e.g. Wayland).
        std::unique_ptr<Surface>           create_surface(const SurfaceCreateInfo& info);
        std::unique_ptr<backend::TrayHost> create_tray(Surface& owner, bool own_selection);

    protected:
        // IEventSink: used by Core, backend, and modules to push events
        // onto the unified queue. Runtime is the sole owner of the queue
        // and the sole drain chokepoint.
        void post_queued(std::unique_ptr<QueuedEvent> ev) override {
            event_queue_.push(std::move(ev));
        }

    private:
        friend class Surface;
        void unregister_surface(Surface* s);

        std::unordered_set<Surface*>           surface_registry_;
        std::unordered_map<WindowId, Surface*> surface_by_id_;

        EventQueue                             event_queue_;
        std::vector<IEventReceiver*>           extra_receivers_;
        HookRegistry                           hook_registry_;

        Backend&       backend();
        const Backend& backend() const;

        // Single drain chokepoint. Called once per tick between epoll wait
        // and render_frame, and synchronously after one-shot state transitions
        // that must flush events (start, stop, reload). Private: production
        // code outside Runtime must not drain directly — post events and let
        // the tick drain them. TestHarness is the only exception.
        void          drain_events();

        void          reload();
        void          tick();                    // one event-loop iteration (Running state)
        void          run_loop();                // epoll setup + tick() loop
        bool          process_pending_reload();  // consumed inside tick()
        void          transition(RuntimeState from, RuntimeState to);
        void          apply_and_refresh_monitors();
        void          setup_sigchld_pipe();
        void          teardown_sigchld_pipe();
        void          reap_children();
        void          save_restart_state();
        ReloadRequest consume_reload_request();
        bool          reload_runtime_config();
};

// ---------------------------------------------------------------------------
// RuntimeOf<B> — typed wrapper that owns the concrete backend as a value.
// All non-template code works with Runtime& and never sees B.
// ---------------------------------------------------------------------------
template<typename B, typename... BArgs>
class RuntimeOf : public Runtime {
    B backend_impl_;
    public:
        explicit RuntimeOf(ModuleRegistry& module_registry, BArgs&&... args)
            : Runtime(module_registry)
              , backend_impl_(core(), static_cast<Runtime&>(*this),
                  std::forward<BArgs>(args)...)
        {
            bind_backend(backend_impl_);
        }
};
