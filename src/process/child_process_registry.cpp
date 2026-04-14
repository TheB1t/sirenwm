#include "child_process_registry.hpp"

#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <fstream>
#include <sstream>

namespace {

bool has_all_keys(const std::unordered_map<std::string, std::string>& env,
    const std::vector<std::string>& keys) {
    for (const auto& key : keys) {
        auto it = env.find(key);
        if (it == env.end() || it->second.empty())
            return false;
    }
    return true;
}

} // namespace

ChildProcessRegistry::ChildProcessRegistry(std::string app_name)
    : app_name_(std::move(app_name)) {
    const std::string run = runtime_dir();
    state_path_ = run + "/" + app_name_ + "-children.state";
    lock_path_  = run + "/" + app_name_ + "-children.lock";
    lock();
    load_state();
}

ChildProcessRegistry::~ChildProcessRegistry() {
    unlock();
}

bool ChildProcessRegistry::spawn_or_adopt(const ManagedChildSpec& spec, ManagedChildInfo& out, std::string* err) {
    if (spec.role.empty()) {
        if (err) *err = "child role is empty";
        return false;
    }
    if (spec.argv.empty() || spec.argv.front().empty()) {
        if (err) *err = "child argv is empty";
        return false;
    }

    if (adopt(spec.role, spec.required_env_keys, out))
        return true;

    if (auto* existing = find_entry(spec.role)) {
        if (existing->owned && is_process_alive(existing->pid)) {
            kill(existing->pid, SIGTERM);
            waitpid(existing->pid, nullptr, WNOHANG);
        }
        existing->pid = -1;
        existing->env.clear();
    }

    Entry spawned;
    if (!spawn(spec, spawned, err))
        return false;

    entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
        [&](const Entry& e) {
            return e.role == spec.role;
        }),
        entries_.end());
    entries_.push_back(spawned);
    save_state();

    out.pid   = spawned.pid;
    out.owned = spawned.owned;
    out.env   = spawned.env;
    return true;
}

bool ChildProcessRegistry::adopt(const std::string& role, const std::vector<std::string>& required_env_keys,
    ManagedChildInfo& out) {
    auto* existing = find_entry(role);
    if (!existing)
        return false;
    if (!entry_ready(*existing, required_env_keys)) {
        existing->pid = -1;
        existing->env.clear();
        entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
            [](const Entry& e) {
                return e.pid <= 0;
            }),
            entries_.end());
        save_state_best_effort();
        return false;
    }
    out.pid   = existing->pid;
    out.owned = existing->owned;
    out.env   = existing->env;
    return true;
}

void ChildProcessRegistry::shutdown_owned() {
    bool changed = false;
    for (auto& e : entries_) {
        if (!e.owned || e.pid <= 0)
            continue;
        if (!is_process_alive(e.pid)) {
            e.pid = -1;
            e.env.clear();
            changed = true;
            continue;
        }
        kill(e.pid, SIGTERM);
        waitpid(e.pid, nullptr, WNOHANG);
        e.pid = -1;
        e.env.clear();
        changed = true;
    }

    if (changed) {
        entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
            [](const Entry& e) {
                return e.pid <= 0;
            }),
            entries_.end());
        save_state_best_effort();
    }
}

void ChildProcessRegistry::load_state() {
    entries_.clear();
    std::ifstream in(state_path_);
    if (!in.is_open())
        return;

    Entry       current;
    bool        in_entry = false;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty())
            continue;
        if (line == "---") {
            if (in_entry && !current.role.empty() && current.pid > 0)
                entries_.push_back(current);
            current  = Entry {};
            in_entry = false;
            continue;
        }
        in_entry = true;
        if (line.rfind("role=", 0) == 0) {
            current.role = line.substr(5);
            continue;
        }
        if (line.rfind("pid=", 0) == 0) {
            current.pid = static_cast<pid_t>(std::strtol(line.c_str() + 4, nullptr, 10));
            continue;
        }
        if (line.rfind("owned=", 0) == 0) {
            current.owned = (line.substr(6) == "1");
            continue;
        }
        if (line.rfind("env.", 0) == 0) {
            auto eq = line.find('=', 4);
            if (eq == std::string::npos)
                continue;
            std::string key = line.substr(4, eq - 4);
            std::string val = line.substr(eq + 1);
            if (!key.empty())
                current.env[key] = val;
        }
    }
    if (in_entry && !current.role.empty() && current.pid > 0)
        entries_.push_back(current);
}

void ChildProcessRegistry::save_state() const {
    const std::string tmp_path = state_path_ + ".tmp";
    {
        std::ofstream out(tmp_path, std::ios::trunc);
        if (!out.is_open())
            return;
        for (const auto& e : entries_) {
            if (e.role.empty() || e.pid <= 0)
                continue;
            out << "role=" << e.role << '\n';
            out << "pid=" << e.pid << '\n';
            out << "owned=" << (e.owned ? "1" : "0") << '\n';
            for (const auto& [k, v] : e.env)
                out << "env." << k << '=' << v << '\n';
            out << "---\n";
        }
    }
    rename(tmp_path.c_str(), state_path_.c_str());
}

void ChildProcessRegistry::save_state_best_effort() const {
    save_state();
}

