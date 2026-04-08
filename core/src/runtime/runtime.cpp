#include <runtime.hpp>

#include <backend/backend.hpp>
#include <backend/commands.hpp>
#include <config.hpp>
#include <core.hpp>
#include <lua_events.hpp>
#include <module_registry.hpp>
#include <monitor_layout.hpp>
#include <log.hpp>

#include <cstdlib>
#include <csignal>
#include <fcntl.h>
#include <fstream>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <unistd.h>
#include <sys/epoll.h>
#include <restart_state.hpp>
#include <sys/wait.h>

// ---------------------------------------------------------------------------
// RuntimeState helpers
// ---------------------------------------------------------------------------

const char* runtime_state_name(RuntimeState s) {
    switch (s) {
        case RuntimeState::Idle:       return "Idle";
        case RuntimeState::Configured: return "Configured";
        case RuntimeState::Starting:   return "Starting";
        case RuntimeState::Running:    return "Running";
        case RuntimeState::Stopping:   return "Stopping";
        case RuntimeState::Stopped:    return "Stopped";
    }
    return "Unknown";
}

void Runtime::transition(RuntimeState from, RuntimeState to) {
    if (state_ != from) {
        LOG_ERR("FSM: illegal transition %s→%s: currently in %s",
            runtime_state_name(from), runtime_state_name(to),
            runtime_state_name(state_));
        std::abort();
    }
    LOG_INFO("FSM: %s → %s", runtime_state_name(from), runtime_state_name(to));
    state_ = to;
}

// ---------------------------------------------------------------------------
// SIGCHLD self-pipe — global write-end for the async-signal-safe handler
// ---------------------------------------------------------------------------

static int g_sigchld_pipe_wr = -1;

static void sigchld_handler(int) {
    if (g_sigchld_pipe_wr >= 0) {
        char b = 1;
        (void)write(g_sigchld_pipe_wr, &b, 1);
    }
}

namespace {

struct ChildExitInfo { pid_t pid; int exit_code; };

int push_child_exit_args(LuaContext& lua, const void* ev, Core&) {
    auto* info = static_cast<const ChildExitInfo*>(ev);
    lua.new_table();
    lua.push_integer(info->pid);
    lua.set_field(-2, "pid");
    lua.push_integer(info->exit_code);
    lua.set_field(-2, "exit_code");
    return 1;
}

int push_stop_args(LuaContext& lua, const void* ev, Core&) {
    bool exec_restart = *static_cast<const bool*>(ev);
    lua.new_table();
    lua.push_bool(exec_restart);
    lua.set_field(-2, "exec_restart");
    return 1;
}

} // namespace

