#include <csignal>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <climits>
#include <unistd.h>
#include <sys/stat.h>
#include <string>
#include <log.hpp>

#include <module_registry.hpp>
#include <runtime.hpp>

#if defined(SIRENWM_BACKEND_WAYLAND)
#  include <wl_backend.hpp>
using ActiveBackend = WaylandBackend;
#else
#  include <x11_backend.hpp>
using ActiveBackend = X11Backend;
#endif

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

std::string resolve_default_config_path() {
    auto try_default = [](const std::string& dir) -> std::string {
        std::string p = dir + "/sirenwm/init.lua.default";
        struct stat s {};
        return (stat(p.c_str(), &s) == 0) ? p : "";
    };

    std::string result;
    if (const char* xdg = std::getenv("XDG_DATA_DIRS")) {
        std::string dirs(xdg);
        for (std::string::size_type pos = 0, end; result.empty();) {
            end    = dirs.find(':', pos);
            result = try_default(dirs.substr(pos, end == std::string::npos ? end : end - pos));
            if (end == std::string::npos) break;
            pos = end + 1;
        }
    }
    if (result.empty()) result = try_default("/usr/local/share");
    if (result.empty()) result = try_default("/usr/share");
    return result;
}

std::string resolve_user_config_path() {
    const char* home = std::getenv("HOME");
    std::string cfg_path = "init.lua";
    if (home)
        cfg_path = std::string(home) + "/.config/sirenwm/init.lua";
    return cfg_path;
}

std::string resolve_exec_path(int argc, char** argv) {
    char resolved[PATH_MAX] = {};
    if (argc > 0 && argv && argv[0]) {
        if (realpath(argv[0], resolved))
            return resolved;
        return argv[0];
    }
    return "sirenwm";
}

} // namespace

int main(int argc, char** argv) {
    const char* home     = std::getenv("HOME");
    std::string log_path = "runtime.log";
    if (home)
        log_path = std::string(home) + "/runtime.log";
    log_init(log_path);

    ModuleRegistry module_registry;
    module_registry_static::apply_static_registrations(module_registry);
    RuntimeOf<ActiveBackend> runtime(module_registry);
    g_signal_runtime = &runtime;
    signal(SIGINT, signal_handler);

    std::string exec_path    = resolve_exec_path(argc, argv);
    std::string cfg_path     = resolve_user_config_path();
    std::string default_path = resolve_default_config_path();

    runtime.run(cfg_path, default_path);

    if (runtime.consume_exec_restart_request()) {
        LOG_INFO("restart: replacing process via %s", exec_path.c_str());
        spdlog::shutdown();
        char* exec_argv[] = { (char*)exec_path.c_str(), nullptr };
        execv(exec_path.c_str(), exec_argv);
        LOG_ERR("restart: execv failed: %s", std::strerror(errno));
        return 1;
    }
    g_signal_runtime = nullptr;

    return 0;
}
