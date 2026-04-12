#include <runtime.hpp>

#include <backend/backend.hpp>
#include <backend/commands.hpp>
#include <backend/render_port.hpp>
#include <backend/tray_host.hpp>
#include <backend/tray_host_port.hpp>
#include <config_loader.hpp>
#include <surface.hpp>
#include <core.hpp>
#include <module_registry.hpp>
#include <monitor_layout.hpp>
#include <log.hpp>
#include <string_utils.hpp>
#include <bar_config.hpp>

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
// Runtime constructor
// ---------------------------------------------------------------------------

Runtime::Runtime(ModuleRegistry& module_registry)
    : module_registry_(module_registry)
{
    config_runtime::register_core_config(core_config_, store_);
}

// ---------------------------------------------------------------------------
// SIGCHLD self-pipe — global write-end for the async-signal-safe handler
// ---------------------------------------------------------------------------

static int g_sigchld_pipe_wr = -1;

static void sigchld_handler(int) {
    if (g_sigchld_pipe_wr >= 0) {
        char    b = 1;
        ssize_t r;
        do {
            r = write(g_sigchld_pipe_wr, &b, 1);
        } while (r < 0 && errno == EINTR);
    }
}

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

        if (!runtime.invoke_hook(hook::ShouldManageWindow{ snap.window }).manage)
            continue;

        int restore_ws_id = snap.from_restart ? snap.restart_workspace_id : -1;
        (void)core.dispatch(command::atom::EnsureWindow{
                .window       = snap.window,
                .workspace_id = restore_ws_id,
            });

        command::WindowHints hints{};
        hints.no_decorations = snap.hints.no_decorations;
        hints.fixed_size     = snap.hints.fixed_size;
        (void)core.dispatch(command::atom::SetWindowMetadata{
                .window      = snap.window,
                .wm_instance = snap.wm_instance,
                .wm_class    = snap.wm_class,
                .title       = {},
                .pid         = 0,
                .type        = snap.type,
                .hints       = hints,
            });

        (void)core.dispatch(command::atom::SetWindowMapped{
                .window = snap.window,
                .mapped = snap.currently_viewable,
            });
        (void)core.dispatch(command::atom::SetWindowHiddenByWorkspace{
                .window = snap.window,
                .hidden = true,
            });

        if (snap.from_restart) {
            if (restore_ws_id >= 0 && restore_ws_id < core.workspace_count())
                (void)core.dispatch(command::atom::AssignWindowWorkspace{ snap.window, restore_ws_id });
            (void)core.dispatch(command::atom::SetWindowFloating{
                    .window   = snap.window,
                    .floating = snap.restart_floating,
                });
            if (snap.restart_fullscreen)
                (void)core.dispatch(command::atom::SetWindowFullscreen{
                        .window            = snap.window,
                        .enabled           = true,
                        .preserve_geometry = false,
                    });
            if (snap.restart_borderless) {
                (void)core.dispatch(command::atom::SetWindowBorderless{
                        .window     = snap.window,
                        .borderless = true,
                    });
                (void)core.dispatch(command::atom::FocusWindow{ snap.window });
            }
            if (snap.restart_hidden_explicitly)
                (void)core.dispatch(command::atom::HideWindow{ snap.window });
        }

        if (snap.has_geometry)
            (void)core.dispatch(command::atom::SetWindowGeometry{
                    snap.window, snap.geo_pos, snap.geo_size });

        runtime.invoke_hook(hook::WindowRules{ snap.window, snap.from_restart });
        runtime.post_event(event::WindowMapped{ snap.window });
        runtime.post_event(event::WindowAdopted{ snap.window, snap.currently_viewable });

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
                (void)core.dispatch(command::atom::SwitchWorkspace{ ws_id, i });
                any_switched = true;
            }
        }
    } else {
        int ws_id = -1;
        int fmon  = core.focused_monitor_index();
        if (fmon >= 0 && fmon < (int)mons.size())
            ws_id = mons[(size_t)fmon].active_ws;
        if (ws_id >= 0) {
            (void)core.dispatch(command::atom::SwitchWorkspace{ ws_id, std::nullopt });
            any_switched = true;
        }
    }
    if (!any_switched)
        (void)core.dispatch(command::atom::ReconcileNow{});

    LOG_INFO("adopt: restored %d existing window(s) at runtime start", adopted);
}

} // namespace

