#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <log.hpp>
#include <module.hpp>

using ModuleFactory = std::function<std::unique_ptr<Module>(ModuleDeps)>;

class ModuleRegistry {
    public:
        void register_module(const std::string& name, ModuleFactory factory) {
            factories[name] = std::move(factory);
        }

        std::unique_ptr<Module> create(const std::string& name, ModuleDeps deps) const {
            auto it = factories.find(name);
            if (it == factories.end()) {
                LOG_ERR("Unknown module: %s", name.c_str());
                return nullptr;
            }
            return it->second(deps);
        }

        bool has(const std::string& name) const {
            return factories.count(name) > 0;
        }

        void register_lua_symbol(const std::string& symbol, const std::string& module_name) {
            auto& mods = lua_symbol_to_modules[symbol];
            for (const auto& m : mods)
                if (m == module_name)
                    return;
            mods.push_back(module_name);
        }

        std::vector<std::string> modules_for_lua_symbol(const std::string& symbol) const {
            auto it = lua_symbol_to_modules.find(symbol);
            if (it == lua_symbol_to_modules.end())
                return {};
            return it->second;
        }

    private:
        std::unordered_map<std::string, ModuleFactory> factories;
        std::unordered_map<std::string, std::vector<std::string> > lua_symbol_to_modules;
};

namespace module_registry_static {

using ModuleRegistration    = std::function<void (ModuleRegistry&)>;
using LuaSymbolRegistration = std::pair<std::string, std::string>;

inline std::vector<ModuleRegistration>& module_registrations() {
    static std::vector<ModuleRegistration> regs;
    return regs;
}

inline std::vector<LuaSymbolRegistration>& lua_symbol_registrations() {
    static std::vector<LuaSymbolRegistration> regs;
    return regs;
}

inline void add_module_registration(ModuleRegistration fn) {
    module_registrations().push_back(std::move(fn));
}

inline void add_lua_symbol_registration(std::string symbol, std::string module_name) {
    lua_symbol_registrations().emplace_back(std::move(symbol), std::move(module_name));
}

inline void apply_static_registrations(ModuleRegistry& registry) {
    for (auto& reg : module_registrations())
        reg(registry);
    for (const auto& [symbol, module_name] : lua_symbol_registrations())
        registry.register_lua_symbol(symbol, module_name);
}

} // namespace module_registry_static

// Helper for static registration in module .cpp files:
//
//   SWM_REGISTER_MODULE("randr", RandRModule)
//
#define SWM_REGISTER_MODULE(name, type)                                      \
    static bool _swm_registered_##type = []() {                              \
        module_registry_static::add_module_registration(                      \
            [](ModuleRegistry& registry) {                                   \
                registry.register_module(name, [](ModuleDeps deps) {         \
                    return std::make_unique<type>(deps);                     \
                });                                                           \
            });                                                               \
        return true;                                                          \
    } ();