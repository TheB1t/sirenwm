#pragma once

#include <string>

class Config;
class Core;
class LuaHost;
class Runtime;

namespace config_loader {

bool load(Config& config, const std::string& path, Core& core, Runtime& runtime, LuaHost& lua, bool reset_lua_vm);

} // namespace config_loader
