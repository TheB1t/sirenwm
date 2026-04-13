# SirenWM Architecture

This document describes the current architecture of the repository and runtime
behavior after the backend/library split (`libwl`, `libwlproto`, `libwlserver`,
`libxcb`) and runtime ownership refactor.

## 0. Scope

What this document covers:
- build topology and target wiring from root `CMakeLists.txt`
- runtime ownership and lifecycle (`main.cpp`, `Runtime`, `Core`, backends)
- boundary split between core, backends, modules, Lua, and protocol libraries
- X11 mode and Wayland mode (including embedded display-server)

What it does not cover in detail:
- every reducer branch in `Core::dispatch`
- low-level X11/Wayland protocol field semantics
- per-widget/module business logic internals

## 1. Build Profiles and Runtime Modes

Top-level build selects one backend per build:
- `-DSIRENWM_BACKEND=x11` -> binary `output/sirenwm-x11`
- `-DSIRENWM_BACKEND=wayland` -> binary `output/sirenwm-wayland`

Runtime modes:

```
+-------------------------+------------------------------+---------------------------+
| Build profile           | Runtime command             | Effective process model   |
+-------------------------+------------------------------+---------------------------+
| x11                     | sirenwm-x11                 | Single WM process         |
| wayland                 | sirenwm-wayland             | WM process + auto-spawned |
|                         |                              | display-server child      |
| wayland                 | sirenwm-wayland --display-  | Display-server only       |
|                         | server [--size WxH]         | (no WM client runtime)    |
+-------------------------+------------------------------+---------------------------+
```

Wayland normal mode (`sirenwm-wayland`) behavior in `main.cpp`:
- if `WAYLAND_DISPLAY` already exists, connect to it as WM client backend
- if `WAYLAND_DISPLAY` is absent and this is not exec-restart handoff,
  spawn same executable with `--display-server`, read emitted endpoints from
  child stdout (`WAYLAND_DISPLAY=...`, optional `DISPLAY=...`), then start WM

## 2. High-Level System View

```
                           +---------------------------+
                           | src/main.cpp              |
                           | - CLI parse               |
                           | - signal handlers         |
                           | - runtime bootstrap       |
                           +-------------+-------------+
                                         |
                                         v
                           +---------------------------+
                           | Runtime                   |
                           | - lifecycle FSM           |
                           | - event loop              |
                           | - module orchestration    |
                           | - reload/restart control  |
                           +------+------+-------------+
                                  |      |
                                  |      v
                                  |  +------------------+
                                  |  | LuaHost + config |
                                  |  | loader            |
                                  |  +------------------+
                                  v
                        +----------------------+
                        | Core (authoritative) |
                        | WorkspaceManager      |
                        | command reducers      |
                        +----------+-----------+
                                   |
                        BackendEffects/CoreEvents
                                   |
                     +-------------+-------------+
                     |                           |
                     v                           v
             +---------------+            +--------------+
             | Backend (x11) |            | Backend (wl) |
             +-------+-------+            +------+-------+
                     |                           |
                     v                           v
             +---------------+            +-----------------------+
             | X11 server    |            | Wayland display-server|
             +---------------+            | (libwlserver)         |
                                          +-----------------------+
```

## 3. Repository Topology and Boundaries

```
src/
  main.cpp                process entrypoint, mode selection, exec-restart handoff

core/
  include/, src/          Runtime, Core, WorkspaceManager, command/event model,
                          Lua host, config loader, event/hook queues

backends/
  x11/                    X11 backend adapter + X11 port implementations
  wayland/                Wayland backend adapter (client side via admin protocol)

libwl/                    generic thin C++ wrappers around Wayland client/server C APIs
libwlproto/               protocol XML + C/headers + generated C++ wrappers and APIs
libwlserver/              Wayland display-server implementation (server-specific)
libxcb/                   shared XCB wrappers used by x11 backend and wlserver

modules/
  keybindings, bar,
  keyboard, sysinfo,
  audio, debug_ui         feature modules (statically registered)

lua/
  swm/*                   Lua runtime API helpers, widgets, config-facing scripts

tests/
  core/, modules/,
  integration/            unit + module + integration layers
```

Boundary rules implemented in code structure:
- `Core` is the single mutable domain authority.
- Backends are adapters: native events in, typed commands/events out.
- Modules operate through typed runtime contracts and backend ports, not through
  direct backend internals.
- Wayland server internals are isolated in `libwlserver`; Wayland client/generic
  wrappers are in `libwl`/`libwlproto`.

