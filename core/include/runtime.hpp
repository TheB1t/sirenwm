#pragma once

#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>
#include <atomic>
#include <sys/select.h>

#include <backend/events.hpp>
#include <module.hpp>

class Core;
class Backend;
class Config;
class ModuleRegistry;

class Runtime {
    private:
        enum class ReloadRequest {
            None,
            ReloadConfig,
            SoftRestart,
        };

        Config& config_;
        ModuleRegistry& module_registry_;
        // Non-owning. Bound via bind_backend() before start(), unbound on stop().
        Backend* active_backend_ = nullptr;

        std::vector<std::unique_ptr<Module> > modules;
        bool modules_frozen = false; // true after first start() — use() becomes no-op

        std::function<void()> display_change_handler;
        int backend_extension_event_base = -1;

        struct WatchedFd {
            int                   fd;
            std::function<void()> cb;
        };
        std::vector<WatchedFd> watched_fds;

        std::atomic_bool reload_pending { false };
        std::atomic_bool soft_restart_pending { false };
        std::atomic_bool exec_restart_pending { false };
        std::atomic_bool stop_requested { false };

    public:
        Runtime(Config& config, ModuleRegistry& module_registry)
            : config_(config), module_registry_(module_registry) {}

        template<typename T, typename... Args>
        Runtime& use(Core& core, Args&&... args) {
            auto mod = std::make_unique<T>(ModuleDeps{ *this, config_ },
                    std::forward<Args>(args)...);
            mod->on_init(core);
            modules.push_back(std::move(mod));
            return *this;
        }

        Runtime& use(Core& core, const std::string& name);

        template<typename T>
        T* get_module() {
            for (auto& mod : modules)
                if (auto* p = dynamic_cast<T*>(mod.get()))
                    return p;
            return nullptr;
        }

        Module* get_module_by_name(const std::string& name);

        void    emit_lua_init(Core& core);
        void bind_backend(Backend& backend) { active_backend_ = &backend; }
        void unbind_backend() { active_backend_ = nullptr; }
        void start(Core& core, Backend& backend);
        void stop(Core& core, bool is_exec_restart = false);
        void reload(Core& core);
        void request_reload();
        void request_soft_restart();
        void request_exec_restart(Core& core);
        bool consume_exec_restart_request();
        void request_stop() { stop_requested = true; }
        bool process_pending_reload(Core& core);
        void run_loop(Backend& backend, Core& core);
        Config& config() { return config_; }
        const Config& config() const { return config_; }
        Backend&       backend();
        const Backend& backend() const;
        ModuleRegistry& module_registry() { return module_registry_; }
        const ModuleRegistry& module_registry() const { return module_registry_; }

        template<typename Ev>
        void emit(Core& core, Ev ev) {
            for (auto& m : modules)
                m->on(core, ev);
        }

        template<typename Ev>
        bool emit_until_handled(Core& core, Ev ev) {
            for (auto& m : modules)
                if (m->on(core, ev))
                    return true;
            return false;
        }

        template<typename Ev>
        void query(Core& core, Ev& ev) {
            for (auto& m : modules)
                m->on(core, ev);
        }

        template<typename Ev, typename StopPred>
        void query(Core& core, Ev& ev, StopPred stop_pred) {
            for (auto& m : modules) {
                m->on(core, ev);
                if (stop_pred(ev))
                    return;
            }
        }

        void set_backend_extension_event_base(int base) { backend_extension_event_base = base; }
        int get_backend_extension_event_base() const { return backend_extension_event_base; }
        void set_display_change_handler(std::function<void()> fn) { display_change_handler = std::move(fn); }
        void dispatch_display_change();

        void watch_fd(int fd, std::function<void()> cb);
        void populate_watched_fds(fd_set& fds, int& max_fd) const;
        void dispatch_watched_fds(const fd_set& fds);

    private:
        void          save_restart_state(const Core& core);
        ReloadRequest consume_reload_request();
        void          drain_core_events(Core& core);
        bool          reload_runtime_config(Core& core);
};