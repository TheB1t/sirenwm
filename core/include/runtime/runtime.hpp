#pragma once

#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>
#include <atomic>

#include <backend/backend_ports.hpp>
#include <backend/events.hpp>
#include <backend/hooks.hpp>
#include <backend/render_port.hpp>
#include <domain/core.hpp>
#include <config/core_config.hpp>
#include <runtime/event_emitter.hpp>
#include <runtime/event_loop.hpp>
#include <runtime/event_queue.hpp>
#include <runtime/hook_registry.hpp>
#include <runtime/pointer_registry.hpp>
#include <lua/lua_host.hpp>
#include <runtime/module.hpp>
#include <runtime/module_registry.hpp>
#include <runtime/runtime_store.hpp>

#include <runtime/runtime_state.hpp>

#include <unordered_map>

namespace backend {
class TrayHost;
} // namespace backend

class Backend;
struct TestHarness;

struct ModuleWindowCreateInfo {
    int   monitor_index = -1;     // advisory; backend places by pos
    Vec2i pos;                    // root-space coordinates
    Vec2i size                = { 1, 1 };
    bool  want_expose         = false;
    bool  want_button_press   = false;
    bool  want_button_release = false;
    bool  dock                = false;  // semantic: dock/bar window
    bool  keep_above          = false;  // semantic: keep above other windows
};

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

        ModuleRegistry module_registry_;
        Core         core_;
        LuaHost      lua_host_{core_}; // must be declared after core_
        RuntimeStore store_;
        CoreConfig   core_config_;
        std::unique_ptr<Backend> backend_;

        int backend_extension_event_base = -1;

        // SIGCHLD self-pipe: child exit writes a byte, event loop reaps and
        // emits "child_exit" Lua events.
        int sigchld_pipe_rd_ = -1;
        int sigchld_pipe_wr_ = -1;

        EventLoop event_loop_;

    public:
        // Direct references to mutable runtime subsystems.
        // Prefer field access over trivial getter wrappers.
        ModuleRegistry& module_registry = module_registry_;
        Core&           core            = core_;
        LuaHost&        lua             = lua_host_;
        RuntimeStore&   store           = store_;
        CoreConfig&     core_config     = core_config_;
        EventLoop&      event_loop      = event_loop_;

    private:
        RuntimeState state_ = RuntimeState::Idle;

        std::atomic_bool reload_pending { false };
        std::atomic_bool soft_restart_pending { false };
        std::atomic_bool exec_restart_pending { false };
        std::atomic_bool stop_requested { false };

    public:
        struct RenderWindowDeleter {
            Runtime* runtime = nullptr;
            void operator()(backend::RenderWindow* window) const;
        };

        using RenderWindowHandle = std::unique_ptr<backend::RenderWindow, RenderWindowDeleter>;

        using BackendFactory = std::function<std::unique_ptr<Backend>(Core&, Runtime&)>;
        explicit Runtime(BackendFactory backend_factory);

        // Non-copyable, non-movable (address stability for references).
        Runtime(const Runtime&)            = delete;
        Runtime& operator=(const Runtime&) = delete;
        Runtime(Runtime&&)                 = delete;
        Runtime& operator=(Runtime&&)      = delete;
        virtual ~Runtime();

        template<typename T, typename... Args>
        Runtime& use(Args&&... args) {
            auto mod = std::make_unique<T>(ModuleDeps{ *this, core },
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
        Backend&       backend();
        const Backend& backend() const;

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

        CoreSettings             build_core_settings() const;
        std::vector<std::string> validate_settings() const;

        // Fire-and-forget event: push onto the queue, delivered on next
        // drain_events() pass. Inherited post_event<Ev>() from IEventSink
        // wraps the event in TypedEvent<Ev> and calls post_queued().
        using IEventSink::post_event;

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

        // Module-facing UI window factories.
        // Runtime wraps backend::RenderWindow with a custom deleter so window
        // ids are unregistered deterministically on destruction.
        RenderWindowHandle                 create_render_window(const ModuleWindowCreateInfo& info);
        std::unique_ptr<backend::TrayHost> create_tray(backend::RenderWindow& owner, bool own_selection);

    protected:
        // IEventSink: used by Core, backend, and modules to push events
        // onto the unified queue. Runtime is the sole owner of the queue
        // and the sole drain chokepoint.
        void post_queued(std::unique_ptr<QueuedEvent> ev) override {
            event_queue_.push(std::move(ev));
        }

    private:
        std::vector<std::unique_ptr<Module>> modules;
        std::unordered_map<WindowId, backend::RenderWindow*> module_windows_by_id_;

        void unregister_module_window(backend::RenderWindow& window);
        void verify_module_window_registry_consistency(const char* where) const;
        void report_live_module_windows(const char* phase) const;

        EventQueue                             event_queue_;
        PointerRegistry<IEventReceiver>        extra_receivers_;
        HookRegistry                           hook_registry_;

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
