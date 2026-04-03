#include "process_module.hpp"

#include <core.hpp>
#include <config.hpp>
#include <module_registry.hpp>
#include <log.hpp>
#include <runtime.hpp>

#include <algorithm>
#include <csignal>
#include <cstring>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <signal.h>
#include <dirent.h>

// ---------------------------------------------------------------------------
// SIGCHLD self-pipe
// ---------------------------------------------------------------------------

static int g_sigchld_pipe_wr = -1;

static void sigchld_handler(int) {
    if (g_sigchld_pipe_wr >= 0) {
        char b = 1;
        (void)write(g_sigchld_pipe_wr, &b, 1);
    }
}

// ---------------------------------------------------------------------------
// Lua config: siren.autostart = { { cmd = "...", policy = "..." }, ... }
// ---------------------------------------------------------------------------

static RestartPolicy parse_policy(const std::string& s) {
    if (s == "restart")           return RestartPolicy::RESTART;
    if (s == "restart-on-error")  return RestartPolicy::RESTART_ON_ERROR;
    return RestartPolicy::ONCE;
}

static bool parse_autostart_table(LuaContext& lua, int table_idx,
    std::vector<ProcessEntry>& out, std::string& err) {
    if (!lua.is_table(table_idx)) {
        err = "siren.autostart must be a table";
        return false;
    }

    out.clear();
    lua.push_nil();
    while (lua.next(table_idx)) {
        if (!lua.is_table(-1)) {
            lua.pop(1);
            continue;
        }

        ProcessEntry e;

        lua.get_field(-1, "cmd");
        if (!lua.is_string(-1)) {
            err = "autostart entry missing 'cmd' string";
            lua.pop(2);
            return false;
        }
        e.cmd = lua.to_string(-1);
        lua.pop(1);

        lua.get_field(-1, "policy");
        if (lua.is_string(-1))
            e.policy = parse_policy(lua.to_string(-1));
        lua.pop(1);

        out.push_back(std::move(e));
        lua.pop(1); // pop value, keep key for next()
    }

    return true;
}

static void register_handler(Config& config, std::vector<ProcessEntry>& pending) {
    config.register_lua_assignment_handler("autostart",
        [&pending](LuaContext& lua, int idx, std::string& err) -> bool {
            return parse_autostart_table(lua, idx, pending, err);
        });
}

// ---------------------------------------------------------------------------
// Module lifecycle
// ---------------------------------------------------------------------------

void ProcessModule::on_init(Core&) {
    pending_.clear();
    register_handler(config(), pending_);
}

void ProcessModule::on_lua_init(Core&) {
    // Re-register after Lua VM reset; map overwrite is safe.
    register_handler(config(), pending_);
    pending_.clear();
}

// Returns true if a process with the given command is already running (by /proc/comm check).
static bool is_already_running(const std::string& cmd) {
    // Extract basename of the first token in cmd.
    auto        end   = cmd.find_first_of(" \t");
    auto        start = cmd.find_last_of("/", end == std::string::npos ? cmd.size() : end);
    size_t      b     = (start == std::string::npos) ? 0 : start + 1;
    size_t      e     = (end   == std::string::npos) ? cmd.size() : end;
    std::string bin   = cmd.substr(b, e - b);
    if (bin.empty())
        return false;

    DIR* dir = opendir("/proc");
    if (!dir)
        return false;
    bool           found = false;
    struct dirent* ent;
    while (!found && (ent = readdir(dir)) != nullptr) {
        pid_t candidate = (pid_t)atoi(ent->d_name);
        if (candidate <= 1)
            continue;
        char  comm_path[64];
        snprintf(comm_path, sizeof(comm_path), "/proc/%d/comm", (int)candidate);
        FILE* f = fopen(comm_path, "r");
        if (!f)
            continue;
        char comm[256] = {};
        fgets(comm, sizeof(comm), f);
        fclose(f);
        size_t len = strlen(comm);
        if (len > 0 && comm[len - 1] == '\n')
            comm[len - 1] = '\0';
        if (bin == comm)
            found = true;
    }
    closedir(dir);
    return found;
}

void ProcessModule::on_start(Core&) {
    int fds[2];
    if (pipe(fds) < 0) {
        LOG_ERR("ProcessModule: pipe() failed");
        return;
    }
    pipe_rd = fds[0];
    pipe_wr = fds[1];
    fcntl(pipe_rd, F_SETFL, O_NONBLOCK);
    fcntl(pipe_rd, F_SETFD, FD_CLOEXEC);
    fcntl(pipe_wr, F_SETFL, O_NONBLOCK);
    fcntl(pipe_wr, F_SETFD, FD_CLOEXEC);

    g_sigchld_pipe_wr = pipe_wr;
    struct sigaction sa {};
    sa.sa_handler     = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags       = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa, nullptr);

    runtime().watch_fd(pipe_rd, [this]() {
            char buf[64];
            while (read(pipe_rd, buf, sizeof(buf)) > 0) {
            }
            int status;
            pid_t pid;
            while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
                int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
                apply_restart(pid, exit_code);
            }
        });

    entries = std::move(pending_);
    pending_.clear();
    spawn_all();
}