Runtime& Runtime::use(const std::string& name) {
    if (state_ >= RuntimeState::Starting)
        return *this;
    for (auto& m : modules)
        if (m && m->name() == name)
            return *this;
    auto mod = module_registry_.create(name, ModuleDeps{ *this, core_ });
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

CoreSettings Runtime::build_core_settings() const {
    return CoreSettings{
        core_config_.monitors.get(),
        core_config_.compose.get(),
        core_config_.workspaces.get(),
        core_config_.theme.get(),
    };
}

std::vector<std::string> Runtime::validate_settings() const {
    std::vector<std::string> errs;

    // Per-setting validation.
    auto per = store_.validate_all();
    errs.insert(errs.end(), per.begin(), per.end());

    // --- Core settings cross-checks ---

    const auto& monitors   = core_config_.monitors.get();
    const auto& compose    = core_config_.compose.get();
    const auto& workspaces = core_config_.workspaces.get();
    const auto& theme      = core_config_.theme.get();

    auto        has_alias = [&](const std::string& alias) {
            for (auto& m : monitors)
                if (m.alias == alias)
                    return true;
            return false;
        };

    // Modifier
    if (auto* s = store_.find("modifier")) {
        auto* ts = dynamic_cast<const TypedSetting<std::optional<uint16_t>>*>(s);
        if (!ts || !ts->get().has_value())
            errs.push_back("modifier not set — use siren.modifier = 'mod4' (or shift/ctrl/alt/mod1..mod5)");
    } else {
        errs.push_back("modifier not set — use siren.modifier = 'mod4' (or shift/ctrl/alt/mod1..mod5)");
    }

    // Workspaces
    if (workspaces.empty())
        errs.push_back("no workspaces defined — set siren.workspaces = {...}");

    // Monitors
    if (monitors.empty()) {
        errs.push_back("no monitors defined — set siren.monitors = {...}");
    } else {
        std::unordered_set<std::string> aliases_seen;
        std::unordered_set<std::string> outputs_seen;
        for (auto& m : monitors) {
            if (m.alias.empty())
                errs.push_back("monitor: 'name' must be non-empty");
            if (m.output.empty())
                errs.push_back("monitor '" + m.alias + "': 'output' must be non-empty");
            if (m.width <= 0 || m.height <= 0)
                errs.push_back("monitor '" + m.alias + "': width/height must be > 0");
            if (!is_valid_rotation(m.rotation))
                errs.push_back("monitor '" + m.alias + "': rotation must be one of normal/left/right/inverted");
            if (!aliases_seen.insert(m.alias).second)
                errs.push_back("duplicate monitor alias '" + m.alias + "'");
            if (!outputs_seen.insert(m.output).second)
                errs.push_back("duplicate output mapping '" + m.output + "'");
        }
    }

    // Compose
    if (!compose.defined) {
        errs.push_back("monitor composition not defined — set siren.compose_monitors = {...}");
    } else {
        auto valid_side = [](const std::string& s) {
                return s == "left" || s == "right" || s == "top" || s == "bottom";
            };
        if (compose.primary.empty())
            errs.push_back("compose_monitors: 'primary' must be set");

        std::unordered_map<std::string, const MonitorComposeLink*> by_monitor;
        for (auto& l : compose.layout) {
            if (l.monitor.empty())
                errs.push_back("compose_monitors.layout: 'monitor' must be non-empty");
            if (!has_alias(l.monitor))
                errs.push_back("compose_monitors.layout: unknown monitor alias '" + l.monitor + "'");
            if (by_monitor.count(l.monitor))
                errs.push_back("compose_monitors.layout: duplicate entry for '" + l.monitor + "'");
            else
                by_monitor[l.monitor] = &l;
            if (!l.side.empty() && !valid_side(l.side))
                errs.push_back("compose_monitors.layout: monitor '" + l.monitor +
                    "' has invalid side '" + l.side + "'");
        }

        if (!compose.primary.empty() && !has_alias(compose.primary))
            errs.push_back("compose_monitors: primary alias '" + compose.primary +
                "' not found in siren.monitors");

        for (auto& m : monitors) {
            if (!m.enabled)
                continue;
            if (m.alias == compose.primary)
                continue;
            if (!by_monitor.count(m.alias))
                errs.push_back("compose_monitors.layout: missing entry for enabled monitor '" + m.alias + "'");
        }

        std::unordered_map<std::string, int>    state;
        std::function<void(const std::string&)> dfs = [&](const std::string& name) {
                if (state[name] == 2)
                    return;
                if (state[name] == 1) {
                    errs.push_back("compose_monitors: cycle detected at '" + name + "'");
                    return;
                }
                state[name] = 1;
                auto it = by_monitor.find(name);
                if (it == by_monitor.end()) {
                    if (name == compose.primary) {
                        state[name] = 2;
                        return;
                    }
                    errs.push_back("compose_monitors: missing layout entry for '" + name + "'");
                    state[name] = 2;
                    return;
                }
                auto* link = it->second;
                if (name == compose.primary) {
                    if (!link->relative_to.empty())
                        errs.push_back("compose_monitors: primary monitor '" + name + "' must not have relative_to");
                    if (!link->side.empty())
                        errs.push_back("compose_monitors: primary monitor '" + name + "' must not have side");
                } else {
                    if (link->relative_to.empty())
                        errs.push_back("compose_monitors: monitor '" + name + "' must set relative_to");
                    if (link->side.empty())
                        errs.push_back("compose_monitors: monitor '" + name + "' must set side");
                }
                if (!link->relative_to.empty()) {
                    if (!has_alias(link->relative_to)) {
                        errs.push_back("compose_monitors: monitor '" + name + "' references unknown relative_to '" +
                            link->relative_to + "'");
                    } else if (link->relative_to == name) {
                        errs.push_back("compose_monitors: monitor '" + name + "' cannot reference itself");
                    } else {
                        dfs(link->relative_to);
                    }
                }
                state[name] = 2;
            };

        for (auto& m : monitors) {
            if (m.enabled)
                dfs(m.alias);
        }
    }

    // Workspace → monitor refs
    for (auto& ws : workspaces) {
        if (ws.monitor.empty())
            continue;
        if (!has_alias(ws.monitor))
            errs.push_back("workspace '" + ws.name + "': unknown monitor alias '" + ws.monitor + "'");
    }

    // Bar → theme cross-checks
    auto*       bar_s        = store_.find("bar");
    auto*       bottom_bar_s = store_.find("bottom_bar");
    const auto* bar_ts       = bar_s
        ? dynamic_cast<const TypedSetting<std::optional<BarConfig>>*>(bar_s) : nullptr;
    const auto* bottom_bar_ts = bottom_bar_s
        ? dynamic_cast<const TypedSetting<std::optional<BarConfig>>*>(bottom_bar_s) : nullptr;
    bool        has_bar    = (bar_ts && bar_ts->get().has_value());
    bool        has_bottom = (bottom_bar_ts && bottom_bar_ts->get().has_value());

    if (has_bar) {
        if (bar_ts->get()->font.empty() && theme.font.empty())
            errs.push_back("bar: 'font' is required (set in bar.top.font or theme.font)");
    }

    if (has_bar || has_bottom) {
        if (theme.bg.empty())     errs.push_back("theme: 'bg' is required when bar is configured");
        if (theme.fg.empty())     errs.push_back("theme: 'fg' is required when bar is configured");
        if (theme.alt_bg.empty()) errs.push_back("theme: 'alt_bg' is required when bar is configured");
        if (theme.alt_fg.empty()) errs.push_back("theme: 'alt_fg' is required when bar is configured");
        if (theme.accent.empty()) errs.push_back("theme: 'accent' is required when bar is configured");
    }

    return errs;
}

bool Runtime::load_config(const std::string& path) {
    if (!config_loader::load(path, core_, *this, lua_host_, /*reset_lua_vm=*/ true))
        return false;

    auto errs = validate_settings();
    if (!errs.empty()) {
        for (auto& e : errs)
            LOG_ERR("Config: %s", e.c_str());
        return false;
    }

    core_.apply_settings(build_core_settings());
    return true;
}

void Runtime::start() {
    if (!backend_) {
        LOG_ERR("Runtime::start() called before backend is bound"); std::abort();
    }

    // Wire up the unified event pipeline:
    //  - Core pushes domain events into the Runtime queue via this sink.
    //  - Backend subscribes as a receiver to hear domain events after Core.
    core_.set_event_sink(this);
    add_receiver(backend_);
    add_hook_receiver(&lua_host_);
    add_hook_receiver(backend_);

    // Query initial monitor list from backend and init core.
    {
        std::vector<Monitor> initial_monitors = ports().monitor.get_monitors();
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
    ports().monitor.select_change_events();

    for (auto& mod : modules) {
        mod->on_start();
    }
    drain_events();
    backend_->on_start(core_);
    adopt_existing_windows(*this, core_, *backend_);
    drain_events();
    post_event(event::RuntimeStarted{});
    drain_events();
}

void Runtime::stop(bool is_exec_restart) {
    post_event(event::RuntimeStopping{ is_exec_restart });
    drain_events();
    if (backend_) {
        remove_receiver(backend_);
        backend_->shutdown();
    }
    core_.set_event_sink(nullptr);
    for (auto it = modules.rbegin(); it != modules.rend(); ++it)
        (*it)->on_stop(is_exec_restart);
    if (!surface_registry_.empty())
        LOG_WARN("Runtime::stop: %zu Surface(s) still alive at shutdown",
            surface_registry_.size());
    teardown_sigchld_pipe();
    core_.mark_runtime_started(false);
}

void Runtime::reload() {
    for (auto& mod : modules)
        mod->on_reload();
}

void Runtime::add_receiver(IEventReceiver* receiver) {
    if (!receiver) return;
    for (auto* r : extra_receivers_)
        if (r == receiver) return;
    extra_receivers_.push_back(receiver);
}

void Runtime::remove_receiver(IEventReceiver* receiver) {
    std::erase(extra_receivers_, receiver);
}

void Runtime::drain_events() {
    event_queue_.drain([&](QueuedEvent& ev) {
            for (auto& m : modules)
                ev.deliver(*m);
            ev.deliver(lua_host_);
            for (auto* r : extra_receivers_)
                ev.deliver(*r);
        });
}

Surface* Runtime::resolve_surface(WindowId win) {
    auto it = surface_by_id_.find(win);
    return it != surface_by_id_.end() ? it->second : nullptr;
}

void Runtime::apply_and_refresh_monitors() {
    auto& mp = ports().monitor;
    mp.apply_monitor_layout(monitor_layout::build(
        core_config_.monitors.get(), core_config_.compose.get()));

    auto monitors = mp.get_monitors();
    LOG_INFO("monitors: found %d monitor(s):", (int)monitors.size());
    for (auto& m : monitors)
        LOG_INFO("  %s: %dx%d+%d+%d", m.name.c_str(), m.width(), m.height(), m.x(), m.y());

    (void)core_.dispatch(command::atom::ApplyMonitorTopology{ std::move(monitors) });
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
    if (fcntl(sigchld_pipe_rd_, F_SETFL, O_NONBLOCK) < 0 ||
        fcntl(sigchld_pipe_rd_, F_SETFD, FD_CLOEXEC) < 0 ||
        fcntl(sigchld_pipe_wr_, F_SETFD, FD_CLOEXEC) < 0) {
        LOG_ERR("runtime: fcntl() failed on SIGCHLD pipe");
        close(sigchld_pipe_rd_); sigchld_pipe_rd_ = -1;
        close(sigchld_pipe_wr_); sigchld_pipe_wr_ = -1;
        return;
    }

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
    // Drain the self-pipe (O_NONBLOCK: stops at EAGAIN/EWOULDBLOCK).
    char    buf[64];
    ssize_t n;
    do {
        n = read(sigchld_pipe_rd_, buf, sizeof(buf));
    } while (n > 0 || (n < 0 && errno == EINTR));

    int   status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
        if (backend_)
            post_event(event::ChildExited{ pid, exit_code });
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
    std::erase_if(watched_fds, [fd](const WatchedFd& w) { return w.fd == fd; });
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

backend::BackendPorts Runtime::ports() {
    if (!backend_) {
        LOG_ERR("Runtime::ports() called before backend is bound"); std::abort();
    }
    return backend_->ports();
}

void Runtime::prepare_exec_restart() {
    if (!backend_) {
        LOG_ERR("Runtime::prepare_exec_restart() called before backend is bound");
        std::abort();
    }
    backend_->prepare_exec_restart();
}

// ---------------------------------------------------------------------------
// Surface & tray factories
// ---------------------------------------------------------------------------

std::unique_ptr<Surface> Runtime::create_surface(const SurfaceCreateInfo& info) {
    if (!backend_) {
        LOG_ERR("Runtime::create_surface() called before backend is bound");
        return nullptr;
    }

    backend::RenderWindowCreateInfo rw{};
    rw.monitor_index           = info.monitor_index;
    rw.pos                     = info.pos;
    rw.size                    = info.size;
    rw.background_pixel        = ports().render.black_pixel();
    rw.want_expose             = info.want_expose;
    rw.want_button_press       = info.want_button_press;
    rw.want_button_release     = info.want_button_release;
    rw.hints.override_redirect = true;
    rw.hints.dock              = info.dock;
    rw.hints.keep_above        = info.keep_above;

    auto window = ports().render.create_window(rw);
    if (!window)
        return nullptr;

    std::unique_ptr<Surface> s(new Surface(*this, std::move(window)));
    surface_registry_.insert(s.get());
    surface_by_id_[s->id()] = s.get();
    return s;
}

std::unique_ptr<backend::TrayHost>
Runtime::create_tray(Surface& owner, bool own_selection) {
    if (!backend_) {
        LOG_ERR("Runtime::create_tray() called before backend is bound");
        return nullptr;
    }
    auto* port = ports().tray_host;
    if (!port)
        return nullptr;

    auto* bw = owner.backend_window();
    if (!bw)
        return nullptr;

    return port->create(bw->id(), bw->x(), bw->y(), bw->height(), own_selection);
}

void Runtime::unregister_surface(Surface* s) {
    surface_registry_.erase(s);
    if (s)
        surface_by_id_.erase(s->id());
}

void Runtime::tick() {
    constexpr std::size_t kMaxBackendEventsPerTick = 2048;
    constexpr int         kMaxEpollEvents          = 16;

    struct epoll_event    ready[kMaxEpollEvents];
    int                   n = epoll_wait(epoll_fd_, ready, kMaxEpollEvents, 100);

    dispatch_ready_fds(ready, n);
    backend_->pump_events(kMaxBackendEventsPerTick);

    bool reloaded = process_pending_reload();
    if (reloaded) {
        backend_->on_reload_applied();
        post_event(event::RaiseDocks{});
    }

    drain_events();

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
    if (!path.empty() && !config_loader::check_syntax(path)) {
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

    if (!config_loader::check_syntax(config_path)) {
        LOG_ERR("reload: aborted — syntax error in %s, config unchanged", config_path.c_str());
        return false;
    }

    // Snapshot store for transactional rollback.
    store_.snapshot_all();
    auto core_reload_backup = core_.snapshot_reload_state();

    auto restore_snapshot = [&]() {
            store_.rollback_all();
            core_.restore_reload_state(core_reload_backup);
        };

    store_.clear_all();
    core_.clear_reloadable_runtime_state();

    if (!config_loader::load(config_path, core_, *this, lua_host_, /*reset_lua_vm=*/ false)) {
        restore_snapshot();
        LOG_ERR("reload: aborted — failed to execute %s", config_path.c_str());
        return false;
    }

    auto errs = validate_settings();
    if (!errs.empty()) {
        for (auto& e : errs)
            LOG_ERR("Config(reload): %s", e.c_str());
        restore_snapshot();
        LOG_ERR("reload: aborted — validation failed, previous config restored");
        return false;
    }

    store_.commit_all();
    core_.apply_settings(build_core_settings());
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
                (void)core_.dispatch(command::atom::SwitchWorkspace{ saved_ws[(size_t)i], i });
                any = true;
            }
        }
        if (!any)
            (void)core_.dispatch(command::atom::ReconcileNow{});

        post_event(event::ConfigReloaded{});
    }

    return true;
}
