#include <lua_host.hpp>
#include <log.hpp>

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

inline lua_State* as_state(const void* p) {
    return static_cast<lua_State*>(const_cast<void*>(p));
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

void LuaHost::on(const std::string& event, LuaRegistryRef handler) {
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
