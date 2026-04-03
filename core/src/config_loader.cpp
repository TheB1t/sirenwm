#include <config_loader.hpp>

#include <backend/backend.hpp>
#include <config.hpp>
#include <runtime/config_runtime.hpp>
#include <core.hpp>
#include <log.hpp>
#include <module_registry.hpp>
#include <runtime.hpp>

#include <cassert>
#include <cstdlib>
#include <string>
#include <unistd.h>
#include <signal.h>
#include <sys/resource.h>

namespace {

Config& loader_config(void* userdata) {
    assert(userdata && "config loader context is not set");
    return *static_cast<Config*>(userdata);
}

Core& loader_core(void* userdata) {
    return loader_config(userdata).bound_core();
}

Runtime& loader_runtime(void* userdata) {
    return loader_config(userdata).bound_runtime();
}

// Intercept `siren.modules = {...}` during init.lua execution:
// load listed modules immediately and let them register their Lua namespaces.
static int lua_root_newindex(LuaContext& lua, void* userdata) {
    // args: table, key, value
    if (lua.is_string(2) &&
        lua.to_string(2) == "modules" &&
        lua.is_table(3)) {
        int n = lua.raw_len(3);
        for (int i = 1; i <= n; i++) {
            lua.raw_geti(3, i);
            if (lua.is_string(-1))
                loader_runtime(userdata).use(loader_core(userdata), lua.to_string(-1));
            lua.pop();
        }
        loader_runtime(userdata).emit_lua_init(loader_core(userdata));
    }

    // rawset(table, key, value)
    lua.raw_set(1);
    return 0;
}

// Lazy module autoload on first access (order-independent init.lua).
// Symbol -> module mapping is registered by modules in ModuleRegistry.
static int lua_root_index(LuaContext& lua, void* userdata) {
    if (!lua.is_string(2)) {
        lua.push_nil();
        return 1;
    }

    const std::string key  = lua.to_string(2);
    auto              mods = loader_runtime(userdata).module_registry().modules_for_lua_symbol(key);
    if (!mods.empty()) {
        auto& core = loader_core(userdata);
        for (const auto& mod : mods)
            loader_runtime(userdata).use(core, mod);
        loader_runtime(userdata).emit_lua_init(core);

        // Return freshly registered siren[key], if now present.
        lua.push_value(2);
        lua.raw_get(1);
        if (!lua.is_nil(-1))
            return 1;
        lua.pop();
    }

    lua.push_nil();
    return 1;
}

// ---------------------------------------------------------------------------
// siren.* — built-in globals (no module owns these)
// ---------------------------------------------------------------------------

// siren.spawn("xterm")  or  siren.spawn("xterm -e bash")
static int lua_root_spawn(LuaContext& lua, void*) {
    std::string cmd = lua.check_string(1);

    if (fork() == 0) {
        setsid();

        // Reset SIGCHLD to default so the child's own wait() calls work correctly.
        struct sigaction sa = {};
        sa.sa_handler = SIG_DFL;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGCHLD, &sa, nullptr);

        // Close all inherited file descriptors (X11 connection, pipe fds, etc.)
        // so they don't leak into the spawned application.
        struct rlimit rl;
        getrlimit(RLIMIT_NOFILE, &rl);
        int           max_fd = (int)(rl.rlim_cur < 4096 ? rl.rlim_cur : 4096);
        for (int fd = STDERR_FILENO + 1; fd < max_fd; fd++)
            close(fd);

        execl("/bin/sh", "sh", "-c", cmd.c_str(), nullptr);
        _exit(1);
    }
    return 0;
}

// siren.reload()  — hot-reload init.lua
static int lua_root_reload(LuaContext&, void* userdata) {
    LOG_INFO("reload: requested");
    loader_runtime(userdata).request_reload();
    return 0;
}

// siren.restart() — hard restart via exec().
// Use this to pick up a rebuilt binary without relogin.
static int lua_root_restart(LuaContext&, void* userdata) {
    LOG_INFO("restart: exec restart requested");
    loader_runtime(userdata).request_exec_restart(loader_core(userdata));
    return 0;
}

