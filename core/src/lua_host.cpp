#include <lua/lua_host.hpp>
#include <support/log.hpp>
#include <protocol/keyboard.hpp>

#include <cstdarg>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

namespace {

constexpr const char* LUA_ROOT_PRIMARY = "siren";

inline lua_State* as_state(void* p) {
    return static_cast<lua_State*>(p);
}

class LuaStackGuard {
    lua_State* L_   = nullptr;
    int        top_ = 0;

    public:
        explicit LuaStackGuard(lua_State* L) : L_(L), top_(L ? lua_gettop(L) : 0) {}
        LuaStackGuard(const LuaStackGuard&)            = delete;
        LuaStackGuard& operator=(const LuaStackGuard&) = delete;
        ~LuaStackGuard() {
            if (L_)
                lua_settop(L_, top_);
        }
};

int lua_native_dispatch(lua_State* L) {
    auto* fn = static_cast<LuaNativeFn*>(lua_touserdata(L, lua_upvalueindex(1)));
    if (!fn || !*fn)
        return 0;
    LuaContext ctx{ static_cast<void*>(L) };
    return (*fn)(ctx);
}

struct LuaNativeCallbackWithContext {
    LuaNativeFnWithContext fn       = nullptr;
    void*                  userdata = nullptr;
};

int lua_native_dispatch_with_context(lua_State* L) {
    auto* cb = static_cast<LuaNativeCallbackWithContext*>(lua_touserdata(L, lua_upvalueindex(1)));
    if (!cb || !cb->fn)
        return 0;
    LuaContext ctx{ static_cast<void*>(L) };
    return cb->fn(ctx, cb->userdata);
}

void push_native_callback(lua_State* L, LuaNativeFn fn) {
    auto* up = static_cast<LuaNativeFn*>(lua_newuserdatauv(L, sizeof(LuaNativeFn), 0));
    *up = fn;
    lua_pushcclosure(L, lua_native_dispatch, 1);
}

void push_native_callback(lua_State* L, LuaNativeFnWithContext fn, void* userdata) {
    auto* up = static_cast<LuaNativeCallbackWithContext*>(
        lua_newuserdatauv(L, sizeof(LuaNativeCallbackWithContext), 0));
    up->fn       = fn;
    up->userdata = userdata;
    lua_pushcclosure(L, lua_native_dispatch_with_context, 1);
}

void publish_root(LuaContext& ctx) {
    ctx.push_value(-1);
    ctx.set_global(LUA_ROOT_PRIMARY);
}

void rebuild_root_table(LuaContext& ctx) {
    ctx.new_table();
    publish_root(ctx);
}

void push_root_table(LuaContext& ctx) {
    ctx.get_global(LUA_ROOT_PRIMARY);
    if (ctx.is_table(-1))
        return;
    ctx.pop();

    rebuild_root_table(ctx);
}

} // namespace

// ---------------------------------------------------------------------------
// LuaContext
// ---------------------------------------------------------------------------

int LuaContext::abs_index(int idx) const { return lua_absindex(as_state(state_), idx); }
int LuaContext::arg_count() const { return lua_gettop(as_state(state_)); }
bool LuaContext::is_nil(int idx) const { return lua_isnil(as_state(state_), idx); }
bool LuaContext::is_bool(int idx) const { return lua_isboolean(as_state(state_), idx); }
bool LuaContext::is_integer(int idx) const { return lua_isinteger(as_state(state_), idx); }
bool LuaContext::is_number(int idx) const { return lua_isnumber(as_state(state_), idx); }
bool LuaContext::is_string(int idx) const { return lua_isstring(as_state(state_), idx); }
bool LuaContext::is_table(int idx) const { return lua_istable(as_state(state_), idx); }
bool LuaContext::is_function(int idx) const { return lua_isfunction(as_state(state_), idx); }

