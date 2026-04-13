#include <csignal>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <climits>
#include <exception>
#include <string_view>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/poll.h>
#include <fcntl.h>
#include <string>
#include <vector>
#include <log.hpp>

#include <runtime.hpp>

#if defined(SIRENWM_BACKEND_WAYLAND)
#  include <wl_backend.hpp>
#  include <wl/server/display_server.hpp>
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
pid_t                 g_spawned_display_server_pid = -1;

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

bool wait_for_display_server_endpoints(int pipe_rd, pid_t pid, std::string& out_wayland, std::string& out_xdisplay) {
    std::string buffer;
    char        temp[512];
    int         ticks = 0;

    while (ticks++ < 100) { // 10s max
        struct pollfd pfd {};
        pfd.fd     = pipe_rd;
        pfd.events = POLLIN;
        int pr = poll(&pfd, 1, 100);
        if (pr > 0 && (pfd.revents & POLLIN)) {
            ssize_t n = read(pipe_rd, temp, sizeof(temp));
            if (n > 0) {
                buffer.append(temp, temp + n);
                std::size_t pos = 0;
                while (true) {
                    auto nl = buffer.find('\n', pos);
                    if (nl == std::string::npos) {
                        buffer.erase(0, pos);
                        break;
                    }
                    std::string line = buffer.substr(pos, nl - pos);
                    pos = nl + 1;
                    if (line.rfind("WAYLAND_DISPLAY=", 0) == 0)
                        out_wayland = line.substr(std::strlen("WAYLAND_DISPLAY="));
                    else if (line.rfind("DISPLAY=", 0) == 0)
                        out_xdisplay = line.substr(std::strlen("DISPLAY="));
                }
            }
        }

        if (!out_wayland.empty())
            return true;

        int   status = 0;
        pid_t wr     = waitpid(pid, &status, WNOHANG);
        if (wr == pid)
            return false;
    }
    return false;
}

bool ensure_embedded_display_server(const std::string& exec_path, bool from_exec_restart) {
    const char* wayland_display = std::getenv("WAYLAND_DISPLAY");
    if (wayland_display && *wayland_display)
        return true;
    if (from_exec_restart)
        return true;

    int pipefd[2] = { -1, -1 };
    if (pipe(pipefd) != 0) {
        LOG_ERR("display-server: failed to create endpoint pipe: %s", std::strerror(errno));
        return false;
    }

    pid_t pid = fork();
    if (pid < 0) {
        LOG_ERR("display-server: fork failed: %s", std::strerror(errno));
        close(pipefd[0]);
        close(pipefd[1]);
        return false;
    }

    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);
        std::vector<char*> argv_vec {
            const_cast<char*>(exec_path.c_str()),
            const_cast<char*>("--display-server"),
            nullptr
        };
        execvp(exec_path.c_str(), argv_vec.data());
        _exit(127);
    }

    close(pipefd[1]);

    std::string spawned_wayland;
    std::string spawned_xdisplay;
    bool ready = wait_for_display_server_endpoints(pipefd[0], pid, spawned_wayland, spawned_xdisplay);
    close(pipefd[0]);

    if (!ready) {
        LOG_ERR("display-server: failed to receive WAYLAND_DISPLAY from spawned server");
        kill(pid, SIGTERM);
        waitpid(pid, nullptr, 0);
        return false;
    }

    setenv("WAYLAND_DISPLAY", spawned_wayland.c_str(), 1);
    if (!spawned_xdisplay.empty())
        setenv("DISPLAY", spawned_xdisplay.c_str(), 1);

    g_spawned_display_server_pid = pid;
    LOG_INFO("display-server: spawned pid=%d WAYLAND_DISPLAY=%s",
        static_cast<int>(pid), spawned_wayland.c_str());
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
        if (!ensure_embedded_display_server(exec_path, from_exec_restart))
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
            return 1;
        }
        g_signal_runtime = nullptr;
    } catch (const std::exception& e) {
        g_signal_runtime = nullptr;
        LOG_ERR("main: fatal startup/runtime error: %s", e.what());
        if (g_spawned_display_server_pid > 0) {
            kill(g_spawned_display_server_pid, SIGTERM);
            waitpid(g_spawned_display_server_pid, nullptr, 0);
            g_spawned_display_server_pid = -1;
        }
        return 1;
    }
    if (g_spawned_display_server_pid > 0) {
        kill(g_spawned_display_server_pid, SIGTERM);
        waitpid(g_spawned_display_server_pid, nullptr, 0);
        g_spawned_display_server_pid = -1;
    }
    LOG_INFO("main: exit");
    return 0;
}