namespace {

void adopt_existing_windows(Runtime& runtime, Core& core, Backend& backend) {
    auto startup = backend.scan_existing_windows();
    if (startup.windows.empty())
        return;

    int  adopted     = 0;
    bool any_restart = false;
    for (const auto& snap : startup.windows) {
        if (snap.window == NO_WINDOW)
            continue;

        if (!snap.from_restart && !snap.default_manage)
            continue;

        event::ManageWindowQuery q{ snap.window, true };
        runtime.query(q, [](const event::ManageWindowQuery& s) {
                return !s.manage;
            });
        if (!q.manage)
            continue;

        int restore_ws_id = snap.from_restart ? snap.restart_workspace_id : -1;
        (void)core.dispatch(command::EnsureWindow{
                .window       = snap.window,
                .workspace_id = restore_ws_id,
            });

        (void)core.dispatch(command::SetWindowMetadata{
                .window             = snap.window,
                .wm_instance        = snap.wm_instance,
                .wm_class           = snap.wm_class,
                .type               = snap.type,
                .hints              = {
                    .no_decorations = snap.hints.no_decorations,
                    .fixed_size     = snap.hints.fixed_size,
                },
            });

        (void)core.dispatch(command::SetWindowMapped{
                .window = snap.window,
                .mapped = snap.currently_viewable,
            });
        (void)core.dispatch(command::SetWindowHiddenByWorkspace{
                .window = snap.window,
                .hidden = true,
            });

        if (snap.from_restart) {
            if (restore_ws_id >= 0 && restore_ws_id < core.workspace_count())
                (void)core.dispatch(command::AssignWindowWorkspace{ snap.window, restore_ws_id });
            (void)core.dispatch(command::SetWindowFloating{
                    .window   = snap.window,
                    .floating = snap.restart_floating,
                });
            if (snap.restart_fullscreen)
                (void)core.dispatch(command::SetWindowFullscreen{
                        .window            = snap.window,
                        .enabled           = true,
                        .preserve_geometry = false,
                    });
            if (snap.restart_borderless) {
                (void)core.dispatch(command::SetWindowBorderless{
                        .window     = snap.window,
                        .borderless = true,
                    });
                (void)core.dispatch(command::FocusWindow{ snap.window });
            }
            if (snap.restart_hidden_explicitly)
                (void)core.dispatch(command::HideWindow{ snap.window });
        }

        if (snap.has_geometry)
            (void)core.dispatch(command::SetWindowGeometry{
                    snap.window, snap.geo_pos, snap.geo_size });

        runtime.emit(event::ApplyWindowRules{ snap.window, snap.from_restart });
        runtime.emit(event::WindowMapped{ snap.window });
        backend.on(event::WindowAdopted{ snap.window, snap.currently_viewable });

        if (snap.from_restart)
            any_restart = true;
        adopted++;
    }

    if (adopted <= 0)
        return;

    const auto& mon_ws       = startup.monitor_active_ws;
    const auto& mons         = core.monitor_states();
    bool        any_switched = false;
    if (any_restart) {
        for (int i = 0; i < (int)mons.size(); i++) {
            int  ws_id = -1;
            auto it    = mon_ws.find(i);
            if (it != mon_ws.end() && it->second >= 0 && it->second < core.workspace_count())
                ws_id = it->second;
            else
                ws_id = mons[(size_t)i].active_ws;
            if (ws_id >= 0) {
                (void)core.dispatch(command::SwitchWorkspace{ ws_id, i });
                any_switched = true;
            }
        }
    } else {
        int ws_id = -1;
        int fmon  = core.focused_monitor_index();
        if (fmon >= 0 && fmon < (int)mons.size())
            ws_id = mons[(size_t)fmon].active_ws;
        if (ws_id >= 0) {
            (void)core.dispatch(command::SwitchWorkspace{ ws_id, std::nullopt });
            any_switched = true;
        }
    }
    if (!any_switched)
        (void)core.dispatch(command::ReconcileNow{});

    LOG_INFO("adopt: restored %d existing window(s) at runtime start", adopted);
}

} // namespace

Runtime& Runtime::use(const std::string& name) {
    if (state_ >= RuntimeState::Starting)
        return *this;
    for (auto& m : modules)
        if (m && m->name() == name)
            return *this;
    auto mod = module_registry_.create(name, ModuleDeps{ *this, config_, core_ });
    if (mod) {
        mod->on_init();
        modules.push_back(std::move(mod));
    }
    return *this;
}

Module* Runtime::get_module_by_name(const std::string& name) {
    for (auto& mod : modules)
        if (mod && mod->name() == name)
            return mod.get();
    return nullptr;
}

void Runtime::emit_lua_init() {
    for (auto& m : modules)
        m->lua_init_once();
}

void Runtime::reset_lua_init() {
    for (auto& m : modules)
        m->lua_init_reset();
}

namespace {

CoreSettings make_core_settings_from_config(const Config& cfg) {
    CoreSettings out;
    out.monitor_aliases = cfg.get_monitor_aliases();
    out.monitor_compose = cfg.get_monitor_compose();
    out.workspace_defs  = cfg.get_workspace_defs();
    out.theme           = cfg.get_theme();
    return out;
}

} // namespace

bool Runtime::load_config(const std::string& path) {
    if (!config_.load(path, *this))
        return false;

    auto errs = config_.validate();
    if (!errs.empty()) {
        for (auto& e : errs)
            LOG_ERR("Config: %s", e.c_str());
        return false;
    }

    core_.apply_settings(make_core_settings_from_config(config_));
    return true;
}

void Runtime::start() {
    if (!backend_) {
        LOG_ERR("Runtime::start() called before backend is bound"); std::abort();
    }

    // Query initial monitor list from backend and init core.
    {
        std::vector<Monitor> initial_monitors;
        if (auto* mp = backend_->monitor_port())
            initial_monitors = mp->get_monitors();
        else
            LOG_WARN("backend has no monitor_port — starting with empty monitor list");
        core_.init(std::move(initial_monitors));
    }

    // Let the backend supply a custom Window factory (for subclassing).
    core_.set_window_factory([this](WindowId id) {
            return backend_->create_window(id);
        });

    core_.mark_runtime_started(true);

    // Infrastructure: SIGCHLD reaping and monitor topology — set up before
    // modules start so they can rely on both.
    setup_sigchld_pipe();
    apply_and_refresh_monitors();
    if (auto* mp = backend_->monitor_port())
        mp->select_change_events();

    for (auto& mod : modules) {
        mod->bind_backend(*backend_);
        mod->on_start();
    }
    drain_core_events();
    backend_->on_start(core_);
    adopt_existing_windows(*this, core_, *backend_);
    drain_core_events();
    emit(event::RuntimeStarted{});
}

