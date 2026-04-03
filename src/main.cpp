#include <csignal>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <climits>
#include <unistd.h>
#include <string>
#include <log.hpp>
#include <backend/backend.hpp>

// Defined in src/backend/backend.cpp — one per build, selected by SWM_BACKEND.
std::unique_ptr<Backend> create_backend(Core& core, Runtime& runtime);
#include <backend/monitor_port.hpp>
#include <config.hpp>
#include <core.hpp>
#include <module_registry.hpp>
#include <runtime.hpp>

// Static modules are linked with --whole-archive from CMake, so registration
// initializers are retained without explicit header includes here.

namespace {

// Non-owning. Written once before signal handlers are installed, read from signal context.
std::atomic<Runtime*> g_signal_runtime { nullptr };

void signal_handler(int signum) {
    if (signum == SIGINT) {
        LOG_INFO("Received SIGINT, stopping window manager");
        if (auto* rt = g_signal_runtime.load())
            rt->request_stop();
    }
}

CoreSettings make_core_settings_from_config(const Config& cfg) {
    CoreSettings out;
    out.monitor_aliases     = cfg.get_monitor_aliases();
    out.monitor_compose     = cfg.get_monitor_compose();
    out.workspace_defs      = cfg.get_workspace_defs();
    out.follow_moved_window = cfg.get_follow_moved_window();
    out.focus_new_window    = cfg.get_focus_new_window();
    out.theme               = cfg.get_theme();
    return out;
}

} // namespace

int main(int argc, char** argv) {
    const char* home     = std::getenv("HOME");
    std::string log_path = "runtime.log";
    if (home)
        log_path = std::string(home) + "/runtime.log";
    log_init(log_path);

    Config         config;
    ModuleRegistry module_registry;
    module_registry_static::apply_static_registrations(module_registry);
    Runtime        runtime(config, module_registry);
    Core           core;
    g_signal_runtime = &runtime;
    signal(SIGINT, signal_handler);

    // Resolve the binary path for exec-restart.
    // Prefer /proc/self/exe so that if the binary on disk is replaced while we
    // are running, execv() picks up the new file rather than the deleted inode.
    std::string exec_path;
    {
        char resolved[PATH_MAX] = {};
        if (readlink("/proc/self/exe", resolved, sizeof(resolved) - 1) > 0) {
            // Strip " (deleted)" suffix that the kernel appends when the inode
            // has been unlinked but the path on disk now points to a new file.
            std::string       s(resolved);
            const std::string suffix = " (deleted)";
            if (s.size() > suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0)
                s.erase(s.size() - suffix.size());
            exec_path = s;
        }
        if (exec_path.empty() && argc > 0 && argv && argv[0]) {
            if (realpath(argv[0], resolved))
                exec_path = resolved;
            else
                exec_path = argv[0];
        }
        if (exec_path.empty())
            exec_path = "sirenwm";
    }

    std::string cfg_path = "init.lua";
    if (home)
        cfg_path = std::string(home) + "/.config/sirenwm/init.lua";

    // 1. Load config — runs Lua config declarations and module registrations.
    if (!config.load(cfg_path, core, runtime)) {
        LOG_ERR("Aborting — failed to load config %s", cfg_path.c_str());
        return 1;
    }

    // 2. Validate — abort on missing required settings
    {
        auto errs = config.validate();
        if (!errs.empty()) {
            for (auto& e : errs)
                LOG_ERR("Config: %s", e.c_str());
            LOG_ERR("Aborting — fix your config at %s", cfg_path.c_str());
            return 1;
        }
    }
    core.apply_settings(make_core_settings_from_config(config));

    // 3. Init backend and bind it before core.init().
    // Some providers (e.g. monitor topology) query backend during core.init().
    auto wm = create_backend(core, runtime);
    if (!wm) {
        LOG_ERR("Aborting — backend creation failed");
        return 1;
    }
    runtime.bind_backend(*wm);

    // 4. Query initial monitor list from backend and pass to core.
    //    Feature modules may later apply topology config (rotation, compose) via ApplyMonitorTopology.
    std::vector<Monitor> initial_monitors;
    {
        auto* mp = wm->monitor_port();
        if (mp)
            initial_monitors = mp->get_monitors();
        else
            LOG_WARN("backend has no monitor_port — starting with empty monitor list");
    }

    core.init(std::move(initial_monitors));

    // 5. Start runtime/modules after root is ready.
    runtime.start(core, *wm);

    LOG_INFO("Siren Window Manager started.");
    runtime.run_loop(*wm, core);

    bool do_exec_restart = runtime.consume_exec_restart_request();
    if (do_exec_restart) {
        // Stop modules with is_exec_restart=true so that ONCE processes (e.g. picom)
        // are left alive and other processes are terminated cleanly. runtime.stop()
        // does NOT call XCloseDisplay — it only calls on_stop() on modules and nulls
        // the backend pointer. The X fd is FD_CLOEXEC (set in XConnection ctor), so
        // it closes automatically after execv. An explicit XCloseDisplay would
        // disconnect the X server immediately and cause picom to drop Damage redirects
        // on transparent windows (render freeze in the replacement process).
        runtime.stop(core, /*is_exec_restart=*/ true);
        LOG_INFO("restart: replacing process via %s", exec_path.c_str());
        char* exec_argv[] = { (char*)exec_path.c_str(), nullptr };
        execv(exec_path.c_str(), exec_argv);
        LOG_ERR("restart: execv failed: %s", std::strerror(errno));
        return 1;
    }
    g_signal_runtime = nullptr;

    return 0;
}