void ChildProcessRegistry::lock() {
    lock_fd_ = open(lock_path_.c_str(), O_CREAT | O_RDWR, 0600);
    if (lock_fd_ < 0)
        return;
    flock(lock_fd_, LOCK_EX);
}

void ChildProcessRegistry::unlock() {
    if (lock_fd_ >= 0) {
        flock(lock_fd_, LOCK_UN);
        close(lock_fd_);
        lock_fd_ = -1;
    }
}

std::string ChildProcessRegistry::runtime_dir() {
    if (const char* xdg = std::getenv("XDG_RUNTIME_DIR"); xdg && *xdg)
        return xdg;
    std::string dir = "/tmp/sirenwm-runtime-" + std::to_string(getuid());
    mkdir(dir.c_str(), 0700);
    return dir;
}

bool ChildProcessRegistry::is_process_alive(pid_t pid) {
    if (pid <= 0)
        return false;
    if (kill(pid, 0) == 0)
        return true;
    return errno == EPERM;
}

bool ChildProcessRegistry::wayland_socket_exists(const std::string& wayland_display) {
    if (wayland_display.empty())
        return false;
    const std::string path = runtime_dir() + "/" + wayland_display;
    struct stat st {};
    if (stat(path.c_str(), &st) != 0)
        return false;
    return S_ISSOCK(st.st_mode);
}

bool ChildProcessRegistry::parse_env_line(const std::string& line, std::string& key, std::string& value) {
    auto eq = line.find('=');
    if (eq == std::string::npos || eq == 0)
        return false;
    key   = line.substr(0, eq);
    value = line.substr(eq + 1);
    if (key.empty())
        return false;
    for (char c : key) {
        if (!(c == '_' || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')))
            return false;
    }
    return true;
}

bool ChildProcessRegistry::wait_for_env_lines(int pipe_rd, pid_t pid,
    const std::vector<std::string>& required_env_keys, int timeout_ms,
    std::unordered_map<std::string, std::string>& out_env) {
    out_env.clear();
    std::string buffer;
    char        temp[512];
    const int   step_ms   = 100;
    const int   max_ticks = std::max(1, timeout_ms / step_ms);

    for (int i = 0; i < max_ticks; ++i) {
        struct pollfd pfd {};
        pfd.fd     = pipe_rd;
        pfd.events = POLLIN;
        int pr = poll(&pfd, 1, step_ms);
        if (pr > 0 && (pfd.revents & POLLIN)) {
            ssize_t n = read(pipe_rd, temp, sizeof(temp));
            if (n > 0) {
                buffer.append(temp, temp + n);
                std::size_t pos = 0;
                while (true) {
                    std::size_t nl = buffer.find('\n', pos);
                    if (nl == std::string::npos) {
                        buffer.erase(0, pos);
                        break;
                    }
                    std::string line = buffer.substr(pos, nl - pos);
                    pos = nl + 1;
                    std::string key;
                    std::string value;
                    if (parse_env_line(line, key, value))
                        out_env[key] = value;
                }
            }
        }

        if (has_all_keys(out_env, required_env_keys))
            return true;

        int   status = 0;
        pid_t wr     = waitpid(pid, &status, WNOHANG);
        if (wr == pid)
            return false;
    }
    return false;
}

bool ChildProcessRegistry::entry_ready(const Entry& e, const std::vector<std::string>& required_env_keys) const {
    if (e.pid <= 0)
        return false;
    if (!is_process_alive(e.pid))
        return false;
    if (!has_all_keys(e.env, required_env_keys))
        return false;
    auto it = e.env.find("WAYLAND_DISPLAY");
    if (it != e.env.end() && !wayland_socket_exists(it->second))
        return false;
    return true;
}

ChildProcessRegistry::Entry* ChildProcessRegistry::find_entry(const std::string& role) {
    for (auto& e : entries_) {
        if (e.role == role)
            return &e;
    }
    return nullptr;
}

bool ChildProcessRegistry::spawn(const ManagedChildSpec& spec, Entry& out_entry, std::string* err) {
    int pipefd[2] = { -1, -1 };
    if (pipe(pipefd) != 0) {
        if (err) *err = std::string("pipe failed: ") + std::strerror(errno);
        return false;
    }

    pid_t pid = fork();
    if (pid < 0) {
        if (err) *err = std::string("fork failed: ") + std::strerror(errno);
        close(pipefd[0]);
        close(pipefd[1]);
        return false;
    }

    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);

        std::vector<char*> argv;
        argv.reserve(spec.argv.size() + 1);
        for (const auto& arg : spec.argv)
            argv.push_back(const_cast<char*>(arg.c_str()));
        argv.push_back(nullptr);

        execvp(argv[0], argv.data());
        _exit(127);
    }

    close(pipefd[1]);
    std::unordered_map<std::string, std::string> env;
    const bool                                   ready = wait_for_env_lines(pipefd[0], pid, spec.required_env_keys,
            spec.startup_timeout_ms, env);
    close(pipefd[0]);

    if (!ready) {
        kill(pid, SIGTERM);
        waitpid(pid, nullptr, 0);
        if (err) *err = "child did not publish required endpoints";
        return false;
    }

    out_entry.role  = spec.role;
    out_entry.pid   = pid;
    out_entry.owned = true;
    out_entry.env   = std::move(env);
    return true;
}
