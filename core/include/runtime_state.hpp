#pragma once

#include <cstdint>

// ---------------------------------------------------------------------------
// Runtime lifecycle FSM states — declared separately to avoid the circular
// dependency between runtime.hpp (includes module.hpp) and module.hpp.
// ---------------------------------------------------------------------------
enum class RuntimeState : uint8_t {
    Idle,       // constructed; modules registered; no backend, no Lua
    Configured, // load_config() succeeded; Lua VM up; on_lua_init() done
    Starting,   // start() in progress: bind_backend + on_start being called
    Running,    // run_loop() active; all modules started; backend accessible
    Stopping,   // stop() in progress: on_stop() called in reverse order
    Stopped,    // terminal; stop() complete
};

const char* runtime_state_name(RuntimeState s);
