#pragma once

#include <sys/types.h>

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

struct ManagedChildSpec {
    std::string              role;
    std::vector<std::string> argv;
    std::vector<std::string> required_env_keys;
    int                      startup_timeout_ms = 10000;
};

struct ManagedChildInfo {
    pid_t                                        pid   = -1;
    bool                                         owned = false;
    std::unordered_map<std::string, std::string> env;
};

class ChildProcessRegistry {
    public:
        explicit ChildProcessRegistry(std::string app_name = "sirenwm");
        ~ChildProcessRegistry();

        ChildProcessRegistry(const ChildProcessRegistry&)            = delete;
        ChildProcessRegistry& operator=(const ChildProcessRegistry&) = delete;

        bool                  adopt(const std::string& role, const std::vector<std::string>& required_env_keys,
            ManagedChildInfo& out);
        bool                  spawn_or_adopt(const ManagedChildSpec& spec, ManagedChildInfo& out, std::string* err = nullptr);
        void                  shutdown_owned();

    private:
        struct Entry {
            std::string                                  role;
            pid_t                                        pid   = -1;
            bool                                         owned = false;
            std::unordered_map<std::string, std::string> env;
        };

        std::string        app_name_;
        std::string        state_path_;
        std::string        lock_path_;
        int                lock_fd_ = -1;
        std::vector<Entry> entries_;

        void               load_state();
        void               save_state() const;
        void               save_state_best_effort() const;
        void               lock();
        void               unlock();

        static std::string runtime_dir();
        static bool        is_process_alive(pid_t pid);
        static bool        wayland_socket_exists(const std::string& wayland_display);
        static bool        parse_env_line(const std::string& line, std::string& key, std::string& value);
        static bool        wait_for_env_lines(int pipe_rd, pid_t pid,
            const std::vector<std::string>& required_env_keys, int timeout_ms,
            std::unordered_map<std::string, std::string>& out_env);

        bool   entry_ready(const Entry& e, const std::vector<std::string>& required_env_keys) const;
        Entry* find_entry(const std::string& role);
        bool   spawn(const ManagedChildSpec& spec, Entry& out_entry, std::string* err);
};