// siren.exec_restart() — explicit alias for hard restart via exec().
static int lua_root_exec_restart(LuaContext&, void* userdata) {
    LOG_INFO("exec_restart: request accepted, stopping event loop");
    loader_runtime(userdata).request_exec_restart(loader_core(userdata));
    return 0;
}

// siren.workspace.switch(n)  — 1-indexed absolute workspace index
static int lua_workspace_switch(LuaContext& lua, void* userdata) {
    int n = (int)lua.check_integer(1);
    (void)loader_core(userdata).dispatch(command::SwitchWorkspace{ n - 1, std::nullopt });
    return 0;
}

// siren.monitor.focused() -> { index, x, y, width, height, name }
static int lua_monitor_focused(LuaContext& lua, void* userdata) {
    auto& core = loader_core(userdata);
    int   idx  = core.focused_monitor_index();
    auto  mon  = core.monitor_state(idx);
    if (!mon) {
        lua.push_nil();
        return 1;
    }
    lua.new_table();
    lua.push_integer(idx);       lua.set_field(-2, "index");
    lua.push_integer(mon->x);    lua.set_field(-2, "x");
    lua.push_integer(mon->y);    lua.set_field(-2, "y");
    lua.push_integer(mon->width); lua.set_field(-2, "width");
    lua.push_integer(mon->height); lua.set_field(-2, "height");
    lua.push_string(mon->name);  lua.set_field(-2, "name");
    return 1;
}

// siren.theme_get() -> { font, bg, fg, alt_bg, alt_fg, accent }
static int lua_theme_get(LuaContext& lua, void* userdata) {
    const auto& t = loader_config(userdata).get_theme();
    lua.new_table();
    lua.push_string(t.font);    lua.set_field(-2, "font");
    lua.push_string(t.bg);      lua.set_field(-2, "bg");
    lua.push_string(t.fg);      lua.set_field(-2, "fg");
    lua.push_string(t.alt_bg);  lua.set_field(-2, "alt_bg");
    lua.push_string(t.alt_fg);  lua.set_field(-2, "alt_fg");
    lua.push_string(t.accent);  lua.set_field(-2, "accent");
    return 1;
}

// ---------------------------------------------------------------------------
// siren.windows.*
// ---------------------------------------------------------------------------

static int lua_windows_close(LuaContext&, void* userdata) {
    auto focused = loader_core(userdata).focused_window_state();
    if (focused) {
        bool handled = loader_runtime(userdata).emit_until_handled(
            loader_core(userdata), event::CloseWindowRequest{ focused->id });
        if (!handled)
            handled = loader_runtime(userdata).backend().close_window(focused->id);
        if (!handled)
            LOG_WARN("windows.close: no close handler accepted window %u", focused->id);
    }
    return 0;
}

static int lua_windows_focus_next(LuaContext&, void* userdata) {
    (void)loader_core(userdata).dispatch(command::FocusNextWindow{});
    return 0;
}

static int lua_windows_focus_prev(LuaContext&, void* userdata) {
    (void)loader_core(userdata).dispatch(command::FocusPrevWindow{});
    return 0;
}

static int lua_windows_move_to(LuaContext& lua, void* userdata) {
    // 1-indexed absolute workspace index
    int n = (int)lua.check_integer(1);
    (void)loader_core(userdata).dispatch(command::MoveFocusedWindowToWorkspace{ n - 1 });
    return 0;
}

static int lua_windows_toggle_floating(LuaContext&, void* userdata) {
    (void)loader_core(userdata).dispatch(command::ToggleFocusedWindowFloating{});
    return 0;
}

static int lua_windows_zoom(LuaContext&, void* userdata) {
    (void)loader_core(userdata).dispatch(command::Zoom{});
    return 0;
}

static int lua_windows_set_layout(LuaContext& lua, void* userdata) {
    (void)loader_core(userdata).dispatch(command::SetLayout{ lua.check_string(1) });
    return 0;
}