void Runtime::stop(bool is_exec_restart) {
    emit_to_lua("stop", push_stop_args, &is_exec_restart);
    for (auto it = modules.rbegin(); it != modules.rend(); ++it)
        (*it)->on_stop(is_exec_restart);
    teardown_sigchld_pipe();
    core_.mark_runtime_started(false);
}

void Runtime::reload() {
    for (auto& mod : modules)
        mod->on_reload();
}

void Runtime::emit_to_lua(const char* event,
    LuaEventPushFn push_args,
    const void* ev) {
    config_.lua().emit_to_lua(event, push_args, ev, &core_);
}

void Runtime::apply_and_refresh_monitors() {
    auto* mp = backend_ ? backend_->monitor_port() : nullptr;
    if (!mp) {
        LOG_ERR("monitors: backend does not provide MonitorPort");
        return;
    }

    mp->apply_monitor_layout(monitor_layout::build(config_));

    auto monitors = mp->get_monitors();
    LOG_INFO("monitors: found %d monitor(s):", (int)monitors.size());
    for (auto& m : monitors)
        LOG_INFO("  %s: %dx%d+%d+%d", m.name.c_str(), m.width(), m.height(), m.x(), m.y());

    (void)core_.dispatch(command::ApplyMonitorTopology{ std::move(monitors) });
}

void Runtime::dispatch_display_change() {
    if (backend_)
        apply_and_refresh_monitors();
}

void Runtime::setup_sigchld_pipe() {
    int fds[2];
    if (pipe(fds) < 0) {
        LOG_ERR("runtime: pipe() failed for SIGCHLD self-pipe");
        return;
    }
    sigchld_pipe_rd_ = fds[0];
    sigchld_pipe_wr_ = fds[1];
    fcntl(sigchld_pipe_rd_, F_SETFL, O_NONBLOCK);
    fcntl(sigchld_pipe_rd_, F_SETFD, FD_CLOEXEC);
    fcntl(sigchld_pipe_wr_, F_SETFD, FD_CLOEXEC);

    g_sigchld_pipe_wr = sigchld_pipe_wr_;
    struct sigaction sa {};
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa, nullptr);

    watch_fd(sigchld_pipe_rd_, [this]() {
            reap_children();
        });
}

void Runtime::teardown_sigchld_pipe() {
    signal(SIGCHLD, SIG_DFL);
    g_sigchld_pipe_wr = -1;
    if (sigchld_pipe_rd_ >= 0) {
        close(sigchld_pipe_rd_); sigchld_pipe_rd_ = -1;
    }
    if (sigchld_pipe_wr_ >= 0) {
        close(sigchld_pipe_wr_); sigchld_pipe_wr_ = -1;
    }
}

void Runtime::reap_children() {
    // Drain the self-pipe.
    char buf[64];
    while (read(sigchld_pipe_rd_, buf, sizeof(buf)) > 0) {
    }

    int   status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        int           exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
        ChildExitInfo info { pid, exit_code };
        if (backend_)
            emit_to_lua("child_exit", push_child_exit_args, &info);
    }
}

void Runtime::watch_fd(int fd, std::function<void()> cb) {
    watched_fds.push_back({ fd, std::move(cb) });
    if (epoll_fd_ >= 0) {
        struct epoll_event ev = {};
        ev.events  = EPOLLIN;
        ev.data.fd = fd;
        epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev);
    }
}

void Runtime::unwatch_fd(int fd) {
    if (epoll_fd_ >= 0)
        epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
    watched_fds.erase(
        std::remove_if(watched_fds.begin(), watched_fds.end(),
        [fd](const WatchedFd& w) {
            return w.fd == fd;
        }),
        watched_fds.end());
}

void Runtime::dispatch_ready_fds(struct epoll_event* events, int count) {
    for (int i = 0; i < count; ++i) {
        int fd = events[i].data.fd;
        for (auto& w : watched_fds)
            if (w.fd == fd) {
                w.cb(); break;
            }
    }
}