void ProcessModule::on_stop(Core&, bool is_exec_restart) {
    // Reset SIGCHLD to default before closing the pipe so the handler
    // never fires on a stale or already-closed fd.
    signal(SIGCHLD, SIG_DFL);
    g_sigchld_pipe_wr = -1;

    terminate_all(is_exec_restart);
    if (pipe_rd >= 0) {
        close(pipe_rd); pipe_rd = -1;
    }
    if (pipe_wr >= 0) {
        close(pipe_wr); pipe_wr = -1;
    }
}

void ProcessModule::on_reload(Core&) {
    diff_and_apply(pending_);
    pending_.clear();
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

void ProcessModule::spawn(ProcessEntry& e) {
    pid_t pid = fork();
    if (pid < 0) {
        LOG_ERR("ProcessModule: fork() failed for '%s'", e.cmd.c_str());
        return;
    }
    if (pid == 0) {
        setsid();

        struct sigaction sa = {};
        sa.sa_handler = SIG_DFL;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGCHLD, &sa, nullptr);

        struct rlimit rl;
        getrlimit(RLIMIT_NOFILE, &rl);
        int           max_fd = (int)(rl.rlim_cur < 4096 ? rl.rlim_cur : 4096);
        for (int fd = STDERR_FILENO + 1; fd < max_fd; fd++)
            close(fd);

        execl("/bin/sh", "sh", "-c", e.cmd.c_str(), nullptr);
        _exit(1);
    }
    e.pid     = pid;
    e.running = true;
    LOG_INFO("ProcessModule: spawned '%s' pid=%d", e.cmd.c_str(), pid);
}

void ProcessModule::spawn_all(bool exec_restart) {
    for (auto& e : entries) {
        if (e.policy == RestartPolicy::ONCE && is_already_running(e.cmd)) {
            // ONCE processes are not restarted — if one is already running (e.g.
            // picom surviving an exec-restart), skip spawning a second copy.
            LOG_INFO("ProcessModule: once: '%s' already running, skipping spawn", e.cmd.c_str());
            continue;
        }
        (void)exec_restart;
        spawn(e);
    }
}

void ProcessModule::terminate_all(bool is_exec_restart) {
    for (auto& e : entries) {
        if (is_exec_restart && e.policy == RestartPolicy::ONCE) {
            // Let ONCE processes survive exec-restart.
            continue;
        }
        if (e.running && e.pid > 0) {
            kill(e.pid, SIGTERM);
            e.running = false;
        }
    }

    // Non-blocking reap pass: collect whatever already exited.
    for (auto& e : entries) {
        if (e.pid <= 0) continue;
        int status;
        if (waitpid(e.pid, &status, WNOHANG) > 0)
            e.pid = -1;
    }

    // SIGKILL any survivors — we cannot block the event loop.
    for (auto& e : entries) {
        if (is_exec_restart && e.policy == RestartPolicy::ONCE)
            continue;
        if (e.pid > 0) {
            kill(e.pid, SIGKILL);
            e.pid = -1;
        }
        e.running = false;
    }
}

void ProcessModule::apply_restart(pid_t pid, int exit_code) {
    for (auto& e : entries) {
        if (e.pid != pid) continue;
        e.running = false;
        e.pid     = -1;

        LOG_INFO("ProcessModule: '%s' exited (code=%d, policy=%d)",
            e.cmd.c_str(), exit_code, (int)e.policy);

        switch (e.policy) {
            case RestartPolicy::ONCE:
                break;
            case RestartPolicy::RESTART:
                spawn(e);
                break;
            case RestartPolicy::RESTART_ON_ERROR:
                if (exit_code != 0) spawn(e);
                break;
        }
        return;
    }
}

void ProcessModule::diff_and_apply(const std::vector<ProcessEntry>& new_entries) {
    for (auto& e : entries) {
        bool still_present = std::any_of(new_entries.begin(), new_entries.end(),
                [&](const ProcessEntry& n) {
                    return n.cmd == e.cmd;
                });
        if (!still_present && e.running && e.pid > 0) {
            LOG_INFO("ProcessModule: stopping removed entry '%s'", e.cmd.c_str());
            kill(e.pid, SIGTERM);
            e.running = false;
            e.pid     = -1;
        }
    }

    for (const auto& n : new_entries) {
        auto it = std::find_if(entries.begin(), entries.end(),
                [&](const ProcessEntry& e) {
                    return e.cmd == n.cmd;
                });
        if (it != entries.end()) {
            it->policy = n.policy;
        } else {
            entries.push_back(n);
            spawn(entries.back());
        }
    }

    entries.erase(std::remove_if(entries.begin(), entries.end(),
        [&](const ProcessEntry& e) {
            return !std::any_of(new_entries.begin(), new_entries.end(),
                   [&](const ProcessEntry& n) {
                       return n.cmd == e.cmd;
                   });
        }), entries.end());
}

static bool _swm_registered_lua_symbols_process = []() {
        module_registry_static::add_lua_symbol_registration("autostart", "process");
        return true;
    }();

SWM_REGISTER_MODULE("process", ProcessModule)