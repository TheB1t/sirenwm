#pragma once

#include <lua_host.hpp>
#include <vec.hpp>

// Push a Vec2 table {x=x, y=y} with the Vec2 metatable onto the Lua stack.
inline void push_vec2(LuaContext& lua, double x, double y) {
    lua.new_table();
    lua.push_number(x);  lua.set_field(-2, "x");
    lua.push_number(y);  lua.set_field(-2, "y");
    lua.get_global("Vec2");
    lua.set_metatable(-2);
}

inline void push_vec2(LuaContext& lua, Vec2i v) {
    push_vec2(lua, (double)v.x(), (double)v.y());
}