static int lua_windows_adj_master_factor(LuaContext& lua, void* userdata) {
    (void)loader_core(userdata).dispatch(command::AdjustMasterFactor{ (float)lua.check_number(1) });
    return 0;
}

static int lua_windows_inc_master(LuaContext& lua, void* userdata) {
    (void)loader_core(userdata).dispatch(command::IncMaster{ (int)lua.check_integer(1) });
    return 0;
}

} // namespace

namespace config_loader {

bool load(Config& config, const std::string& path, Core& core, Runtime& runtime, bool reset_lua_vm) {
    config.bind_runtime_handles(core, runtime);
    core.set_config_path(path);

    auto& lua = config.lua();
    if (reset_lua_vm || !lua.initialized())
        lua.init();
    else
        lua.reset_root_table();

    // Built-in declarative runtime settings (behavior/monitors/workspaces/...).
    config_runtime::register_builtin_runtime_settings(config);

    // Let modules re-register their Lua namespaces (no-op on first load
    // since modules haven't been loaded yet; essential on hot-reload).
    runtime.emit_lua_init(core);
    auto ctx = lua.context();

    auto register_root_fn = [&](const char* name, LuaNativeFnWithContext fn) {
            if (!name || !fn)
                return;
            ctx.get_global("siren");
            lua.push_callback(fn, &config);
            ctx.set_field(-2, name);
            ctx.pop();
        };

    auto register_ns_fn = [&](const char* ns, const char* name, LuaNativeFnWithContext fn) {
            if (!ns || !name || !fn)
                return;
            ctx.get_global("siren");
            ctx.get_field(-1, ns);
            if (!ctx.is_table(-1)) {
                ctx.pop();
                ctx.new_table();
                ctx.push_value(-1);
                ctx.set_field(-3, ns);
            }
            lua.push_callback(fn, &config);
            ctx.set_field(-2, name);
            ctx.pop();
            ctx.pop();
        };

    register_root_fn("spawn", lua_root_spawn);
    register_root_fn("reload", lua_root_reload);
    register_root_fn("restart", lua_root_restart);
    register_root_fn("exec_restart", lua_root_exec_restart);

    register_ns_fn("workspace", "switch", lua_workspace_switch);
    register_ns_fn("monitor", "focused", lua_monitor_focused);
    register_root_fn("theme_get",              lua_theme_get);
    register_ns_fn("windows", "close", lua_windows_close);
    register_ns_fn("windows", "focus_next", lua_windows_focus_next);
    register_ns_fn("windows", "focus_prev", lua_windows_focus_prev);
    register_ns_fn("windows", "move_to", lua_windows_move_to);
    register_ns_fn("windows", "toggle_floating", lua_windows_toggle_floating);
    register_ns_fn("windows", "zoom", lua_windows_zoom);
    register_ns_fn("windows", "set_layout", lua_windows_set_layout);
    register_ns_fn("windows", "adj_master_factor", lua_windows_adj_master_factor);
    register_ns_fn("windows", "inc_master", lua_windows_inc_master);

    // Hook assignment to siren.modules so module Lua APIs can be registered
    // immediately and used later in the same init.lua.
    ctx.get_global("siren");
    ctx.new_table();
    lua.push_callback(lua_root_newindex, &config);
    ctx.set_field(-2, "__newindex");
    lua.push_callback(lua_root_index, &config);
    ctx.set_field(-2, "__index");
    ctx.set_metatable(-2);
    ctx.pop();

    if (!lua.exec_file(path))
        return false;

    // -----------------------------------------------------------------------
    // Post-exec: read declarative field assignments from the siren table.
    // These can't be processed during exec because they're simple assignments,
    // not function calls.
    // -----------------------------------------------------------------------

    ctx.get_global("siren");

    // siren.modules = { "bar", "randr", ... }  — load modules in order
    ctx.get_field(-1, "modules");
    if (ctx.is_table(-1)) {
        int n = ctx.raw_len(-1);
        for (int i = 1; i <= n; i++) {
            ctx.raw_geti(-1, i);
            if (ctx.is_string(-1))
                runtime.use(core, ctx.to_string(-1));
            ctx.pop();
        }
    }
    ctx.pop();

    // siren.theme = { dpi=, cursor_size=, cursor_theme=, bg=, fg=, alt_bg=, accent= }
    ctx.get_field(-1, "theme");
    if (ctx.is_table(-1)) {
        ThemeConfig tc;
        int         theme_idx = ctx.abs_index(-1);

        auto        rn = [&](const char* k, int& v) {
                ctx.get_field(theme_idx, k);
                if (ctx.is_number(-1)) v = (int)ctx.to_number(-1);
                ctx.pop();
            };
        auto rs = [&](const char* k, std::string& v) {
                ctx.get_field(theme_idx, k);
                if (ctx.is_string(-1)) v = ctx.to_string(-1);
                ctx.pop();
            };

        rn("dpi",         tc.dpi);
        rn("cursor_size", tc.cursor_size);
        rs("cursor_theme", tc.cursor_theme);
        rs("font",   tc.font);
        rs("bg",     tc.bg);
        rs("fg",     tc.fg);
        rs("alt_bg", tc.alt_bg);
        rs("alt_fg", tc.alt_fg);
        rs("accent", tc.accent);

        // siren.theme.gap
        ctx.get_field(theme_idx, "gap");
        if (ctx.is_number(-1)) tc.gap = (int)ctx.to_number(-1);
        ctx.pop();

        // siren.theme.border = { thickness=, focused=, unfocused= }
        ctx.get_field(theme_idx, "border");
        if (ctx.is_table(-1)) {
            int border_idx = ctx.abs_index(-1);
            ctx.get_field(border_idx, "thickness");
            if (ctx.is_number(-1)) tc.border_thickness = (int)ctx.to_number(-1);
            ctx.pop();
            ctx.get_field(border_idx, "focused");
            if (ctx.is_string(-1)) tc.border_focused = ctx.to_string(-1);
            ctx.pop();
            ctx.get_field(border_idx, "unfocused");
            if (ctx.is_string(-1)) tc.border_unfocused = ctx.to_string(-1);
            ctx.pop();
        }
        ctx.pop();

        config.set_theme(tc);
    }
    ctx.pop();

    // Typed runtime settings (registered by modules), e.g. siren.rules = {...}
    bool runtime_ok = true;
    for (const auto& key : config.runtime_setting_keys()) {
        ctx.get_field(-1, key.c_str());
        if (ctx.is_nil(-1)) {
            ctx.pop();
            continue;
        }
        if (ctx.is_function(-1)) {
            // Key still points to Lua API function (e.g. siren.rules()) and
            // was not overridden by declarative assignment.
            ctx.pop();
            continue;
        }

        RuntimeValue rv;
        if (!runtime_value_from_lua(ctx, -1, rv)) {
            LOG_ERR("Config: setting '%s' has unsupported Lua value type", key.c_str());
            ctx.pop();
            runtime_ok = false;
            continue;
        }

        std::string err;
        if (!config.apply_runtime_setting(key, rv, err)) {
            LOG_ERR("Config: setting '%s' is invalid: %s", key.c_str(), err.c_str());
            ctx.pop();
            runtime_ok = false;
            continue;
        }

        ctx.pop();
    }

    // Declarative assignment handlers (registered by modules), e.g.
    // siren.binds = {...}, siren.keys = {...}
    bool assignment_ok = true;
    for (const auto& key : config.lua_assignment_handler_keys()) {
        ctx.get_field(-1, key.c_str());
        if (ctx.is_table(-1)) {
            std::string err;
            if (!config.dispatch_lua_assignment_handler(key, ctx, ctx.abs_index(-1), err)) {
                if (err.empty())
                    err = "handler failed";
                LOG_ERR("Config: assignment '%s' is invalid: %s", key.c_str(), err.c_str());
                assignment_ok = false;
            }
        }
        ctx.pop();
    }

    ctx.pop();
    return assignment_ok && runtime_ok;
}

} // namespace config_loader