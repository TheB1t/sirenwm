#include <config_loader.hpp>

#include <backend/backend.hpp>
#include <backend/keyboard_port.hpp>
#include <config.hpp>
#include <runtime/config_runtime.hpp>
#include <core.hpp>
#include <log.hpp>
#include <lua_helpers.hpp>
#include <module_registry.hpp>
#include <runtime.hpp>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/wait.h>

namespace {

Config& loader_config(void* userdata) {
    if (!userdata) {
        LOG_ERR("config loader context is not set"); std::abort();
    }
    return *static_cast<Config*>(userdata);
}

Core& loader_core(void* userdata) {
    return loader_config(userdata).bound_core();
}

Runtime& loader_runtime(void* userdata) {
    return loader_config(userdata).bound_runtime();
}

// package.preload loader for a C++ module: require("bar") → module API table.
// The module registers its table via LuaHost::set_module_table() in on_lua_init().
static int lua_module_preload(LuaContext& lua, void* userdata) {
    if (!lua.is_string(1))
        return 0;
    const std::string name    = lua.to_string(1);
    auto&             config  = loader_config(userdata);
    auto&             runtime = loader_runtime(userdata);
    runtime.use(name);
    runtime.emit_lua_init();
    if (config.lua().push_module_table(name))
        return 1;
    // Module registered no table — return true as a sentinel so require() succeeds.
    lua.push_bool(true);
    return 1;
}

// ---------------------------------------------------------------------------
// siren.* — built-in globals (no module owns these)
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Process object — userdata returned by siren.spawn()
// Methods: proc:pid() -> int, proc:kill([sig]) -> bool, proc:alive() -> bool
// ---------------------------------------------------------------------------

static constexpr const char* PROCESS_MT = "SirenProcess";

struct ProcessHandle {
    pid_t pid = -1;
};

static ProcessHandle* process_check(LuaContext& lua, int idx) {
    return static_cast<ProcessHandle*>(lua.to_userdata(idx));
}

static int lua_process_pid(LuaContext& lua, void*) {
    auto* p = process_check(lua, 1);
    lua.push_integer(p ? p->pid : -1);
    return 1;
}

static int lua_process_alive(LuaContext& lua, void*) {
    auto* p = process_check(lua, 1);
    if (!p || p->pid <= 0) {
        lua.push_bool(false);
        return 1;
    }
    // Use kill(pid, 0) instead of waitpid — Runtime reaps children globally
    // via SIGCHLD, so waitpid would race and return ECHILD.
    bool alive = (kill(p->pid, 0) == 0);
    lua.push_bool(alive);
    return 1;
}

static int lua_process_kill(LuaContext& lua, void*) {
    auto* p = process_check(lua, 1);
    if (!p || p->pid <= 0) {
        lua.push_bool(false);
        return 1;
    }
    int  sig = lua.is_integer(2) ? (int)lua.to_integer(2) : SIGTERM;
    bool ok  = (kill(p->pid, sig) == 0);
    lua.push_bool(ok);
    return 1;
}

static int lua_process_gc(LuaContext& lua, void*) {
    auto* p = process_check(lua, 1);
    if (p)
        p->pid = -1; // Runtime handles reaping globally via SIGCHLD.
    return 0;
}

// Register the SirenProcess metatable (idempotent — luaL_newmetatable is no-op if exists).
static void ensure_process_metatable(LuaContext& lua, LuaHost& host) {
    if (!lua.new_metatable(PROCESS_MT))
        return; // already registered

    // __index = method table
    lua.new_table();
    host.push_callback(lua_process_pid,   nullptr); lua.set_field(-2, "pid");
    host.push_callback(lua_process_alive, nullptr); lua.set_field(-2, "alive");
    host.push_callback(lua_process_kill,  nullptr); lua.set_field(-2, "kill");
    lua.set_field(-2, "__index");

    host.push_callback(lua_process_gc, nullptr);
    lua.set_field(-2, "__gc");

    lua.pop(); // pop metatable
}

// siren.spawn("xterm")  or  siren.spawn("xterm -e bash")
// Returns a process object with methods: pid(), alive(), kill([sig])
static int lua_root_spawn(LuaContext& lua, void* userdata) {
    std::string cmd = lua.check_string(1);

    pid_t       pid = fork();
    if (pid < 0) {
        lua.push_nil();
        lua.push_string(("fork failed: " + std::string(strerror(errno))).c_str());
        return 2;
    }
    if (pid == 0) {
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

    // Build and return a ProcessHandle userdata.
    auto& host = loader_config(userdata).lua();
    ensure_process_metatable(lua, host);

    auto* handle = static_cast<ProcessHandle*>(lua.new_userdata(sizeof(ProcessHandle)));
    handle->pid = pid;
    lua.get_metatable_reg(PROCESS_MT);
    lua.set_metatable(-2);
    return 1;
}

// siren.log_warn(msg) — log a warning through the WM logging system
static int lua_log_warn(LuaContext& lua, void*) {
    std::string msg = lua.is_string(1) ? lua.to_string(1) : "?";
    LOG_WARN("Lua: %s", msg.c_str());
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
    loader_runtime(userdata).request_exec_restart();
    return 0;
}

// siren.ws.switch(n)  — 1-indexed absolute workspace index
static int lua_workspace_switch(LuaContext& lua, void* userdata) {
    int n = (int)lua.check_integer(1);
    (void)loader_core(userdata).dispatch(command::SwitchWorkspace{ n - 1, std::nullopt });
    return 0;
}

// siren.monitor.focused() -> { index, pos, size, name, output }
// name   = logical alias if configured; falls back to output name
// output = RandR output name (always set)
static int lua_monitor_focused(LuaContext& lua, void* userdata) {
    auto& core = loader_core(userdata);
    int   idx  = core.focused_monitor_index();
    auto  mon  = core.monitor_state(idx);
    if (!mon) {
        lua.push_nil();
        return 1;
    }
    const auto&        aliases = loader_config(userdata).get_monitor_aliases();
    const std::string& output  = mon->name;
    std::string        name    = output;
    for (const auto& a : aliases)
        if (a.output == output) {
            name = a.alias; break;
        }

    lua.new_table();
    lua.push_integer(idx);         lua.set_field(-2, "index");
    push_vec2(lua, mon->pos());
    lua.set_field(-2, "pos");
    push_vec2(lua, mon->size());
    lua.set_field(-2, "size");
    lua.push_string(name);         lua.set_field(-2, "name");
    lua.push_string(output);       lua.set_field(-2, "output");
    return 1;
}

// siren.monitor.list() -> array of { index, pos, size, name, output }
// output = RandR output name (e.g. "eDP-1", or "default" in Xephyr) — always set
// name   = logical alias (e.g. "primary") if configured; falls back to output name
static int lua_monitor_list(LuaContext& lua, void* userdata) {
    const auto& mons    = loader_core(userdata).monitor_states();
    const auto& aliases = loader_config(userdata).get_monitor_aliases();
    lua.new_table();
    for (int i = 0; i < (int)mons.size(); i++) {
        // mons[i].name is the RandR output name; find its logical alias if any.
        const std::string& output = mons[i].name;
        std::string        name   = output; // fallback: alias = output name
        for (const auto& a : aliases)
            if (a.output == output) {
                name = a.alias; break;
            }

        lua.new_table();
        lua.push_integer(i);              lua.set_field(-2, "index");
        push_vec2(lua, mons[i].pos());
        lua.set_field(-2, "pos");
        push_vec2(lua, mons[i].size());
        lua.set_field(-2, "size");
        lua.push_string(name);            lua.set_field(-2, "name");
        lua.push_string(output);          lua.set_field(-2, "output");
        lua.raw_seti(-2, i + 1);
    }
    return 1;
}

// siren.monitor.focus(n) — focus monitor n (1-indexed)
static int lua_monitor_focus(LuaContext& lua, void* userdata) {
    int n = lua.to_integer(1) - 1; // convert to 0-indexed
    (void)loader_core(userdata).dispatch(command::FocusMonitor{ n });
    return 0;
}

// siren.win.move_to_monitor(n) or siren.win.move_to_monitor(id, n)
// 1-arg: move focused window; 2-arg: move window by id
static int lua_win_move_to_monitor(LuaContext& lua, void* userdata) {
    if (lua.arg_count() >= 2) {
        WindowId id = (WindowId)lua.check_integer(1);
        int      n  = (int)lua.check_integer(2) - 1;
        (void)loader_core(userdata).dispatch(command::MoveWindowToMonitor{ id, n });
    } else {
        int n = (int)lua.check_integer(1) - 1;
        (void)loader_core(userdata).dispatch(command::MoveWindowToMonitor{ NO_WINDOW, n });
    }
    return 0;
}

// ---------------------------------------------------------------------------
// siren.win.*
// ---------------------------------------------------------------------------

static int lua_win_close(LuaContext&, void* userdata) {
    auto focused = loader_core(userdata).focused_window_state();
    if (focused) {
        bool handled = loader_runtime(userdata).emit_until_handled(
            event::CloseWindowRequest{ focused->id });
        if (!handled)
            handled = loader_runtime(userdata).backend().close_window(focused->id);
        if (!handled)
            LOG_WARN("win.close: no close handler accepted window %u", focused->id);
    }
    return 0;
}

static int lua_win_focus_next(LuaContext&, void* userdata) {
    (void)loader_core(userdata).dispatch(command::FocusNextWindow{});
    return 0;
}

static int lua_win_focus_prev(LuaContext&, void* userdata) {
    (void)loader_core(userdata).dispatch(command::FocusPrevWindow{});
    return 0;
}

// siren.win.toggle_floating()
static int lua_win_toggle_floating(LuaContext&, void* userdata) {
    (void)loader_core(userdata).dispatch(command::ToggleFocusedWindowFloating{});
    return 0;
}

// siren.win.set_floating(id, bool) — set floating state for a specific window
static int lua_win_set_floating(LuaContext& lua, void* userdata) {
    WindowId id       = (WindowId)lua.check_integer(1);
    bool     floating = lua.to_bool(2);
    (void)loader_core(userdata).dispatch(command::SetWindowFloating{ id, floating });
    return 0;
}

// siren.win.move_to(ws)       — move focused window to workspace ws (1-indexed)
// siren.win.move_to(id, ws)   — move window by id to workspace ws (1-indexed)
static int lua_win_move_to(LuaContext& lua, void* userdata) {
    if (lua.arg_count() >= 2) {
        WindowId id = (WindowId)lua.check_integer(1);
        int      ws = (int)lua.check_integer(2) - 1;
        (void)loader_core(userdata).dispatch(command::SetWindowSuppressFocusOnce{ id, true });
        (void)loader_core(userdata).dispatch(command::MoveWindowToWorkspace{ id, ws });
    } else {
        int n = (int)lua.check_integer(1);
        (void)loader_core(userdata).dispatch(command::MoveFocusedWindowToWorkspace{ n - 1 });
    }
    return 0;
}

// ---------------------------------------------------------------------------
// siren.layout.*
// ---------------------------------------------------------------------------

static int lua_layout_zoom(LuaContext&, void* userdata) {
    (void)loader_core(userdata).dispatch(command::Zoom{});
    return 0;
}

static int lua_layout_set(LuaContext& lua, void* userdata) {
    (void)loader_core(userdata).dispatch(command::SetLayout{ lua.check_string(1) });
    return 0;
}

static int lua_layout_adj_master(LuaContext& lua, void* userdata) {
    (void)loader_core(userdata).dispatch(command::AdjustMasterFactor{ (float)lua.check_number(1) });
    return 0;
}

static int lua_layout_inc_master(LuaContext& lua, void* userdata) {
    (void)loader_core(userdata).dispatch(command::IncMaster{ (int)lua.check_integer(1) });
    return 0;
}

// siren.layout.register(name, fn) — register a Lua-defined layout algorithm.
// fn receives one table argument: { windows, monitor, gap, border, master_factor, nmaster }
// and must call siren.layout.place() for each window it wants to position.
static int lua_layout_register(LuaContext& lua, void* userdata) {
    std::string    name = lua.check_string(1);
    lua.check_function(2);
    LuaHost&       host = loader_config(userdata).lua();
    LuaRegistryRef ref  = host.ref_function(2);
    loader_core(userdata).register_lua_layout(name, std::move(ref));
    return 0;
}

// siren.layout.place(id, pos, size [, border])
//   pos  = Vec2 {x, y}
//   size = Vec2 {x, y}  (width=x, height=y)
// Routes window placement back to the C++ PlacementSink during a Lua layout callback.
static int lua_layout_place(LuaContext& lua, void* userdata) {
    WindowId id = (WindowId)lua.check_integer(1);

    lua.check_table(2);
    lua.get_field(2, "x"); int32_t x = (int32_t)lua.to_integer(-1); lua.pop();
    lua.get_field(2, "y"); int32_t y = (int32_t)lua.to_integer(-1); lua.pop();

    lua.check_table(3);
    lua.get_field(3, "x"); int w = (int)lua.to_integer(-1); lua.pop();
    lua.get_field(3, "y"); int h = (int)lua.to_integer(-1); lua.pop();

    uint32_t                   border = lua.arg_count() >= 4 ? (uint32_t)lua.check_integer(4) : 0;
    if (!loader_core(userdata).lua_place_window(id, { x, y }, { w, h }, border))
        LOG_WARN("layout.place: called outside of a layout callback");
    return 0;
}

// siren.on(event, fn) — register a Lua callback for a named WM event.
static int lua_siren_on(LuaContext& lua, void* userdata) {
    std::string    event = lua.check_string(1);
    lua.check_function(2);
    LuaHost&       host = loader_config(userdata).lua();
    LuaRegistryRef ref  = host.ref_function(2);
    host.on(event, std::move(ref));
    return 0;
}

} // namespace

namespace config_loader {

bool load(Config& config, const std::string& path, Core& core, Runtime& runtime, bool reset_lua_vm) {
    config.bind_runtime_handles(runtime);
    core.set_config_path(path);
    core.bind_lua_host(config.lua());

    auto& lua = config.lua();
    if (reset_lua_vm || !lua.initialized())
        lua.init();
    else
        lua.reset_root_table();

    // Built-in declarative runtime settings (monitors/workspaces/...).
    config_runtime::register_builtin_runtime_settings(config);

    // Reset the lua_init guard so on_lua_init() runs fresh for this VM epoch.
    runtime.reset_lua_init();
    // Let modules re-register their Lua namespaces (no-op on first load
    // since modules haven't been loaded yet; essential on hot-reload).
    runtime.emit_lua_init();
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

    register_root_fn("spawn",    lua_root_spawn);
    register_root_fn("reload",   lua_root_reload);
    register_root_fn("restart",  lua_root_restart);
    register_root_fn("log_warn", lua_log_warn);
    register_root_fn("on",      lua_siren_on);

    register_ns_fn("ws",      "switch",  lua_workspace_switch);
    register_ns_fn("monitor", "focused", lua_monitor_focused);
    register_ns_fn("monitor", "list",    lua_monitor_list);
    register_ns_fn("monitor", "focus",   lua_monitor_focus);
    register_ns_fn("win", "close",          lua_win_close);
    register_ns_fn("win", "focus_next",     lua_win_focus_next);
    register_ns_fn("win", "focus_prev",     lua_win_focus_prev);
    register_ns_fn("win", "toggle_floating",lua_win_toggle_floating);
    register_ns_fn("win", "set_floating",   lua_win_set_floating);
    register_ns_fn("win", "move_to",        lua_win_move_to);
    register_ns_fn("win", "move_to_monitor",lua_win_move_to_monitor);
    register_ns_fn("layout", "zoom",       lua_layout_zoom);
    register_ns_fn("layout", "set",        lua_layout_set);
    register_ns_fn("layout", "adj_master", lua_layout_adj_master);
    register_ns_fn("layout", "inc_master", lua_layout_inc_master);
    register_ns_fn("layout", "register",   lua_layout_register);
    register_ns_fn("layout", "place",      lua_layout_place);

    // Clear old event handlers before reload so callbacks don't accumulate.
    lua.clear_handlers();

    // Clear all user Lua modules from package.loaded so they re-execute on reload.
    // Keep standard Lua libraries (single-word names like "string", "table", etc.)
    // and C++ modules registered via package.preload (identified by the registry).
    {
        const auto& cpp_modules = runtime.module_registry().module_names();
        auto        is_cpp = [&](const std::string& key) {
                for (const auto& m : cpp_modules)
                    if (m == key) return true;
                return false;
            };
        // Standard Lua libs — never clear these.
        static const char* const std_libs[] = {
            "string", "table", "math", "io", "os", "coroutine",
            "package", "debug", "utf8", nullptr
        };
        auto is_stdlib = [](const std::string& key) {
                for (const char* const* p = std_libs; *p; ++p)
                    if (key == *p) return true;
                return false;
            };

        ctx.get_global("package");
        if (ctx.is_table(-1)) {
            ctx.get_field(-1, "loaded");
            if (ctx.is_table(-1)) {
                int                      loaded_idx = ctx.abs_index(-1);
                std::vector<std::string> to_clear;
                ctx.push_nil();
                while (ctx.next(loaded_idx)) {
                    ctx.pop(); // value
                    if (ctx.is_string(-1)) {
                        std::string key = ctx.to_string(-1);
                        if (!is_cpp(key) && !is_stdlib(key))
                            to_clear.push_back(key);
                    }
                }
                for (const auto& key : to_clear) {
                    ctx.push_nil();
                    ctx.set_field(loaded_idx, key.c_str());
                }
            }
            ctx.pop(); // loaded
        }
        ctx.pop(); // package
    }

    // Configure package.path:
    //   1. ~/.config/sirenwm/swm/?.lua       — user overrides for stdlib
    //   2. ~/.config/sirenwm/?.lua           — user modules
    //   3. SIRENWM_LUA_DIR/swm/?.lua        — stdlib (widgets.*, rules, etc.)
    //   4. SIRENWM_LUA_DIR/?.lua            — for require("swm.module") etc.
    {
        const char* home      = getenv("HOME");
        std::string user_path = home
            ? std::string(home) + "/.config/sirenwm/?.lua"
            : std::string();
        std::string user_swm_path = home
            ? std::string(home) + "/.config/sirenwm/swm/?.lua"
            : std::string();
        std::string sys_swm_path = SIRENWM_LUA_DIR "/swm/?.lua";
        std::string sys_root_path = SIRENWM_LUA_DIR "/?.lua";

        ctx.get_global("package");
        if (ctx.is_table(-1)) {
            ctx.get_field(-1, "path");
            std::string existing = ctx.is_string(-1) ? ctx.to_string(-1) : std::string();
            ctx.pop();
            // User paths take priority over system paths so user configs
            // can override bundled Lua modules.
            std::string extra;
            if (!user_swm_path.empty())
                extra = user_swm_path;
            if (!user_path.empty())
                extra += (extra.empty() ? "" : ";") + user_path;
            extra += (extra.empty() ? "" : ";") + sys_swm_path;
            extra += ";" + sys_root_path;
            if (!existing.empty())
                extra += ";" + existing;
            ctx.push_string(extra);
            ctx.set_field(-2, "path");
        }
        ctx.pop();
    }

    // Register all known C++ modules into package.preload so require("bar") etc. work.
    // The loader calls Runtime::use(core, name) and emit_lua_init, then returns true.
    {
        ctx.get_global("package");
        if (ctx.is_table(-1)) {
            ctx.get_field(-1, "preload");
            if (ctx.is_table(-1)) {
                for (const auto& name : runtime.module_registry().module_names()) {
                    lua.push_callback(lua_module_preload, &config);
                    ctx.set_field(-2, name.c_str());
                }
            }
            ctx.pop(); // preload
        }
        ctx.pop(); // package
    }

    // siren.load(name) — safe require that returns a null-object on failure.
    // The null-object silently absorbs any field access, method call, or
    // assignment, so user config code works without nil-checks.
    lua.exec_string(R"lua(
local function make_null_object(mod_name)
    local null
    local mt = {
        __index    = function() return null end,
        __newindex = function() end,
        __call     = function() return null end,
        __tostring = function() return "<unavailable:" .. mod_name .. ">" end,
    }
    null = setmetatable({}, mt)
    return null
end

function siren.load(name)
    local ok, mod = pcall(require, name)
    if ok and mod ~= nil then return mod end
    if not ok then
        siren.log_warn("siren.load('" .. name .. "'): " .. tostring(mod))
    end
    return make_null_object(name)
end
)lua", "=siren_load_prelude");

    // Vec2 type: Lua table with arithmetic metamethods.
    // Available before init.lua so configs can use Vec2(x, y) everywhere.
    lua.exec_string(R"lua(
Vec2 = {}
Vec2.__index = Vec2
function Vec2.new(x, y) return setmetatable({x=x, y=y}, Vec2) end
function Vec2.__add(a, b) return Vec2.new(a.x+b.x, a.y+b.y) end
function Vec2.__sub(a, b) return Vec2.new(a.x-b.x, a.y-b.y) end
function Vec2.__mul(a, b)
    if type(a)=="number" then return Vec2.new(a*b.x, a*b.y) end
    return Vec2.new(a.x*b, a.y*b)
end
function Vec2.__div(a, b) return Vec2.new(a.x/b, a.y/b) end
function Vec2.__unm(a) return Vec2.new(-a.x, -a.y) end
function Vec2.__eq(a, b) return a.x==b.x and a.y==b.y end
function Vec2.__tostring(v) return string.format("Vec2(%g, %g)", v.x, v.y) end
setmetatable(Vec2, {__call = function(_, x, y) return Vec2.new(x, y) end})
)lua", "=vec2_prelude");

    if (!lua.exec_file(path))
        return false;

    // -----------------------------------------------------------------------
    // Post-exec: read declarative field assignments from the siren table.
    // These can't be processed during exec because they're simple assignments,
    // not function calls.
    // -----------------------------------------------------------------------

    ctx.get_global("siren");

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

    ctx.pop();
    return runtime_ok;
}

} // namespace config_loader