bool LuaContext::to_bool(int idx) const { return (bool)lua_toboolean(as_state(state_), idx); }
int64_t LuaContext::to_integer(int idx) const { return (int64_t)lua_tointeger(as_state(state_), idx); }
double LuaContext::to_number(int idx) const { return (double)lua_tonumber(as_state(state_), idx); }
std::string LuaContext::to_string(int idx) const {
    const char* s = lua_tostring(as_state(state_), idx);
    return s ? std::string(s) : std::string();
}

std::string LuaContext::check_string(int idx) const { return std::string(luaL_checkstring(as_state(state_), idx)); }
int64_t LuaContext::check_integer(int idx) const { return (int64_t)luaL_checkinteger(as_state(state_), idx); }
double LuaContext::check_number(int idx) const { return (double)luaL_checknumber(as_state(state_), idx); }
void LuaContext::check_table(int idx) const { luaL_checktype(as_state(state_), idx, LUA_TTABLE); }
void LuaContext::check_function(int idx) const { luaL_checktype(as_state(state_), idx, LUA_TFUNCTION); }

int LuaContext::raw_len(int idx) const {
    idx = abs_index(idx);
    return (int)lua_rawlen(as_state(state_), idx);
}
void LuaContext::push_nil() const { lua_pushnil(as_state(state_)); }
bool LuaContext::next(int idx) const {
    idx = abs_index(idx);
    return lua_next(as_state(state_), idx) != 0;
}
void LuaContext::raw_geti(int idx, int n) const {
    idx = abs_index(idx);
    lua_rawgeti(as_state(state_), idx, n);
}
void LuaContext::get_field(int idx, const char* key) const {
    idx = abs_index(idx);
    lua_getfield(as_state(state_), idx, key);
}
void LuaContext::set_field(int idx, const char* key) const {
    idx = abs_index(idx);
    lua_setfield(as_state(state_), idx, key);
}
void LuaContext::raw_get(int idx) const {
    idx = abs_index(idx);
    lua_rawget(as_state(state_), idx);
}
void LuaContext::raw_set(int idx) const {
    idx = abs_index(idx);
    lua_rawset(as_state(state_), idx);
}
void LuaContext::raw_seti(int idx, int n) const {
    idx = abs_index(idx);
    lua_rawseti(as_state(state_), idx, n);
}
void LuaContext::push_value(int idx) const { lua_pushvalue(as_state(state_), idx); }
void LuaContext::push_integer(int64_t v) const { lua_pushinteger(as_state(state_), (lua_Integer)v); }
void LuaContext::push_number(double v) const { lua_pushnumber(as_state(state_), (lua_Number)v); }
void LuaContext::push_string(const std::string& v) const { lua_pushlstring(as_state(state_), v.c_str(), v.size()); }
void LuaContext::push_bool(bool v) const { lua_pushboolean(as_state(state_), v ? 1 : 0); }
void LuaContext::new_table() const { lua_newtable(as_state(state_)); }
void LuaContext::get_global(const char* name) const { lua_getglobal(as_state(state_), name); }
void LuaContext::set_global(const char* name) const { lua_setglobal(as_state(state_), name); }
void LuaContext::set_metatable(int idx) const { lua_setmetatable(as_state(state_), idx); }
bool LuaContext::new_metatable(const char* name) const { return luaL_newmetatable(as_state(state_), name) != 0; }
void LuaContext::get_metatable_reg(const char* name) const { luaL_getmetatable(as_state(state_), name); }
void* LuaContext::new_userdata(size_t size) const { return lua_newuserdatauv(as_state(state_), size, 0); }
void* LuaContext::to_userdata(int idx) const { return lua_touserdata(as_state(state_), idx); }
bool LuaContext::is_userdata(int idx) const { return lua_isuserdata(as_state(state_), idx) != 0; }
void LuaContext::pop(int n) const { lua_pop(as_state(state_), n); }
void LuaContext::remove(int idx) const { lua_remove(as_state(state_), idx); }
void LuaContext::insert(int idx) const { lua_insert(as_state(state_), idx); }

