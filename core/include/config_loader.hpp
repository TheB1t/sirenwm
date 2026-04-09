#pragma once

#include <string>

class Core;
class LuaHost;
class Runtime;

namespace config_loader {

bool load(const std::string& path, Core& core, Runtime& runtime, LuaHost& lua, bool reset_lua_vm);

// Parse-only check: compile the Lua file without executing it.
// Returns true if the file has no syntax errors.
bool check_syntax(const std::string& path);

} // namespace config_loader