## 4. Build Graph (Current)

```
                               +------------------+
                               | module_* (static)|
                               +---------+--------+
                                         |
                        (linked with --whole-archive)
                                         |
+--------------------+      +------------v-----------+
| sirenwm_core       +----->+ sirenwm-{backend} app  |
| (static)           |      | (src/main.cpp)         |
+---------+----------+      +------------+-----------+
          ^                                ^
          |                                |
+---------+-----------+                    |
| sirenwm_backend     +--------------------+
| (x11 or wayland)    |
+---------+-----------+
          |
          +--> x11 build: links xcb_wrappers + X11/XCB deps
          |
          +--> wayland build:
                links wlproto (client-side protocol wrappers)
                app additionally links wlserver (display-server mode)

Extra wayland-side libs:
  wlproto -> depends on wl
  wlserver -> depends on wlproto + xcb_wrappers + wayland-server deps
```

## 5. Runtime Ownership Model

`Runtime` now owns backend lifetime directly:
- `Runtime` constructor takes `BackendFactory`
- `Runtime` stores `std::unique_ptr<Backend>`
- `RuntimeOf<>` wrapper is removed
- `ModuleRegistry` is owned inside `Runtime` and static registrations are applied
  in constructor (`module_registry_static::apply_static_registrations`)

`Module` dependency model:
- module receives references to `Runtime`, `Core`, `Backend`, `RuntimeStore`,
  `LuaHost` at construction
- no module-side backend ownership; all lifetimes are runtime-bound

## 6. Runtime Lifecycle FSM

State enum is compile-time guarded by transition table (`runtime_state.hpp`).

```
Idle -> Configured -> Starting -> Running -> Stopping -> Stopped
```

Allowed transitions are only the edges above. Any illegal edge aborts with log.

State meaning:
- `Idle`: runtime allocated, backend allocated, config not yet loaded
- `Configured`: config loaded and validated
- `Starting`: backend wiring/module startup in progress
- `Running`: main loop active
- `Stopping`: stop sequence in progress
- `Stopped`: terminal state

## 7. Startup and Main Loop

Startup sequence (`Runtime::start`):

```
1) core.set_event_sink(runtime)
2) register backend as event receiver + hook receiver
3) query monitors from backend port, then core.init(monitors)
4) install backend window factory into core
5) setup SIGCHLD self-pipe watcher
6) apply monitor layout + select backend monitor change events
7) modules.on_start()
8) backend.on_start(core)
9) backend.scan_existing_windows() -> adopt into core
10) emit RuntimeStarted
```

Running tick (`Runtime::tick`):

```
+------------------------+
| event_loop.poll(100ms) |
+-----------+------------+
            |
            v
+------------------------+
| backend.pump_events()  |
+-----------+------------+
            |
            v
+------------------------------+
| process pending reload/restart|
+-----------+------------------+
            |
            v
+------------------------+
| drain queued events    |
| delivery order:        |
| 1) modules             |
| 2) LuaHost             |
| 3) extra receivers     |
+-----------+------------+
            |
            v
+------------------------+
| backend.render_frame() |
+------------------------+
```

## 8. Command / Effect / Event Pipeline

Core mutation pipeline is CQRS-lite with explicit write/read split.

```
Native backend events
      |
      v
Backend translates to command::atom/composite
      |
      v
Core::dispatch(...) mutates authoritative state
      |
      +--> enqueue BackendEffect / WindowFlush
      |
      +--> enqueue Core domain events (typed)

Runtime drain:
  Core events -> modules -> Lua -> backend hooks/receivers

Backend render phase:
  consume BackendEffect + WindowFlush
  apply native side effects (map/unmap/focus/configure/stack/order)
```

Important consequence:
- reducers never call native platform APIs directly
- native side effects are applied only by backend adapters

## 9. Backend Architecture

### 9.1 X11 Backend (`backends/x11`)

Primary responsibilities:
- consume X events (map/unmap/configure/client messages/property changes)
- enforce EWMH/ICCCM behavior
- map native events to core commands and runtime typed events
- apply backend effects through XCB/Xlib operations

Internal structure (simplified):
- `x11_backend.cpp` -> orchestration and backend interface
- `events.cpp` -> native event handlers and policy glue
- `ewmh.cpp` -> `_NET_*` contract handling
- `loop.cpp` -> event pump/coalescing
- `monitor_port.cpp` -> RandR topology integration
- `render_port.cpp` -> backend render window creation
- `input_port.cpp`, `keyboard_port.cpp`, `tray_host.cpp`
- shared XCB helper layer from `libxcb`