int LuaContext::errorf(const char* fmt, ...) const {
    va_list ap;
    va_start(ap, fmt);
    luaL_where(as_state(state_), 1);
    lua_pushvfstring(as_state(state_), fmt, ap);
    va_end(ap);
    lua_concat(as_state(state_), 2);
    return lua_error(as_state(state_));
}

// ---------------------------------------------------------------------------
// LuaHost
// ---------------------------------------------------------------------------

LuaHost::~LuaHost() {
    if (state_)
        lua_close(as_state(state_));
}

void LuaHost::set_module_table(const std::string& name) {
    // Stores the table at the top of the stack, then pops it.
    module_tables_[name] = ref_value(-1);
    context().pop();
}

bool LuaHost::push_module_table(const std::string& name) const {
    auto it = module_tables_.find(name);
    if (it == module_tables_.end() || !it->second.valid())
        return false;
    return push_ref(it->second);
}

void LuaHost::init() {
    if (state_) {
        lua_close(as_state(state_));
        state_ = nullptr;
    }
    module_tables_.clear();
    state_ = luaL_newstate();
    luaL_openlibs(as_state(state_));
    vm_epoch_++;

    LuaContext ctx = context();
    rebuild_root_table(ctx);
}

void LuaHost::reset_root_table() {
    if (!state_)
        return;
    LuaContext ctx = context();
    rebuild_root_table(ctx);
}

void LuaHost::push_callback(LuaNativeFn fn) const {
    push_native_callback(as_state(state_), fn);
}

void LuaHost::push_callback(LuaNativeFnWithContext fn, void* userdata) const {
    push_native_callback(as_state(state_), fn, userdata);
}

void LuaHost::register_namespace(const std::string& name,
    std::initializer_list<LuaFunctionReg> funcs) {
    LuaContext    ctx = context();
    LuaStackGuard guard(as_state(state_));
    push_root_table(ctx);

    ctx.push_string(name);
    ctx.raw_get(-2);
    if (ctx.is_nil(-1)) {
        ctx.pop();
        ctx.new_table();
        ctx.push_string(name);
        ctx.push_value(-2);
        ctx.raw_set(-4);
    }

    for (auto& reg : funcs) {
        if (!reg.name || !reg.func)
            continue;
        push_callback(reg.func);
        ctx.set_field(-2, reg.name);
    }
}

void LuaHost::register_global(std::initializer_list<LuaFunctionReg> funcs) {
    LuaContext    ctx = context();
    LuaStackGuard guard(as_state(state_));
    push_root_table(ctx);
    for (auto& reg : funcs) {
        if (!reg.name || !reg.func)
            continue;
        push_callback(reg.func);
        ctx.set_field(-2, reg.name);
    }
}

void LuaHost::ensure_table(const std::string& dotted_path) {
    LuaContext ctx = context();
    push_root_table(ctx);

    size_t pos = 0;
    while (pos < dotted_path.size()) {
        size_t      dot = dotted_path.find('.', pos);
        std::string key = (dot == std::string::npos)
            ? dotted_path.substr(pos)
            : dotted_path.substr(pos, dot - pos);

        ctx.push_string(key);
        ctx.raw_get(-2);
        if (!ctx.is_table(-1)) {
            ctx.pop();
            ctx.new_table();
            ctx.push_string(key);
            ctx.push_value(-2);
            ctx.raw_set(-4);
        }
        ctx.remove(-2);
        pos = (dot == std::string::npos) ? dotted_path.size() : dot + 1;
    }
}

bool LuaHost::exec_file(const std::string& path) {
    if (luaL_dofile(as_state(state_), path.c_str()) != LUA_OK) {
        LOG_ERR("Lua error in '%s': %s", path.c_str(), lua_tostring(as_state(state_), -1));
        lua_pop(as_state(state_), 1);
        return false;
    }
    return true;
}

