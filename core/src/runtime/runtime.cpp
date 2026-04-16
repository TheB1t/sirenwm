#include <runtime/runtime.hpp>

#include <backend/backend.hpp>
#include <backend/commands.hpp>
#include <backend/render_port.hpp>
#include <backend/tray_host.hpp>
#include <backend/tray_host_port.hpp>
#include <config/config_loader.hpp>
#include <domain/core.hpp>
#include <runtime/module_registry.hpp>
#include <domain/monitor_layout.hpp>
#include <support/log.hpp>
#include <support/string_utils.hpp>
#include <config/bar_config.hpp>

#include <cstdlib>
#include <csignal>
#include <exception>
#include <fcntl.h>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <unistd.h>
#include <sys/epoll.h>
#include <runtime/restart_state.hpp>
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
    if (!runtime_transition_allowed(from, to)) {
        LOG_ERR("FSM: transition edge %s→%s is not allowed by lifecycle table",
            runtime_state_name(from), runtime_state_name(to));
        std::abort();
    }
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

Runtime::Runtime(BackendFactory backend_factory)
{
    module_registry_static::apply_static_registrations(module_registry_);
    config_runtime::register_core_config(core_config_, store_);

    if (!backend_factory) {
        throw std::invalid_argument("Runtime: backend factory is required");
    }
    try {
        backend_ = backend_factory(core_, *this);
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Runtime: backend factory threw: ") + e.what());
    } catch (...) {
        throw std::runtime_error("Runtime: backend factory threw non-standard exception");
    }
    if (!backend_) {
        throw std::runtime_error("Runtime: backend factory returned null");
    }
}

Runtime::~Runtime() {
    modules.clear();
    module_windows_by_id_.clear();
}

void Runtime::RenderWindowDeleter::operator()(backend::RenderWindow* window) const {
    if (!window)
        return;
    if (runtime)
        runtime->unregister_module_window(*window);
    delete window;
}

// ---------------------------------------------------------------------------
// SIGCHLD self-pipe — global write-end for the async-signal-safe handler
// ---------------------------------------------------------------------------

static int g_sigchld_pipe_wr = -1;