### 9.2 Wayland Backend Client (`backends/wayland`)

Primary responsibilities:
- connect as Wayland client (`wl::ClientDisplay`)
- bind generated admin protocol client API (`SirenwmAdminV1ClientApi`)
- mirror admin surfaces/outputs into core windows/monitors
- apply core backend effects by sending admin protocol requests

High-level flow:

```
WM backend (client)
  receives admin events: surface/output/input/overlay
      |
      v
  updates local surface/output maps
      |
      v
  dispatches core commands/events

Core effects
      |
      v
WM backend sends admin requests:
  configure_surface / set_surface_visible / set_surface_activated
  set_surface_border / set_surface_stacking / close_surface / warp_pointer
```

### 9.3 Wayland Display-Server (`libwlserver`)

`wl::server::run_display_server(...)` assembles server-side components:

```
wl::Display
  + Shm
  + Compositor
  + Seat (+ data-device manager)
  + Subcompositor
  + Output (wl_output global)
  + XdgShell + XdgDecoration
  + Admin protocol endpoint
  + OverlayManager
  + XWayland bridge (optional/when available)
  + OutputX11 (nested X11 presentation + input capture)
```

Main loop polls:
- Wayland event loop fd
- OutputX11 fd
- XWayland xcb fd (if available)

Then:
- dispatch corresponding sources
- request repaint
- repaint composed scene to X11 output

## 10. Protocol Layer (`libwlproto`)

`libwlproto` owns protocol generation path:

```
XML protocol specs
  - xdg-shell
  - xdg-decoration
  - xwayland-shell-v1
  - sirenwm-server-v1 (repo local)
        |
        v
wayland-scanner
  -> client header
  -> server header
  -> private-code C glue
        |
        v
Python wrapper generator
  -> *-client-wrapper.hpp
  -> *-server-wrapper.hpp
  -> *-client-api.hpp
        |
        v
protocol static targets + wlproto interface target
```

This keeps protocol glue generation centralized and removes repeated
hand-written boilerplate per protocol.

## 11. Configuration and Lua Integration

Config load (`config_loader::load`) initializes Lua and runtime settings in phases:
- initialize/reset Lua runtime table and built-in APIs
- register C++ module preload hooks (`require(...)` -> `Runtime::use(...)`)
- execute user config file
- read declarative `siren.*` assignments into typed runtime store
- validate settings (`RuntimeStore` validators + cross-setting validation)
- apply resulting `CoreSettings` into core

Event bridge:
- typed C++ events are emitted through runtime queue
- `LuaHost` subscribes as runtime receiver and re-emits mapped events to Lua

## 12. Reload and Restart Semantics

Hot reload (`SIGHUP` or API request):
- syntax check config before mutate
- snapshot runtime store + reloadable core state
- reload config without full VM teardown path used at startup
- rollback on parse/validation failure
- on success:
  - commit new settings
  - apply settings to core
  - call `modules.on_reload()`
  - refresh monitor topology and restore active workspace per monitor
  - emit `ConfigReloaded`

Exec restart:
- runtime validates config syntax
- writes restart state file with monitor active workspace and per-window state
- stops runtime
- main process calls `execv(self)`
- new process starts, backend scans existing windows, core re-adopts state

Wayland-specific note:
- in wayland build normal mode, display-server spawn is skipped on exec-restart
  handoff path via `SIRENWM_EXEC_RESTART`

## 13. Testing Architecture

```
tests/core/         core reducer and model tests
tests/modules/      module contract tests with fake backend stubs
tests/integration/  end-to-end scripts
  - run_tests.sh            (X11 path)
  - run_tests_wayland.sh    (Wayland + display-server path)
```

Integration scripts validate runtime behavior (layout/focus/EWMH/reload/restart)
and include Wayland display-server lifecycle checks.

## 14. Extension Guide

When adding new behavior:
- domain mutation -> add command (`atom` or `composite`) + reducer branch in `Core`
- backend capability -> extend backend port interface in `core/include/backend/*`
  and implement in concrete backend ports
- server protocol feature -> update XML in `libwlproto/protocols`, regenerate via
  `add_wl_protocol`, implement server-side logic in `libwlserver`
- user-facing config -> add typed setting parser/validator and keep docs/tests in sync

Rule of thumb:
- state decisions belong to `Core`
- platform I/O belongs to backends/server adapters
- feature policy belongs to modules/Lua
