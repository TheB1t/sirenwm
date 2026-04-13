# Contributing to SirenWM

## Building

See the [Dependencies](README.md#1-dependencies) section in the README for package names per distro.

```bash
cmake -S . -B build
cmake --build build -j$(nproc)
# binaries:
#   output/sirenwm-x11
#   output/sirenwm-wayland
```

## Running Tests

```bash
cmake -S . -B build-test -DBUILD_TESTING=ON
cmake --build build-test -j$(nproc)
ctest --test-dir build-test --output-on-failure
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
