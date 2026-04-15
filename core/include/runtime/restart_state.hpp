#pragma once

#include <cstdlib>
#include <string>
#include <unistd.h>

#include <support/log.hpp>

// Returns the path used for restart-state persistence.
// Prefers $XDG_RUNTIME_DIR (per-user, tmpfs, mode 0700 on most distros);
// falls back to /tmp with the uid in the filename.
inline std::string restart_state_path() {
    const char* xdg = std::getenv("XDG_RUNTIME_DIR");
    if (xdg && xdg[0] != '\0')
        return std::string(xdg) + "/sirenwm-restart-state.txt";
    return "/tmp/sirenwm-restart-state-" + std::to_string((unsigned long)getuid()) + ".txt";
}
