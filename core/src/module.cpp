#include <module.hpp>
#include <runtime.hpp>

Module::Module(ModuleDeps deps)
    : runtime(deps.runtime)
    , core(deps.core)
    , backend(deps.runtime.backend())
    , store(deps.runtime.store)
    , lua(deps.runtime.lua) {}

RuntimeState Module::runtime_state() const {
    return runtime.state();
}
