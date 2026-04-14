#include <csignal>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <climits>
#include <exception>
#include <string_view>
#include <unistd.h>
#include <sys/stat.h>
#include <string>
#include <vector>
#include <log.hpp>

#include <runtime.hpp>

#if defined(SIRENWM_BACKEND_WAYLAND)
#  include <wl_backend.hpp>
#  include <wl/server/display_server.hpp>
#  include "process/child_process_registry.hpp"
using ActiveBackend = WlBackend;
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
    if (auto* rt = g_signal_runtime.load()) {
        if (signum == SIGINT) {
            LOG_INFO("Received SIGINT, stopping window manager");
            rt->request_stop();
        } else if (signum == SIGHUP) {
            LOG_INFO("Received SIGHUP, reloading config");
            rt->request_reload();
        }
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
    const char* home     = std::getenv("HOME");
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

#if !defined(SIRENWM_HAS_DISPLAY_SERVER)
bool has_display_server_args(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i] ? argv[i] : "";
        if (arg == "--display-server")
            return true;
        if (arg == "--size" || arg.rfind("--size=", 0) == 0)
            return true;
    }
    return false;
}
#endif

#if defined(SIRENWM_HAS_DISPLAY_SERVER)
struct CliOptions {
    bool display_server_mode = false;
    int  display_width       = 1280;
    int  display_height      = 720;
};

bool parse_size_arg(std::string_view spec, int& w, int& h) {
    int pw = 0;
    int ph = 0;
    if (std::sscanf(std::string(spec).c_str(), "%dx%d", &pw, &ph) != 2)
        return false;
    if (pw <= 0 || ph <= 0)
        return false;
    w = pw;
    h = ph;
    return true;
}

CliOptions parse_cli(int argc, char** argv) {
    CliOptions opts;
    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i] ? argv[i] : "";
        if (arg == "--display-server") {
            opts.display_server_mode = true;
            continue;
        }
        if (arg == "--size") {
            if (i + 1 < argc && parse_size_arg(argv[i + 1], opts.display_width, opts.display_height)) {
                ++i;
            }
            continue;
        }
        if (arg.rfind("--size=", 0) == 0) {
            (void)parse_size_arg(arg.substr(7), opts.display_width, opts.display_height);
            continue;
        }
    }
    return opts;
}

bool ensure_embedded_display_server(const std::string& exec_path, bool from_exec_restart,
    ChildProcessRegistry& child_registry) {
    const char* wayland_display = std::getenv("WAYLAND_DISPLAY");
    if (wayland_display && *wayland_display) {
        ManagedChildInfo adopted;
        if (child_registry.adopt("display-server", { "WAYLAND_DISPLAY" }, adopted)) {
            auto it_wayland = adopted.env.find("WAYLAND_DISPLAY");
            if (it_wayland != adopted.env.end() && it_wayland->second == wayland_display) {
                auto it_display = adopted.env.find("DISPLAY");
                if (it_display != adopted.env.end() && !it_display->second.empty())
                    setenv("DISPLAY", it_display->second.c_str(), 1);
                LOG_INFO("display-server: adopted pid=%d WAYLAND_DISPLAY=%s",
                    static_cast<int>(adopted.pid), it_wayland->second.c_str());
            }
        }
        return true;
    }

    if (from_exec_restart)
        LOG_WARN("display-server: WAYLAND_DISPLAY missing after exec-restart, trying adopt/spawn");

    ManagedChildSpec spec;
    spec.role              = "display-server";
    spec.argv              = { exec_path, "--display-server" };
    spec.required_env_keys = { "WAYLAND_DISPLAY" };

    ManagedChildInfo child;
    std::string      err;
    if (!child_registry.spawn_or_adopt(spec, child, &err)) {
        LOG_ERR("display-server: startup failed: %s", err.c_str());
        return false;
    }

    auto it_wayland = child.env.find("WAYLAND_DISPLAY");
    if (it_wayland == child.env.end() || it_wayland->second.empty()) {
        LOG_ERR("display-server: startup failed: missing WAYLAND_DISPLAY");
        return false;
    }
    setenv("WAYLAND_DISPLAY", it_wayland->second.c_str(), 1);

    auto it_display = child.env.find("DISPLAY");
    if (it_display != child.env.end() && !it_display->second.empty())
        setenv("DISPLAY", it_display->second.c_str(), 1);
    else
        unsetenv("DISPLAY");

    LOG_INFO("display-server: using pid=%d WAYLAND_DISPLAY=%s",
        static_cast<int>(child.pid), it_wayland->second.c_str());
    return true;
}
#endif

} // namespace

int main(int argc, char** argv) {
    const char* home     = std::getenv("HOME");
    std::string log_path = "runtime.log";
    if (home)
        log_path = std::string(home) + "/runtime.log";
    log_init(log_path);

    std::string exec_path = resolve_exec_path(argc, argv);

#if defined(SIRENWM_HAS_DISPLAY_SERVER)
    std::unique_ptr<ChildProcessRegistry> child_registry;
#endif

    try {
#if !defined(SIRENWM_HAS_DISPLAY_SERVER)
        if (has_display_server_args(argc, argv)) {
            LOG_ERR("--display-server/--size are available only in Wayland build");
            return 2;
        }
#endif

#if defined(SIRENWM_HAS_DISPLAY_SERVER)
        CliOptions cli = parse_cli(argc, argv);
        if (cli.display_server_mode) {
            wl::server::DisplayServerOptions ds_opts;
            ds_opts.width  = cli.display_width;
            ds_opts.height = cli.display_height;
            return wl::server::run_display_server(ds_opts);
        }

        bool from_exec_restart = (std::getenv("SIRENWM_EXEC_RESTART") != nullptr);
        unsetenv("SIRENWM_EXEC_RESTART");
        child_registry = std::make_unique<ChildProcessRegistry>("sirenwm");
        if (!ensure_embedded_display_server(exec_path, from_exec_restart, *child_registry))
            return 1;
#endif

        std::string cfg_path     = resolve_user_config_path();
        std::string default_path = resolve_default_config_path();

        Runtime runtime([](Core& core, Runtime& rt) {
                return std::make_unique<ActiveBackend>(core, rt);
            });
        g_signal_runtime = &runtime;
        signal(SIGINT,  signal_handler);
        signal(SIGHUP,  signal_handler);

        runtime.run(cfg_path, default_path);

        if (runtime.consume_exec_restart_request()) {
            LOG_INFO("restart: replacing process via %s", exec_path.c_str());
            runtime.backend().prepare_exec_restart();
            setenv("SIRENWM_EXEC_RESTART", "1", 1);
            spdlog::shutdown();
            char* exec_argv[] = { (char*)exec_path.c_str(), nullptr };
            execvp(exec_path.c_str(), exec_argv);
            LOG_ERR("restart: execvp failed: %s", std::strerror(errno));
#if defined(SIRENWM_HAS_DISPLAY_SERVER)
            if (child_registry)
                child_registry->shutdown_owned();
#endif
            return 1;
        }
        g_signal_runtime = nullptr;
    } catch (const std::exception& e) {
        g_signal_runtime = nullptr;
        LOG_ERR("main: fatal startup/runtime error: %s", e.what());
#if defined(SIRENWM_HAS_DISPLAY_SERVER)
        if (child_registry)
            child_registry->shutdown_owned();
#endif
        return 1;
    }
#if defined(SIRENWM_HAS_DISPLAY_SERVER)
    if (child_registry)
        child_registry->shutdown_owned();
#endif
    LOG_INFO("main: exit");
    return 0;
}
