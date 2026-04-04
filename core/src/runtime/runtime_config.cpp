#include <runtime_config.hpp>
#include <lua_host.hpp>

// GCC 14 false positive: -Wfree-nonheap-object triggered by recursive
// std::variant<..., std::unordered_map<std::string, RuntimeValue>> destruction
// during inlined assignment. The object is stack-allocated; the warning is wrong.
#if defined(__GNUC__) && !defined(__clang__)
#  pragma GCC diagnostic ignored "-Wfree-nonheap-object"
#endif

namespace {

constexpr int kMaxRuntimeDepth = 32;

bool runtime_value_from_lua_impl(LuaContext& lua, int idx, RuntimeValue& out, int depth) {
    idx = lua.abs_index(idx);
    if (depth > kMaxRuntimeDepth)
        return false;

    if (lua.is_nil(idx)) {
        out = RuntimeValue();
        return true;
    }
    if (lua.is_bool(idx)) {
        out = RuntimeValue(lua.to_bool(idx));
        return true;
    }
    if (lua.is_number(idx)) {
        if (lua.is_integer(idx))
            out = RuntimeValue((int64_t)lua.to_integer(idx));
        else
            out = RuntimeValue((double)lua.to_number(idx));
        return true;
    }
    if (lua.is_string(idx)) {
        out = RuntimeValue(lua.to_string(idx));
        return true;
    }
    if (lua.is_table(idx)) {
        // Array-style table if [1..n] are all present and there are no
        // non-integer keys in iteration.
        int  n          = lua.raw_len(idx);
        bool array_like = true;

        lua.push_nil();
        while (lua.next(idx)) {
            if (!lua.is_number(-2) || !lua.is_integer(-2)) {
                array_like = false;
                lua.pop(2);
                break;
            }
            int64_t k = lua.to_integer(-2);
            if (k < 1 || k > n)
                array_like = false;
            lua.pop();
            if (!array_like) {
                lua.pop();
                break;
            }
        }

        if (array_like) {
            RuntimeValue::Array arr;
            arr.reserve((size_t)n);
            for (int i = 1; i <= n; i++) {
                lua.raw_geti(idx, i);
                RuntimeValue v;
                bool         ok = runtime_value_from_lua_impl(lua, -1, v, depth + 1);
                lua.pop();
                if (!ok)
                    return false;
                arr.push_back(std::move(v));
            }
            out = RuntimeValue(std::move(arr));
            return true;
        }

        RuntimeValue::Object obj;
        lua.push_nil();
        while (lua.next(idx)) {
            if (!lua.is_string(-2)) {
                lua.pop(2);
                return false;
            }
            std::string  k  = lua.to_string(-2);
            RuntimeValue v;
            bool         ok = runtime_value_from_lua_impl(lua, -1, v, depth + 1);
            lua.pop();
            if (!ok) {
                lua.pop();
                return false;
            }
            obj.emplace(std::move(k), std::move(v));
        }
        out = RuntimeValue(std::move(obj));
        return true;
    }
    return false;
}

} // namespace

const char* runtime_value_type_name(RuntimeValueType t) {
    switch (t) {
        case RuntimeValueType::Null:   return "null";
        case RuntimeValueType::Bool:   return "bool";
        case RuntimeValueType::Int:    return "int";
        case RuntimeValueType::Number: return "number";
        case RuntimeValueType::String: return "string";
        case RuntimeValueType::Array:  return "array";
        case RuntimeValueType::Object: return "object";
    }
    return "unknown";
}

RuntimeValueType runtime_value_type(const RuntimeValue& v) {
    if (v.is_null())   return RuntimeValueType::Null;
    if (v.is_bool())   return RuntimeValueType::Bool;
    if (v.is_int())    return RuntimeValueType::Int;
    if (v.is_num())    return RuntimeValueType::Number;
    if (v.is_string()) return RuntimeValueType::String;
    if (v.is_array())  return RuntimeValueType::Array;
    return RuntimeValueType::Object;
}

bool runtime_value_from_lua(LuaContext& lua, int idx, RuntimeValue& out) {
    return runtime_value_from_lua_impl(lua, idx, out, 0);
}