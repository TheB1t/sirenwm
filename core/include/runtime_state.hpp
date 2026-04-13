#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// ---------------------------------------------------------------------------
// Runtime lifecycle FSM states — declared separately to avoid the circular
// dependency between runtime.hpp (includes module.hpp) and module.hpp.
// ---------------------------------------------------------------------------
enum class RuntimeState : uint8_t {
    Idle,       // constructed; backend allocated; Lua VM not initialized
    Configured, // load_config() succeeded; Lua VM up; on_lua_init() done
    Starting,   // start() in progress: backend wiring + module on_start callbacks
    Running,    // run_loop() active; all modules started; backend accessible
    Stopping,   // stop() in progress: on_stop() called in reverse order
    Stopped,    // terminal; stop() complete
};

constexpr std::size_t runtime_state_count = 6;

constexpr std::size_t runtime_state_index(RuntimeState s) {
    return static_cast<std::size_t>(s);
}

// Compile-time lifecycle DFA:
//   Idle -> Configured
//   Configured -> Starting
//   Starting -> Running
//   Running -> Stopping
//   Stopping -> Stopped
constexpr std::array<std::array<bool, runtime_state_count>, runtime_state_count>
runtime_transition_table = {{
    /* from Idle */       {{ false, true,  false, false, false, false }},
    /* from Configured */ {{ false, false, true,  false, false, false }},
    /* from Starting */   {{ false, false, false, true,  false, false }},
    /* from Running */    {{ false, false, false, false, true,  false }},
    /* from Stopping */   {{ false, false, false, false, false, true  }},
    /* from Stopped */    {{ false, false, false, false, false, false }},
}};

constexpr bool runtime_transition_allowed(RuntimeState from, RuntimeState to) {
    return runtime_transition_table[runtime_state_index(from)][runtime_state_index(to)];
}

const char* runtime_state_name(RuntimeState s);