static void sigchld_handler(int) {
    int saved_errno = errno;
    if (g_sigchld_pipe_wr >= 0) {
        char    b = 1;
        ssize_t r;
        do {
            r = write(g_sigchld_pipe_wr, &b, 1);
        } while (r < 0 && errno == EINTR);
    }
    errno = saved_errno;
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

        WorkspaceId restore_ws_id = snap.from_restart ? snap.restart_workspace_id : NO_WORKSPACE;
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
                .title       = snap.title,
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
            MonitorId   mi    = MonitorId{ i };
            WorkspaceId ws_id = NO_WORKSPACE;
            auto        it    = mon_ws.find(mi);
            if (it != mon_ws.end() && it->second >= 0 && it->second < core.workspace_count())
                ws_id = it->second;
            else
                ws_id = mons[(size_t)i].active_ws;
            if (ws_id >= 0) {
                (void)core.dispatch(command::atom::SwitchWorkspace{ ws_id, mi });
                any_switched = true;
            }
        }
    } else {
        WorkspaceId ws_id = NO_WORKSPACE;
        MonitorId   fmon  = core.focused_monitor_index();
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
    if (const auto* bar_set = store_.find_typed<BarSetConfig>("bar_set")) {
        auto has_content = [](const BarConfig& cfg) {
                return !cfg.left.empty() || !cfg.center.empty() || !cfg.right.empty();
            };
        auto check_side = [&](const BarSide& side, bool& any_bar, bool& missing_font) {
                if (side.state != BarSideState::Custom || !has_content(side.cfg))
                    return;
                any_bar = true;
                if (side.cfg.font.empty() && theme.font.empty())
                    missing_font = true;
            };

        bool any_bar      = false;
        bool missing_font = false;

        for (const auto& mon : monitors) {
            auto cfg = bar_set->get().resolve(mon.alias);
            check_side(cfg.top, any_bar, missing_font);
            check_side(cfg.bottom, any_bar, missing_font);
        }

        if (missing_font)
            errs.push_back("bar: 'font' is required (set in bar.settings.*.font or theme.font)");

        if (any_bar) {
            if (theme.bg.empty())     errs.push_back("theme: 'bg' is required when bar is configured");
            if (theme.fg.empty())     errs.push_back("theme: 'fg' is required when bar is configured");
            if (theme.alt_bg.empty()) errs.push_back("theme: 'alt_bg' is required when bar is configured");
            if (theme.alt_fg.empty()) errs.push_back("theme: 'alt_fg' is required when bar is configured");
            if (theme.accent.empty()) errs.push_back("theme: 'accent' is required when bar is configured");
        }
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
    auto& be = backend();

    // Wire up the unified event pipeline:
    //  - Core pushes domain events into the Runtime queue via this sink.
    //  - Backend subscribes as a receiver to hear domain events after Core.
    core_.set_event_sink(this);
    add_receiver(&be);
    add_hook_receiver(&lua_host_);
    add_hook_receiver(&be);

    // Query initial monitor list from backend and init core.
    {
        std::vector<Monitor> initial_monitors = backend().ports().monitor.get_monitors();
        core_.init(std::move(initial_monitors));
    }

    // Let the backend supply a custom Window factory (for subclassing).
    core_.set_window_factory([this](WindowId id) {
            return backend().create_window(id);
        });

    core_.mark_runtime_started(true);

    // Infrastructure: SIGCHLD reaping and monitor topology — set up before
    // modules start so they can rely on both.
    setup_sigchld_pipe();
    apply_and_refresh_monitors();
    backend().ports().monitor.select_change_events();

    for (auto& mod : modules) {
        mod->on_start();
    }
    drain_events();
    be.on_start(core_);
    adopt_existing_windows(*this, core_, be);
    drain_events();
    post_event(event::RuntimeStarted{});
    drain_events();
}

void Runtime::stop(bool is_exec_restart) {
    auto& be = backend();
    post_event(event::RuntimeStopping{ is_exec_restart });
    drain_events();

    // Stop routing runtime events to backend/core before module teardown.
    remove_receiver(&be);
    core_.set_event_sink(nullptr);

    // Give modules a chance to release runtime resources explicitly.
    for (auto it = modules.rbegin(); it != modules.rend(); ++it)
        (*it)->on_stop(is_exec_restart);

    // Drop events queued during on_stop() to avoid carrying stale pointers
    // into a future start() in test harnesses.
    event_queue_.clear();

    // Deterministic lifetime boundary: module-owned resources (including
    // module windows) must be destroyed before backend shutdown.
    modules.clear();

    verify_module_window_registry_consistency("Runtime::stop(modules.clear)");
    report_live_module_windows("stop");
    module_windows_by_id_.clear();

    // Stop hook/event fanout after module teardown.
    hook_registry_.clear();
    extra_receivers_.clear();

    be.shutdown();
    teardown_sigchld_pipe();
    core_.mark_runtime_started(false);
}

void Runtime::reload() {
    for (auto& mod : modules)
        mod->on_reload();
}

void Runtime::add_receiver(IEventReceiver* receiver) {
    extra_receivers_.add(receiver);
}

void Runtime::remove_receiver(IEventReceiver* receiver) {
    extra_receivers_.remove(receiver);
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

void Runtime::verify_module_window_registry_consistency(const char* where) const {
#ifdef NDEBUG
    (void)where;
#else
    for (const auto& [id, window] : module_windows_by_id_) {
        if (!window) {
            LOG_ERR("%s: null RenderWindow* for id=%d in module window map", where, id);
            std::abort();
        }
        if (window->id() != id) {
            LOG_ERR("%s: module window id mismatch: map key=%d actual=%d",
                where, id, window->id());
            std::abort();
        }
    }
#endif
}

void Runtime::report_live_module_windows(const char* phase) const {
    if (module_windows_by_id_.empty())
        return;

    std::ostringstream ids;
    bool               first = true;
    int                count = 0;
    for (const auto& [id, _] : module_windows_by_id_) {
        if (count++ >= 16) {
            ids << ", ...";
            break;
        }
        if (!first) ids << ", ";
        first = false;
        ids << id;
    }

    LOG_ERR("Runtime::%s: %zu module render window(s) still alive ids=[%s]",
        phase, module_windows_by_id_.size(), ids.str().c_str());

#ifndef NDEBUG
    std::abort();
#endif
}

void Runtime::apply_and_refresh_monitors() {
    auto& mp = backend().ports().monitor;
    mp.apply_monitor_layout(monitor_layout::build(
        core_config_.monitors.get(), core_config_.compose.get()));

    auto monitors = mp.get_monitors();
    LOG_INFO("monitors: found %d monitor(s):", (int)monitors.size());
    for (auto& m : monitors)
        LOG_INFO("  %s: %dx%d+%d+%d", m.name.c_str(), m.size().x(), m.size().y(), m.pos().x(), m.pos().y());

    (void)core_.dispatch(command::atom::ApplyMonitorTopology{ std::move(monitors) });
}

void Runtime::dispatch_display_change() {
    apply_and_refresh_monitors();
    // Hot-plug path: notify modules so they can rebuild bar/tray layout.
    // (Initial topology apply in start() intentionally does not emit this.)
    post_event(event::DisplayTopologyChanged{});
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

    event_loop_.watch(sigchld_pipe_rd_, [this]() {
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
        post_event(event::ChildExited{ pid, exit_code });
    }
}

Backend& Runtime::backend() {
    return *backend_;
}

const Backend& Runtime::backend() const {
    return *backend_;
}

// ---------------------------------------------------------------------------
// Module render window & tray factories
// ---------------------------------------------------------------------------

Runtime::RenderWindowHandle Runtime::create_render_window(const ModuleWindowCreateInfo& info) {
    backend::RenderWindowCreateInfo rw{};
    rw.monitor_index           = info.monitor_index;
    rw.pos                     = info.pos;
    rw.size                    = info.size;
    rw.background_pixel        = backend().ports().render.black_pixel();
    rw.want_expose             = info.want_expose;
    rw.want_button_press       = info.want_button_press;
    rw.want_button_release     = info.want_button_release;
    rw.hints.override_redirect = true;
    rw.hints.dock              = info.dock;
    rw.hints.keep_above        = info.keep_above;

    auto window = backend().ports().render.create_window(rw);
    if (!window)
        return nullptr;

    WindowId id = window->id();
    if (module_windows_by_id_.contains(id)) {
        LOG_ERR("Runtime::create_render_window: duplicate module window id=%d", id);
        return nullptr;
    }

    auto* raw = window.release();
    module_windows_by_id_[id] = raw;
    verify_module_window_registry_consistency("Runtime::create_render_window");
    return RenderWindowHandle(raw, RenderWindowDeleter{ this });
}

std::unique_ptr<backend::TrayHost>
Runtime::create_tray(backend::RenderWindow& owner, bool own_selection) {
    auto* port = backend().ports().tray_host;
    if (!port)
        return nullptr;

    return port->create(owner.id(), owner.x(), owner.y(), owner.height(), own_selection);
}

void Runtime::unregister_module_window(backend::RenderWindow& window) {
    const WindowId id = window.id();
    auto           it = module_windows_by_id_.find(id);
    if (it == module_windows_by_id_.end()) {
        LOG_WARN("Runtime::unregister_module_window: id=%d not found in map", id);
        return;
    }
    if (it->second != &window) {
        LOG_WARN("Runtime::unregister_module_window: id=%d points to a different window ptr", id);
        return;
    }
    module_windows_by_id_.erase(it);
    verify_module_window_registry_consistency("Runtime::unregister_module_window");
}

void Runtime::tick() {
    constexpr std::size_t kMaxBackendEventsPerTick = 2048;
    auto&                 be                       = backend();

    event_loop_.poll(100);
    be.pump_events(kMaxBackendEventsPerTick);

    bool reloaded = process_pending_reload();
    if (reloaded) {
        be.on_reload_applied();
        post_event(event::RaiseDocks{});
    }

    drain_events();

    be.render_frame();
}

void Runtime::run_loop() {
    auto& be = backend();
    event_loop_.watch(be.event_fd(), []() {
        });
    event_loop_.start();

    while (!stop_requested)
        tick();

    event_loop_.stop();
    event_loop_.unwatch(be.event_fd());

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
        WorkspaceId ws_id = core_.workspace_of_window(id);
        if (ws_id < 0)
            continue;
        auto ws = core_.window_state_any(id);
        // Skip self-managed borderless (Proton/Wine service windows) — recreated by the app.
        if (ws && ws->borderless && ws->self_managed)
            continue;
        int fs = (ws && ws->fullscreen) ? 1 : 0;
        int he = (ws && ws->hidden_explicitly) ? 1 : 0;
        int bl = (ws && ws->borderless) ? 1 : 0;
        out << "WINDOW " << id << " " << ws_id
            << " " << ((ws && ws->floating) ? 1 : 0)
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
