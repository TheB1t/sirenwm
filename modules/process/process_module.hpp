#pragma once

#include <module.hpp>
#include <string>
#include <vector>
#include <sys/types.h>

enum class RestartPolicy {
    ONCE,             // start once per X session, survives WM exec-restart
    RESTART,          // restart unconditionally on any exit
    RESTART_ON_ERROR, // restart only if exit code != 0
};

struct ProcessEntry {
    std::string   cmd;
    RestartPolicy policy  = RestartPolicy::ONCE;
    pid_t         pid     = -1;
    bool          running = false;
};

class ProcessModule : public Module {
    public:
        explicit ProcessModule(ModuleDeps deps) : Module(deps) {}
        std::string name() const override { return "process"; }

        void on_init(Core& core)                          override;
        void on_lua_init(Core& core)                      override;
        void on_start(Core& core)                         override;
        void on_stop(Core& core, bool is_exec_restart)    override;
        void on_reload(Core& core)                        override;

    private:
        std::vector<ProcessEntry> entries;
        std::vector<ProcessEntry> pending_;

        void spawn(ProcessEntry& e);
        void spawn_all(bool exec_restart = false);
        void terminate_all(bool is_exec_restart);
        void apply_restart(pid_t pid, int exit_code);
        void diff_and_apply(const std::vector<ProcessEntry>& new_entries);

        // self-pipe: SIGCHLD writes a byte, event loop reads and calls handle_child_exit
        int pipe_rd = -1;
        int pipe_wr = -1;
};