#pragma once

#include <vector>
#include <config/config_types.hpp>
#include <runtime/runtime_store.hpp>

// Core-level configuration settings, owned by Runtime.
// Each field is a TypedSetting registered in the RuntimeStore.
struct CoreConfig {
    TypedSetting<std::vector<MonitorAlias>> monitors;
    TypedSetting<MonitorCompose>            compose;
    TypedSetting<std::vector<WorkspaceDef>> workspaces;
    TypedSetting<ThemeConfig>               theme;
};

namespace config_runtime {

// Registers core settings (monitors, compose, workspaces, theme)
// into RuntimeStore via CoreConfig typed settings.
void register_core_config(CoreConfig& cc, RuntimeStore& store);

} // namespace config_runtime