bool LuaHost::exec_string(const char* code, const char* name) {
    auto* L = as_state(state_);
    if (luaL_loadbuffer(L, code, strlen(code), name) != LUA_OK ||
        lua_pcall(L, 0, 0, 0) != LUA_OK) {
        LOG_ERR("Lua error in %s: %s", name, lua_tostring(L, -1));
        lua_pop(L, 1);
        return false;
    }
    return true;
}

namespace {

// Captured print() output — thread_local since the Lua state is single-
// threaded and re-entrant calls should nest cleanly if one ever happens.
thread_local std::string* g_repl_capture = nullptr;

int repl_print(lua_State* L) {
    if (!g_repl_capture) return 0;
    int n = lua_gettop(L);
    // Mimic Lua's print: separate args with \t, end with \n.
    for (int i = 1; i <= n; ++i) {
        size_t len = 0;
        // luaL_tolstring respects __tostring metamethods and leaves the
        // string on the stack; we pop it below.
        const char* str = luaL_tolstring(L, i, &len);
        if (i > 1) g_repl_capture->push_back('\t');
        g_repl_capture->append(str, len);
        lua_pop(L, 1);
    }
    g_repl_capture->push_back('\n');
    return 0;
}

} // namespace

std::string LuaHost::repl_eval(const std::string& code) {
    auto* L = as_state(state_);
    if (!L) return "<lua not initialized>";

    LuaStackGuard guard(L);

    std::string   out;

    // Save previous `print` so we can restore it if the chunk mutates it.
    lua_getglobal(L, "print");
    int prev_print_idx = lua_gettop(L);

    // Install capture-print for the duration of the call.
    std::string* prev_capture = g_repl_capture;
    g_repl_capture = &out;
    lua_pushcfunction(L, repl_print);
    lua_setglobal(L, "print");

    auto restore = [&]() {
            lua_pushvalue(L, prev_print_idx);
            lua_setglobal(L, "print");
            g_repl_capture = prev_capture;
        };

    // Try as expression (`return <code>`) first so bare expressions print
    // their value, fall back to loading the raw code as a statement block.
    std::string wrapped = "return " + code;
    int         load_rc = luaL_loadbuffer(L, wrapped.data(), wrapped.size(), "=repl");
    if (load_rc != LUA_OK) {
        lua_pop(L, 1);
        load_rc = luaL_loadbuffer(L, code.data(), code.size(), "=repl");
    }
    if (load_rc != LUA_OK) {
        out.append(lua_tostring(L, -1));
        out.push_back('\n');
        lua_pop(L, 1);
        restore();
        return out;
    }

    // Stack right now: [..., prev_print, chunk]. After pcall:
    //   success → [..., prev_print, r1, r2, ...]
    //   error   → [..., prev_print, errmsg]
    int call_rc  = lua_pcall(L, 0, LUA_MULTRET, 0);
    int nresults = lua_gettop(L) - prev_print_idx;

    if (call_rc != LUA_OK) {
        out.append(lua_tostring(L, -1));
        out.push_back('\n');
        restore();
        return out;
    }

    for (int i = 0; i < nresults; ++i) {
        int         idx = prev_print_idx + 1 + i;
        size_t      len = 0;
        const char* str = luaL_tolstring(L, idx, &len);
        if (i > 0) out.push_back('\t');
        out.append(str, len);
        lua_pop(L, 1); // pop the tolstring copy
    }
    if (nresults > 0)
        out.push_back('\n');

    restore();
    return out;
}

LuaRegistryRef LuaHost::ref_value(int index) const {
    LuaContext     ctx = context();
    LuaRegistryRef out;
    int            abs = ctx.abs_index(index);
    ctx.push_value(abs);
    out.ref_      = luaL_ref(as_state(state_), LUA_REGISTRYINDEX);
    out.vm_epoch_ = vm_epoch_;
    return out;
}