void Runtime::drain_core_events() {
    auto events = core_.take_core_events();
    for (const auto& ev : events) {
        std::visit([&](const auto& e) {
                emit(e);
                if (backend_)
                    backend_->on(e);
            }, ev);
    }
}

Backend& Runtime::backend() {
    if (!backend_) {
        LOG_ERR("Runtime::backend() called before backend is bound"); std::abort();
    }
    return *backend_;
}

const Backend& Runtime::backend() const {
    if (!backend_) {
        LOG_ERR("Runtime::backend() called before backend is bound"); std::abort();
    }
    return *backend_;
}

void Runtime::tick() {
    constexpr std::size_t kMaxBackendEventsPerTick = 2048;
    constexpr int         kMaxEpollEvents          = 16;

    struct epoll_event ready[kMaxEpollEvents];
    int n = epoll_wait(epoll_fd_, ready, kMaxEpollEvents, 100);

    dispatch_ready_fds(ready, n);
    backend_->pump_events(kMaxBackendEventsPerTick);

    bool reloaded = process_pending_reload();
    if (reloaded)
        backend_->on_reload_applied();

    drain_core_events();

    if (reloaded)
        emit(event::RaiseDocks{});

    backend_->render_frame();
}

void Runtime::run_loop() {
    epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd_ < 0) {
        LOG_ERR("epoll_create1 failed"); std::abort();
    }

    // Register backend fd.
    {
        struct epoll_event ev = {};
        ev.events  = EPOLLIN;
        ev.data.fd = backend_->event_fd();
        epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, backend_->event_fd(), &ev);
    }

    // Register already-watched fds (sigchld pipe, module timers, etc.).
    for (auto& w : watched_fds) {
        struct epoll_event ev = {};
        ev.events  = EPOLLIN;
        ev.data.fd = w.fd;
        epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, w.fd, &ev);
    }

    while (!stop_requested)
        tick();

    close(epoll_fd_);
    epoll_fd_ = -1;

    LOG_INFO("Runtime: event loop stopped");
}

void Runtime::run(const std::string& config_path,
                   const std::string& fallback_config_path) {
    while (true) {
        switch (state_) {
            case RuntimeState::Idle:
                if (load_config(config_path)) {
                    transition(RuntimeState::Idle, RuntimeState::Configured);
                } else if (!fallback_config_path.empty()) {
                    LOG_WARN("Failed to load %s — trying fallback %s",
                        config_path.c_str(), fallback_config_path.c_str());
                    if (load_config(fallback_config_path)) {
                        transition(RuntimeState::Idle, RuntimeState::Configured);
                    } else {
                        LOG_ERR("FSM: fallback config also failed — aborting");
                        state_ = RuntimeState::Stopped;
                    }
                } else {
                    LOG_ERR("FSM: aborting — failed to load config %s", config_path.c_str());
                    state_ = RuntimeState::Stopped;
                }
                break;

            case RuntimeState::Configured:
                transition(RuntimeState::Configured, RuntimeState::Starting);
                start();
                transition(RuntimeState::Starting, RuntimeState::Running);
                break;

            case RuntimeState::Starting:
                LOG_ERR("FSM: unexpected Starting state in run() — programming error");
                std::abort();

            case RuntimeState::Running:
                stop_requested = false;
                run_loop();
                transition(RuntimeState::Running, RuntimeState::Stopping);
                stop(exec_restart_pending.load());
                transition(RuntimeState::Stopping, RuntimeState::Stopped);
                break;

            case RuntimeState::Stopping:
                // Transient — stop() drives this to Stopped synchronously.
                LOG_ERR("FSM: unexpected Stopping state in run() — programming error");
                std::abort();

            case RuntimeState::Stopped:
                return;
        }
    }
}

