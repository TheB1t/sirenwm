#include <runtime.hpp>

#include <backend/backend.hpp>
#include <backend/commands.hpp>
#include <config.hpp>
#include <core.hpp>
#include <module_registry.hpp>
#include <log.hpp>

#include <cassert>
#include <fstream>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <unistd.h>
#include <sys/select.h>

namespace {

void adopt_existing_windows(Runtime& runtime, Core& core, Backend& backend) {
    auto startup = backend.scan_existing_windows();
    if (startup.windows.empty())
        return;

    int adopted = 0;
    for (const auto& snap : startup.windows) {
        if (snap.window == NO_WINDOW)
            continue;

        if (!snap.from_restart && !snap.default_manage)
            continue;

        event::ManageWindowQuery q{ snap.window, true };
        runtime.query(core, q, [](const event::ManageWindowQuery& s) {
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
                .window      = snap.window,
                .wm_instance = snap.wm_instance,
                .wm_class    = snap.wm_class,
                .type        = snap.type,
                .wm_fixed_size = snap.wm_fixed_size,
            });

        (void)core.dispatch(command::SetWindowMapped{
                .window = snap.window,
                .mapped = snap.currently_viewable,
            });
        // Force hidden_by_workspace=true for currently-visible snapshot windows so the
        // subsequent SwitchWorkspace drives sync_workspace_visibility to emit MapWindow
        // effects. This causes xconn.map_window → MapNotify on all visible windows,
        // which Electron/Chromium apps require to activate input handling after a reload.
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
        }

        // Seed geometry from the actual X state so floating windows have correct
        // coordinates before the first ConfigureNotify. Without this, window->x/y
        // stay zero until a ConfigureNotify arrives, causing the first drag to jump.
        if (snap.has_geometry)
            (void)core.dispatch(command::SetWindowGeometry{
                    snap.window, snap.geo_x, snap.geo_y, snap.geo_w, snap.geo_h });

        runtime.emit(core, event::ApplyWindowRules{ snap.window, snap.from_restart });
        runtime.emit(core, event::WindowMapped{ snap.window });
        backend.on(event::WindowAdopted{ snap.window, snap.currently_viewable });

        adopted++;
    }

    if (adopted <= 0)
        return;

    // Restore active workspace per monitor from restart snapshot.
    const auto& mon_ws = startup.monitor_active_ws;
    const auto& mons   = core.monitor_states();
    if (!mon_ws.empty()) {
        for (int i = 0; i < (int)mons.size(); i++) {
            auto it = mon_ws.find(i);
            if (it != mon_ws.end() && it->second >= 0 && it->second < core.workspace_count())
                (void)core.dispatch(command::SwitchWorkspace{ it->second, i });
        }
    } else {
        int ws_id = -1;
        int fmon  = core.focused_monitor_index();
        if (fmon >= 0 && fmon < (int)mons.size())
            ws_id = mons[(size_t)fmon].active_ws;

        if (ws_id >= 0)
            (void)core.dispatch(command::SwitchWorkspace{ ws_id, std::nullopt });
        else
            (void)core.dispatch(command::ReconcileNow{});
    }

    LOG_INFO("adopt: restored %d existing window(s) at runtime start", adopted);
}

} // namespace