LuaRegistryRef LuaHost::ref_function(int index) const {
    LuaContext ctx = context();
    int        abs = ctx.abs_index(index);
    if (!ctx.is_function(abs))
        return {};
    return ref_value(abs);
}

bool LuaHost::push_ref(const LuaRegistryRef& ref) const {
    if (!ref.valid())
        return false;
    if (!state_ || ref.vm_epoch_ != vm_epoch_) {
        LOG_ERR("LuaHost: stale Lua callback reference");
        return false;
    }
    lua_rawgeti(as_state(state_), LUA_REGISTRYINDEX, ref.ref_);
    return true;
}

bool LuaHost::pcall(int nargs, int nresults, const char* context_text) const {
    if (!state_)
        return false;
    if (lua_pcall(as_state(state_), nargs, nresults, 0) == LUA_OK)
        return true;
    const char* err = lua_tostring(as_state(state_), -1);
    LOG_ERR("%s: %s", context_text ? context_text : "Lua callback error", err ? err : "<unknown>");
    lua_pop(as_state(state_), 1);
    return false;
}

bool LuaHost::call_ref(const LuaRegistryRef& ref, int nargs, int nresults, const char* context_text) const {
    if (!push_ref(ref))
        return false;
    if (nargs > 0)
        lua_insert(as_state(state_), -1 - nargs);
    return pcall(nargs, nresults, context_text);
}

bool LuaHost::call_ref_string(const LuaRegistryRef& ref, std::string& out, const char* context_text) const {
    if (!call_ref(ref, 0, 1, context_text))
        return false;
    LuaContext ctx = context();
    if (ctx.is_string(-1))
        out = ctx.to_string(-1);
    ctx.pop();
    return true;
}

bool LuaHost::call_ref_method_string(const LuaRegistryRef& obj_ref, const char* method,
    std::string& out, const char* context_text) const {
    if (!push_ref(obj_ref))
        return false;
    LuaContext ctx = context();
    // stack: [obj]
    ctx.get_field(-1, method);          // stack: [obj, fn]
    if (!ctx.is_function(-1)) {
        ctx.pop(2);
        return false;
    }
    ctx.push_value(-2);                 // stack: [obj, fn, obj]  (self)
    ctx.remove(-3);                     // remove original obj copy → [fn, obj]
    if (!pcall(1, 1, context_text))     // fn(obj) → [result]
        return false;
    if (ctx.is_string(-1))
        out = ctx.to_string(-1);
    ctx.pop();
    return true;
}

bool LuaHost::call_ref_method_void(const LuaRegistryRef& obj_ref, const char* method,
    const char* context_text) const {
    if (!push_ref(obj_ref))
        return false;
    LuaContext ctx = context();
    // stack: [obj]
    ctx.get_field(-1, method);          // stack: [obj, fn]
    if (!ctx.is_function(-1)) {
        ctx.pop(2);
        return false;
    }
    ctx.push_value(-2);                 // stack: [obj, fn, obj]  (self)
    ctx.remove(-3);                     // remove original obj copy → [fn, obj]
    return pcall(1, 0, context_text);   // fn(obj) → []
}

bool LuaHost::call_ref_with_int_fields(const LuaRegistryRef& ref,
    std::initializer_list<std::pair<const char*, int64_t>> fields,
    const char* context_text) const {
    if (!push_ref(ref))
        return false;
    LuaContext ctx = context();
    ctx.new_table();
    for (const auto& [key, value] : fields) {
        ctx.push_integer(value);
        ctx.set_field(-2, key);
    }
    return pcall(1, 0, context_text);
}

// ---------------------------------------------------------------------------
// sys event bus
// ---------------------------------------------------------------------------

void LuaHost::register_handler(const std::string& event, LuaRegistryRef handler) {
    if (handler.valid())
        event_handlers_[event].push_back(std::move(handler));
}

void LuaHost::clear_handlers() {
    event_handlers_.clear();
}