void Runtime::save_restart_state() {
    auto path = restart_state_path();

    // Create (or truncate) with 0600 so only the owning user can read/write.
    int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) {
        LOG_WARN("restart: cannot create state file %s", path.c_str());
        return;
    }
    close(fd);

    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) {
        LOG_WARN("restart: cannot write state file %s", path.c_str());
        return;
    }

    // Save active workspace per monitor: "MON <idx> <active_ws>"
    const auto& mons = core_.monitor_states();
    for (int i = 0; i < (int)mons.size(); i++)
        out << "MON " << i << " " << mons[(size_t)i].active_ws << "\n";

    // Save per-window state: "WINDOW <win_id> <ws_id> <floating> <fullscreen> <hidden_explicitly>"
    std::unordered_set<WindowId> seen;
    int                          saved_windows = 0;
    for (auto id : core_.all_window_ids()) {
        if (!seen.insert(id).second)
            continue;
        int ws_id = core_.workspace_of_window(id);
        if (ws_id < 0)
            continue;
        auto ws = core_.window_state_any(id);
        // Skip self-managed borderless (Proton/Wine service windows) — recreated by the app.
        if (ws && ws->borderless && ws->self_managed)
            continue;
        int fs = core_.is_window_fullscreen(id) ? 1 : 0;
        int he = (ws && ws->hidden_explicitly) ? 1 : 0;
        int bl = (ws && ws->borderless) ? 1 : 0;
        out << "WINDOW " << id << " " << ws_id
            << " " << (core_.is_window_floating(id) ? 1 : 0)
            << " " << fs
            << " " << he
            << " " << bl
            << "\n";
        saved_windows++;
    }

    LOG_INFO("restart: saved %d monitor(s) + %d window(s) to %s",
        (int)mons.size(), saved_windows, restart_state_path().c_str());
}

void Runtime::request_exec_restart() {
    const auto& path = core_.get_config_path();
    if (!path.empty() && !Config::check_syntax(path)) {
        LOG_ERR("restart: aborted — syntax error in %s", path.c_str());
        return;
    }
    save_restart_state();
    exec_restart_pending = true;
    request_stop();
}

bool Runtime::consume_exec_restart_request() {
    return exec_restart_pending.exchange(false);
}

bool Runtime::reload_runtime_config() {
    const auto& config_path = core_.get_config_path();
    if (config_path.empty()) {
        LOG_ERR("reload: aborted — config_path is empty");
        return false;
    }

    if (!Config::check_syntax(config_path)) {
        LOG_ERR("reload: aborted — syntax error in %s, config unchanged", config_path.c_str());
        return false;
    }

    auto config_backup      = config_.snapshot();
    auto core_reload_backup = core_.snapshot_reload_state();

    auto restore_snapshot = [&]() {
            config_.restore(config_backup);
            core_.restore_reload_state(core_reload_backup);
        };

    config_.clear();
    core_.clear_reloadable_runtime_state();

    if (!config_.load(config_path, *this, /*reset_lua_vm=*/ false)) {
        restore_snapshot();
        LOG_ERR("reload: aborted — failed to execute %s", config_path.c_str());
        return false;
    }

    auto errs = config_.validate();
    if (!errs.empty()) {
        for (auto& e : errs)
            LOG_ERR("Config(reload): %s", e.c_str());
        restore_snapshot();
        LOG_ERR("reload: aborted — validation failed, previous config restored");
        return false;
    }

    core_.apply_settings(make_core_settings_from_config(config_));
    reload();
    return true;
}

void Runtime::request_reload() {
    reload_pending = true;
}

void Runtime::request_soft_restart() {
    reload_pending       = false;
    soft_restart_pending = true;
}

Runtime::ReloadRequest Runtime::consume_reload_request() {
    if (soft_restart_pending.exchange(false))
        return ReloadRequest::SoftRestart;
    if (reload_pending.exchange(false))
        return ReloadRequest::ReloadConfig;
    return ReloadRequest::None;
}

bool Runtime::process_pending_reload() {
    ReloadRequest req = consume_reload_request();
    if (req == ReloadRequest::None)
        return false;

    if (req == ReloadRequest::SoftRestart)
        LOG_INFO("restart: deferred in-process reload");
    else
        LOG_INFO("reload: deferred config reload");

    bool reload_ok = reload_runtime_config();

    if (reload_ok) {
        // Snapshot active workspace per monitor before display change resets layout.
        std::vector<int> saved_ws;
        {
            const auto& mons = core_.monitor_states();
            saved_ws.resize(mons.size(), -1);
            for (int i = 0; i < (int)mons.size(); i++)
                saved_ws[(size_t)i] = mons[(size_t)i].active_ws;
        }

        dispatch_display_change();

        // Restore active workspace on every monitor.
        const auto& mons = core_.monitor_states();
        bool        any  = false;
        for (int i = 0; i < (int)mons.size() && i < (int)saved_ws.size(); i++) {
            if (saved_ws[(size_t)i] >= 0 && saved_ws[(size_t)i] < core_.workspace_count()) {
                (void)core_.dispatch(command::SwitchWorkspace{ saved_ws[(size_t)i], i });
                any = true;
            }
        }
        if (!any)
            (void)core_.dispatch(command::ReconcileNow{});

        emit_to_lua("reload", nullptr, nullptr);
    }

    return true;
}