Runtime& Runtime::use(Core& core, const std::string& name) {
    if (modules_frozen)
        return *this;
    for (auto& m : modules)
        if (m && m->name() == name)
            return *this;
    auto mod = module_registry_.create(name, ModuleDeps{ *this, config_ });
    if (mod) {
        mod->on_init(core);
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

void Runtime::emit_lua_init(Core& core) {
    for (auto& m : modules)
        m->on_lua_init(core);
}

void Runtime::start(Core& core, Backend& backend) {
    modules_frozen  = true;
    active_backend_ = &backend;
    core.mark_runtime_started(true);
    for (auto& mod : modules)
        mod->on_start(core);
    drain_core_events(core);
    backend.on_start(core);
    adopt_existing_windows(*this, core, backend);
    drain_core_events(core);
}

void Runtime::stop(Core& core, bool is_exec_restart) {
    for (auto it = modules.rbegin(); it != modules.rend(); ++it)
        (*it)->on_stop(core, is_exec_restart);
    core.mark_runtime_started(false);
    unbind_backend();
}

void Runtime::reload(Core& core) {
    for (auto& mod : modules)
        mod->on_reload(core);
}

void Runtime::dispatch_display_change() {
    if (display_change_handler)
        display_change_handler();
}

void Runtime::watch_fd(int fd, std::function<void()> cb) {
    watched_fds.push_back({ fd, std::move(cb) });
}

void Runtime::populate_watched_fds(fd_set& fds, int& max_fd) const {
    for (auto& w : watched_fds) {
        FD_SET(w.fd, &fds);
        if (w.fd > max_fd)
            max_fd = w.fd;
    }
}

void Runtime::dispatch_watched_fds(const fd_set& fds) {
    for (auto& w : watched_fds)
        if (FD_ISSET(w.fd, &fds))
            w.cb();
}

void Runtime::drain_core_events(Core& core) {
    auto events = core.take_core_events();
    for (const auto& ev : events) {
        std::visit([&](const auto& e) {
                emit(core, e);
                if (active_backend_)
                    active_backend_->on(e);
            }, ev);
    }
}

Backend& Runtime::backend() {
    assert(active_backend_ && "Runtime backend is not active");
    return *active_backend_;
}

const Backend& Runtime::backend() const {
    assert(active_backend_ && "Runtime backend is not active");
    return *active_backend_;
}

void Runtime::run_loop(Backend& backend, Core& core) {
    stop_requested = false;
    constexpr std::size_t kMaxBackendEventsPerTick = 2048;

    while (!stop_requested) {
        fd_set fds;
        FD_ZERO(&fds);

        int backend_fd = backend.event_fd();
        FD_SET(backend_fd, &fds);
        int max_fd     = backend_fd;

        populate_watched_fds(fds, max_fd);

        struct timeval tv = { .tv_sec = 0, .tv_usec = 100000 };
        select(max_fd + 1, &fds, nullptr, nullptr, &tv);

        dispatch_watched_fds(fds);
        backend.pump_events(kMaxBackendEventsPerTick);

        if (process_pending_reload(core))
            backend.on_reload_applied();

        drain_core_events(core);
        backend.render_frame();
    }

    LOG_INFO("Runtime loop stopped");
}

namespace {

std::string restart_state_path() {
    return "/tmp/sirenwm-restart-state-" + std::to_string((unsigned long)getuid()) + ".txt";
}

} // namespace

void Runtime::save_restart_state(const Core& core) {
    std::ofstream out(restart_state_path(), std::ios::trunc);
    if (!out.is_open()) {
        LOG_WARN("restart: cannot write state file %s", restart_state_path().c_str());
        return;
    }

    // Save active workspace per monitor: "MON <idx> <active_ws>"
    const auto& mons = core.monitor_states();
    for (int i = 0; i < (int)mons.size(); i++)
        out << "MON " << i << " " << mons[(size_t)i].active_ws << "\n";

    // Save per-window state: "<window_id> <ws_id> <floating>"
    std::unordered_set<WindowId> seen;
    for (auto id : core.all_window_ids()) {
        if (!seen.insert(id).second)
            continue;
        int ws_id = core.workspace_of_window(id);
        if (ws_id < 0)
            continue;
        // Skip borderless windows (Proton/Wine service windows) — they are
        // recreated by the application and must not be restored by the WM.
        auto ws = core.window_state_any(id);
        if (ws && ws->borderless)
            continue;
        out << id << " " << ws_id << " " << (core.is_window_floating(id) ? 1 : 0) << "\n";
    }

    LOG_INFO("restart: saved %d monitor(s) + %d window(s) to %s",
        (int)mons.size(), (int)seen.size(), restart_state_path().c_str());
}

void Runtime::request_exec_restart(Core& core) {
    const auto& path = core.get_config_path();
    if (!path.empty() && !Config::check_syntax(path)) {
        LOG_ERR("restart: aborted — syntax error in %s", path.c_str());
        return;
    }
    save_restart_state(core);
    exec_restart_pending = true;
    request_stop();
}

bool Runtime::consume_exec_restart_request() {
    return exec_restart_pending.exchange(false);
}

namespace {

CoreSettings make_core_settings_from_config(const Config& cfg) {
    CoreSettings out;
    out.monitor_aliases     = cfg.get_monitor_aliases();
    out.monitor_compose     = cfg.get_monitor_compose();
    out.workspace_defs      = cfg.get_workspace_defs();
    out.follow_moved_window = cfg.get_follow_moved_window();
    out.focus_new_window    = cfg.get_focus_new_window();
    out.theme               = cfg.get_theme();
    return out;
}

} // namespace

bool Runtime::reload_runtime_config(Core& core) {
    const auto& config_path = core.get_config_path();
    if (config_path.empty()) {
        LOG_ERR("reload: aborted — config_path is empty");
        return false;
    }

    if (!Config::check_syntax(config_path)) {
        LOG_ERR("reload: aborted — syntax error in %s, config unchanged", config_path.c_str());
        return false;
    }

    auto config_backup      = config_.snapshot();
    auto core_reload_backup = core.snapshot_reload_state();

    auto restore_snapshot   = [&]() {
            config_.restore(config_backup);
            core.restore_reload_state(core_reload_backup);
        };

    config_.clear();
    core.clear_reloadable_runtime_state();

    if (!config_.load(config_path, core, *this, /*reset_lua_vm=*/ false)) {
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

    core.apply_settings(make_core_settings_from_config(config_));
    reload(core);
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

bool Runtime::process_pending_reload(Core& core) {
    ReloadRequest req = consume_reload_request();
    if (req == ReloadRequest::None)
        return false;

    if (req == ReloadRequest::SoftRestart)
        LOG_INFO("restart: deferred in-process reload");
    else
        LOG_INFO("reload: deferred config reload");

    bool reload_ok = reload_runtime_config(core);

    if (reload_ok) {
        // Snapshot active workspace per monitor before display change resets layout.
        std::vector<int> saved_ws;
        {
            const auto& mons = core.monitor_states();
            saved_ws.resize(mons.size(), -1);
            for (int i = 0; i < (int)mons.size(); i++)
                saved_ws[(size_t)i] = mons[(size_t)i].active_ws;
        }

        dispatch_display_change();

        // Restore active workspace on every monitor.
        const auto& mons = core.monitor_states();
        bool        any  = false;
        for (int i = 0; i < (int)mons.size() && i < (int)saved_ws.size(); i++) {
            if (saved_ws[(size_t)i] >= 0 && saved_ws[(size_t)i] < core.workspace_count()) {
                (void)core.dispatch(command::SwitchWorkspace{ saved_ws[(size_t)i], i });
                any = true;
            }
        }
        if (!any)
            (void)core.dispatch(command::ReconcileNow{});
    }

    return true;
}