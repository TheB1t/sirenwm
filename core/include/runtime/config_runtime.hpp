#pragma once

class Config;

namespace config_runtime {

// Registers built-in runtime settings owned by Config:
// - behavior
// - monitors
// - compose_monitors
// - workspaces
void register_builtin_runtime_settings(Config& config);

} // namespace config_runtime