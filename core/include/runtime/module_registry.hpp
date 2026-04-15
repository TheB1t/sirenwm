#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <support/log.hpp>
#include <runtime/module.hpp>

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

        std::vector<std::string> module_names() const {
            std::vector<std::string> names;
            names.reserve(factories.size());
            for (const auto& [k, _] : factories)
                names.push_back(k);
            return names;
        }

    private:
        std::unordered_map<std::string, ModuleFactory> factories;
};

namespace module_registry_static {

using ModuleRegistration = std::function<void (ModuleRegistry&)>;

inline std::vector<ModuleRegistration>& module_registrations() {
    static std::vector<ModuleRegistration> regs;
    return regs;
}

inline void add_module_registration(ModuleRegistration fn) {
    module_registrations().push_back(std::move(fn));
}

inline void apply_static_registrations(ModuleRegistry& registry) {
    for (auto& reg : module_registrations())
        reg(registry);
}

} // namespace module_registry_static

// Helper for static registration in module .cpp files:
//
//   SIRENWM_REGISTER_MODULE("randr", RandRModule)
//
#define SIRENWM_REGISTER_MODULE(name, type)                                  \
    static bool _sirenwm_registered_##type = []() {                          \
        module_registry_static::add_module_registration(                      \
            [](ModuleRegistry& registry) {                                   \
                registry.register_module(name, [](ModuleDeps deps) {         \
                    return std::make_unique<type>(deps);                     \
                });                                                           \
            });                                                               \
        return true;                                                          \
    } ();
