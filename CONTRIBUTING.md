# Contributing to SirenWM

## Building

```bash
# Dependencies (Debian/Ubuntu)
sudo apt install \
  cmake pkg-config g++ \
  libx11-dev libx11-xcb-dev \
  libxcb1-dev libxcb-randr0-dev libxcb-keysyms1-dev \
  libxkbcommon-dev liblua5.4-dev \
  libcairo2-dev libpango1.0-dev libfontconfig1-dev

cmake -S . -B build
cmake --build build -j$(nproc)
# binary: output/sirenwm
```

## Code Style

All C++ is formatted with [uncrustify](https://github.com/uncrustify/uncrustify) using the project config:

```bash
uncrustify -c .uncrustify.cfg --replace --no-backup path/to/file.cpp
```

Run it against every file you touch before submitting. Key rules from the config:
- 4-space indent, no tabs
- Line width 120
- LF line endings

## Architecture Boundaries

SirenWM has strict layer separation:

- **Core** — the only runtime state writer. All mutations go through `Core::dispatch(Command)`.
- **Backend** (`backends/x11/`) — platform layer. Must not be called directly from modules or Core.
- **Modules** (`modules/`) — feature layer. Communicate through typed events and commands, never directly with each other or with backend internals.

Do not add `#include` paths that cross these boundaries. When in doubt, check that a hypothetical Wayland backend could implement the same interface without knowing about X11 concepts.

## Submitting Changes

- One logical change per PR. Do not mix refactors with behavior changes.
- Test against a real X session if the change touches event handling, geometry, or focus.
- For non-trivial behavior changes, describe what you verified and how.
