#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

class LuaContext;

// Runtime-typed value tree extracted from Lua config.
// Used by module-owned config registries to avoid hardcoded parser branches
// in Config::load().
struct RuntimeValue {
    using Array  = std::vector<RuntimeValue>;
    using Object = std::unordered_map<std::string, RuntimeValue>;
    using Data   = std::variant<std::monostate, bool, int64_t, double, std::string, Array, Object>;

    Data data;

    RuntimeValue() = default;
    explicit RuntimeValue(bool v)             : data(v) {}
    explicit RuntimeValue(int64_t v)          : data(v) {}
    explicit RuntimeValue(double v)           : data(v) {}
    explicit RuntimeValue(std::string v)      : data(std::move(v)) {}
    explicit RuntimeValue(Array v)            : data(std::move(v)) {}
    explicit RuntimeValue(Object v)           : data(std::move(v)) {}

    bool is_null()   const { return std::holds_alternative<std::monostate>(data); }
    bool is_bool()   const { return std::holds_alternative<bool>(data); }
    bool is_int()    const { return std::holds_alternative<int64_t>(data); }
    bool is_num()    const { return std::holds_alternative<double>(data); }
    bool is_string() const { return std::holds_alternative<std::string>(data); }
    bool is_array()  const { return std::holds_alternative<Array>(data); }
    bool is_object() const { return std::holds_alternative<Object>(data); }

    const bool*        as_bool()   const { return std::get_if<bool>(&data); }
    const int64_t*     as_int()    const { return std::get_if<int64_t>(&data); }
    const double*      as_num()    const { return std::get_if<double>(&data); }
    const std::string* as_string() const { return std::get_if<std::string>(&data); }
    const Array*       as_array()  const { return std::get_if<Array>(&data); }
    const Object*      as_object() const { return std::get_if<Object>(&data); }
};

enum class RuntimeValueType {
    Null,
    Bool,
    Int,
    Number,
    String,
    Array,
    Object,
};

const char*      runtime_value_type_name(RuntimeValueType t);
RuntimeValueType runtime_value_type(const RuntimeValue& v);

// Converts a Lua value at idx into RuntimeValue.
// Returns false for unsupported Lua types (function/userdata/thread/lightuserdata).
bool runtime_value_from_lua(LuaContext& lua, int idx, RuntimeValue& out);