void LuaHost::emit_to_lua(const std::string& event,
    int (*push_args)(LuaContext&, const void*, Core&),
    const void* ev,
    Core* core) {
    auto it = event_handlers_.find(event);
    if (it == event_handlers_.end())
        return;
    LuaContext ctx = context();
    for (const auto& ref : it->second) {
        if (!push_ref(ref))
            continue;
        int nargs = (push_args && core) ? push_args(ctx, ev, *core) : 0;
        pcall(nargs, 0, ("siren.on(\"" + event + "\")").c_str());
    }
}

// ---------------------------------------------------------------------------
// IEventReceiver — bridge C++ events to Lua siren.on() handlers.
//
// Event naming convention: "category.past_participle"
//   window.*     — window lifecycle
//   workspace.*  — workspace changes
//   wm.*         — runtime lifecycle
//   display.*    — monitor topology
//   keyboard.*   — input device state
//   process.*    — child process lifecycle
// ---------------------------------------------------------------------------

#include <domain/core.hpp>
#include <domain/window_state.hpp>

// -- window.mapped --------------------------------------------------------

static int push_window_mapped(LuaContext& lua, const void* ev_ptr, Core& core) {
    auto* ev = static_cast<const event::WindowMapped*>(ev_ptr);
    lua.new_table();
    lua.push_integer(ev->window);
    lua.set_field(-2, "id");
    auto ws = core.window_state_any(ev->window);
    if (ws) {
        lua.push_string(ws->wm_class);
        lua.set_field(-2, "class");
        lua.push_string(ws->wm_instance);
        lua.set_field(-2, "instance");
        WorkspaceId ws_id = core.workspace_of_window(ev->window);
        lua.push_integer(ws_id >= 0 ? ws_id + 1 : 0);
        lua.set_field(-2, "workspace");
    }
    return 1;
}

void LuaHost::on(event::WindowMapped ev) {
    emit_to_lua("window.mapped", push_window_mapped, &ev, &core_);
}

// -- window.unmapped ------------------------------------------------------

static int push_window_unmapped(LuaContext& lua, const void* ev_ptr, Core&) {
    auto* ev = static_cast<const event::WindowUnmapped*>(ev_ptr);
    lua.new_table();
    lua.push_integer(ev->window);
    lua.set_field(-2, "id");
    lua.push_bool(ev->withdrawn);
    lua.set_field(-2, "withdrawn");
    return 1;
}

void LuaHost::on(event::WindowUnmapped ev) {
    emit_to_lua("window.unmapped", push_window_unmapped, &ev, &core_);
}

// -- window.focused -------------------------------------------------------

static int push_focus_changed(LuaContext& lua, const void* ev_ptr, Core&) {
    auto* ev = static_cast<const event::FocusChanged*>(ev_ptr);
    lua.new_table();
    lua.push_integer(ev->window);
    lua.set_field(-2, "id");
    return 1;
}

void LuaHost::on(event::FocusChanged ev) {
    emit_to_lua("window.focused", push_focus_changed, &ev, &core_);
}

// -- workspace.switched ---------------------------------------------------

static int push_workspace_switched(LuaContext& lua, const void* ev_ptr, Core&) {
    auto* ev = static_cast<const event::WorkspaceSwitched*>(ev_ptr);
    lua.new_table();
    lua.push_integer(ev->workspace_id + 1);
    lua.set_field(-2, "workspace");
    return 1;
}

void LuaHost::on(event::WorkspaceSwitched ev) {
    emit_to_lua("workspace.switched", push_workspace_switched, &ev, &core_);
}

// -- window.rules ---------------------------------------------------------

static int push_window_rules(LuaContext& lua, const void* ev_ptr, Core& core) {
    auto* h = static_cast<const hook::WindowRules*>(ev_ptr);
    lua.new_table();
    lua.push_integer(h->window);
    lua.set_field(-2, "id");
    lua.push_bool(h->from_restart);
    lua.set_field(-2, "from_restart");
    auto ws = core.window_state_any(h->window);
    if (ws) {
        lua.push_string(ws->wm_class);
        lua.set_field(-2, "class");
        lua.push_string(ws->wm_instance);
        lua.set_field(-2, "instance");
        WorkspaceId ws_id = core.workspace_of_window(h->window);
        lua.push_integer(ws_id >= 0 ? ws_id + 1 : 0);
        lua.set_field(-2, "workspace");
        const char* type_str = "normal";
        switch (ws->type) {
            case WindowType::Dialog:  type_str = "dialog";  break;
            case WindowType::Utility: type_str = "utility"; break;
            case WindowType::Splash:  type_str = "splash";  break;
            case WindowType::Modal:   type_str = "modal";   break;
            default: break;
        }
        lua.push_string(type_str);
        lua.set_field(-2, "type");
    }
    return 1;
}

void LuaHost::on_hook(hook::WindowRules& h) {
    emit_to_lua("window.rules", push_window_rules, &h, &core_);
}

// -- display.changed ------------------------------------------------------

void LuaHost::on(event::DisplayTopologyChanged) {
    emit_to_lua("display.changed", nullptr, nullptr, &core_);
}

// -- wm.started -----------------------------------------------------------

void LuaHost::on(event::RuntimeStarted) {
    emit_to_lua("wm.started", nullptr, nullptr, &core_);
}

// -- wm.stopping ----------------------------------------------------------

static int push_stopping(LuaContext& lua, const void* ev_ptr, Core&) {
    auto* ev = static_cast<const event::RuntimeStopping*>(ev_ptr);
    lua.new_table();
    lua.push_bool(ev->exec_restart);
    lua.set_field(-2, "exec_restart");
    return 1;
}

void LuaHost::on(event::RuntimeStopping ev) {
    emit_to_lua("wm.stopping", push_stopping, &ev, &core_);
}

// -- wm.reloaded ----------------------------------------------------------

void LuaHost::on(event::ConfigReloaded) {
    emit_to_lua("wm.reloaded", nullptr, nullptr, &core_);
}

// -- process.exited -------------------------------------------------------

static int push_child_exited(LuaContext& lua, const void* ev_ptr, Core&) {
    auto* ev = static_cast<const event::ChildExited*>(ev_ptr);
    lua.new_table();
    lua.push_integer(ev->pid);
    lua.set_field(-2, "pid");
    lua.push_integer(ev->exit_code);
    lua.set_field(-2, "exit_code");
    return 1;
}

void LuaHost::on(event::ChildExited ev) {
    emit_to_lua("process.exited", push_child_exited, &ev, &core_);
}

// -- window.workspace_changed ---------------------------------------------

static int push_window_workspace_changed(LuaContext& lua, const void* ev_ptr, Core&) {
    auto* ev = static_cast<const event::WindowAssignedToWorkspace*>(ev_ptr);
    lua.new_table();
    lua.push_integer(ev->window);
    lua.set_field(-2, "id");
    lua.push_integer(ev->workspace_id + 1);
    lua.set_field(-2, "workspace");
    return 1;
}

void LuaHost::on(event::WindowAssignedToWorkspace ev) {
    emit_to_lua("window.workspace_changed", push_window_workspace_changed, &ev, &core_);
}

// -- keyboard.layout_changed ----------------------------------------------

static int push_keyboard_layout(LuaContext& lua, const void* ev_ptr, Core&) {
    auto* payload = static_cast<const protocol::keyboard::LayoutChanged*>(ev_ptr);
    lua.new_table();
    lua.push_string(payload->name);
    lua.set_field(-2, "layout");
    return 1;
}

void LuaHost::on(const event::CustomEvent& ev) {
    if (auto* p = ev.msg.unpack<protocol::keyboard::LayoutChanged>()) {
        emit_to_lua("keyboard.layout_changed", push_keyboard_layout, p, &core_);
    }
}
