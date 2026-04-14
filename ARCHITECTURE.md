# SirenWM Architecture

## 1. Purpose

This document defines the implemented architecture of SirenWM. It is a
maintainer-facing reference for:

- binary composition
- repository structure
- runtime topology
- ownership boundaries
- event, command, and effect flow
- X11 architecture
- Wayland display-server architecture
- reload and restart behavior

This document describes the current system. It does not describe speculative or
future architecture.

## 2. System Definition

SirenWM is a tiling window manager with a single domain core and two backend
profiles:

- `x11`: a direct X11 window manager
- `wayland`: a two-process system composed of a WM controller and a SirenWM
  display-server connected by private local IPC

The architecture is defined by four invariants:

1. `Core` is the only authority for window-manager domain state.
2. `Runtime` is the only owner of lifecycle and the active backend instance.
3. backends are adapters; they do not own policy.
4. Wayland server resources are owned only by `libwlserver`.

## 3. Top-Level Architecture

```text
                           +---------------------------+
                           | src/main.cpp              |
                           | process bootstrap         |
                           +-------------+-------------+
                                         |
                                         v
                           +---------------------------+
                           | Runtime                   |
                           | lifecycle + event loop    |
                           +-------------+-------------+
                                         |
                                         v
                           +---------------------------+
                           | Core                      |
                           | authoritative WM state    |
                           +------+------+-------------+
                                  |      |
                                  |      v
                                  |  +------------------+
                                  |  | LuaHost +        |
                                  |  | modules          |
                                  |  +------------------+
                                  v
                   +--------------+--------------+
                   |                             |
                   v                             v
          +------------------+         +------------------------+
          | X11Backend       |         | DisplayServerBackend   |
          | direct adapter   |         | WM-side IPC adapter    |
          +--------+---------+         +-----------+------------+
                   |                                 |
                   v                                 v
             +-----------+                 +---------------------+
             | X11       |                 | libwlserver         |
             | server    |                 | display-server      |
             +-----------+                 +---------------------+
```

## 4. Build Profiles and Runtime Roles

### 4.1 Build Profiles

```text
+--------------------------+------------------------+--------------------------+
| CMake option             | Primary binary         | Native integration       |
+--------------------------+------------------------+--------------------------+
| SIRENWM_BACKEND=x11      | output/sirenwm-x11     | direct X11              |
| SIRENWM_BACKEND=wayland  | output/sirenwm-wayland | display-server + IPC    |
+--------------------------+------------------------+--------------------------+
```

### 4.2 Wayland Runtime Roles

The Wayland build contains two runtime roles in one executable:

```text
+--------------------------------------+----------------------------------+
| Command                              | Role                             |
+--------------------------------------+----------------------------------+
| sirenwm-wayland                      | WM controller                    |
| sirenwm-wayland --display-server     | display-server host              |
+--------------------------------------+----------------------------------+
```

Normal Wayland startup is role-dispatching logic in `src/main.cpp`:

- if `WAYLAND_DISPLAY` already exists, start only the WM controller
- otherwise spawn the same binary in `--display-server` mode, read emitted
  environment endpoints, then start the WM controller against that server

### 4.3 Current Deployment Model

The current Wayland implementation is a nested compositor presented through an
X11 window via `OutputX11`.

Implications:

- the current display-server is not a DRM/KMS compositor
- the WM-side Wayland backend is not a generic client backend for foreign
  compositors
- local development and integration tests assume an X-hosted environment such as
  Xephyr, Xvfb, or an existing X session

## 5. Build Graph

```text
                                   +----------------------+
                                   | statically linked    |
                                   | modules/*            |
                                   +----------+-----------+
                                              |
                                   linked via whole-archive
                                              |
+--------------------+              +---------v----------+
| sirenwm_core       |------------->| sirenwm-{backend}  |
|                    |              | src/main.cpp       |
| - Runtime          |              +---------+----------+
| - Core             |                        |
| - LuaHost          |                        |
| - config/runtime   |                        |
+---------+----------+                        |
          ^                                   |
          |                                   |
+---------+----------------+                  |
| backend target           |------------------+
|                          |
| x11: X11Backend          |
| wayland:                 |
|   DisplayServerBackend   |
+---------+----------------+
          |
          +--------------------------+
                                     |
                   +-----------------+------------------+
                   |                                    |
                   v                                    v
          +------------------+                 +------------------+
          | xcb_wrappers     |                 | sirenwm_ipc      |
          | libxcb           |                 | libipc           |
          +------------------+                 +---------+--------+
                                                          |
                                                          v
                                                +---------+--------+
                                                | wlserver         |
                                                | libwlserver      |
                                                +------------------+
```

Build responsibilities:

```text
+------------------------+------------------------------------------------+
| Target                 | Responsibility                                 |
+------------------------+------------------------------------------------+
| sirenwm_core           | Runtime, Core, config, Lua host, shared API    |
| X11Backend             | direct X11 backend                             |
| DisplayServerBackend   | WM-side IPC backend for Wayland mode           |
| sirenwm_ipc            | private control protocol and transport         |
| wlserver               | Wayland display-server implementation          |
| xcb_wrappers           | shared XCB helpers                             |
+------------------------+------------------------------------------------+
```

## 6. Repository Structure

```text
src/
  main.cpp                      process bootstrap and role selection

core/
  include/backend/              backend contracts, commands, events, ports, hooks
  include/config/               config schema and loader interfaces
  include/domain/               Core, monitor/window/workspace model
  include/lua/                  Lua host and Lua helpers
  include/runtime/              Runtime, queues, registries, lifecycle
  include/support/              logging and utility types
  src/                          reducers, runtime, config, Lua host

backends/x11/
  include/                      backend public surface
  include/x11/                  X11 helper types
  src/backend/                  orchestration, event loop, EWMH, adoption
  src/ports/                    input/monitor/render/keyboard/debug ports
  src/x11/                      atoms, tray host, X connection helpers

backends/wayland/
  include/                      DisplayServerBackend and port types
  src/backend/                  WM-side IPC orchestration
  src/ports/                    input/monitor/render/keyboard ports

libipc/
  include/swm/ipc/              message schema, dispatch, channel, shared buffer
  src/                          transport implementation

libwlserver/
  cmake/                        protocol code generation glue
  include/wl/                   internal Wayland support wrappers
  include/wl/server/ipc/        control server boundary
  include/wl/server/protocol/   compositor, seat, output, shell, decoration
  include/wl/server/runtime/    display-server state and runtime glue
  include/wl/server/x11/        nested output, XWayland, XWM bridge
  src/...                       matching implementation tree

libxcb/
  reusable XCB wrapper layer

modules/
  statically linked feature modules

lua/
  Lua-side widgets and helpers

tests/
  unit, module, and integration suites
```

## 7. Ownership and Architectural Rules

### 7.1 Ownership Map

```text
+-----------------------+------------------------------------------------+
| Layer                 | Owns                                           |
+-----------------------+------------------------------------------------+
| main.cpp              | process bootstrap and signal bootstrap          |
| Runtime               | lifecycle FSM, event loop, backend, modules    |
| Core                  | authoritative WM domain state                  |
| LuaHost               | Lua VM and config execution                    |
| Modules               | feature logic and Lua-visible API              |
| X11Backend            | X11 native objects and side effects            |
| DisplayServerBackend  | WM-side IPC session and mirrored server state  |
| libwlserver           | Wayland resources, display-server state,       |
|                       | XWayland bridge, nested X11 output             |
| libipc                | wire ABI, socket transport, shared buffers     |
+-----------------------+------------------------------------------------+
```

### 7.2 Mandatory Rules

1. `Core` is the only writer of WM domain state.
2. `Runtime` is the only owner of the active backend object.
3. Backends do not decide layout, workspace, or focus policy.
4. `Runtime` is the only drain point for queued events.
5. `libwlserver` is the only owner of Wayland server resources.
6. `DisplayServerBackend` does not own Wayland protocol resources.
7. Modules interact through typed contracts and ports, not backend internals.

These rules are what make the two backend families structurally compatible.

## 8. Core Window-Manager Architecture

This section describes the WM itself, independent of transport.

### 8.1 Object Model

```text
+--------------------------------------------------------------+
| Runtime                                                      |
|--------------------------------------------------------------|
| module_registry_   core_       lua_host_     store_          |
| core_config_       backend_    event_loop_   event_queue_    |
| hook_registry_     extra_receivers_          lifecycle FSM   |
+--------------------------------------------------------------+
```

```text
+--------------------------------------------------------------+
| Core                                                         |
|--------------------------------------------------------------|
| WorkspaceManager                                             |
| layout registry                                               |
| keybindings                                                   |
| monitor state                                                 |
| window state                                                  |
| pending BackendEffect list                                    |
| pending WindowFlush map                                       |
+--------------------------------------------------------------+
```

### 8.2 Responsibility Split

```text
+-------------------+------------------------------------------------------+
| Component         | Responsibility                                       |
+-------------------+------------------------------------------------------+
| Runtime           | lifecycle, orchestration, queue ownership, startup   |
| Core              | authoritative WM state and reducers                  |
| LuaHost           | Lua execution and reloadable scripting boundary      |
| Modules           | policy features, widgets, bindings, runtime services |
| Backend           | native event ingestion and side-effect execution     |
+-------------------+------------------------------------------------------+
```

### 8.3 Internal Control Flow

The WM uses a reducer-and-effects model.

```text
Native event or module intent
            |
            v
  command::atom / command::composite
            |
            v
       Core::dispatch(...)
            |
            +--> mutate domain state
            +--> queue BackendEffect
            +--> queue WindowFlush
            `--> post typed runtime events
```

Reducer constraints:

- reducers mutate only domain state
- reducers do not call native protocol APIs
- native side effects are applied later by the active backend

### 8.4 Runtime Tick

```text
+-------------------------------+
| epoll wait / event_loop.poll  |
+---------------+---------------+
                |
                v
+-------------------------------+
| backend.pump_events()         |
+---------------+---------------+
                |
                v
+-------------------------------+
| process pending reload/restart|
+---------------+---------------+
                |
                v
+-------------------------------+
| drain unified event queue     |
| - modules                     |
| - LuaHost                     |
| - extra receivers             |
+---------------+---------------+
                |
                v
+-------------------------------+
| backend.render_frame()        |
+-------------------------------+
```

### 8.5 WM Startup Sequence

```text
main.cpp            Runtime             Backend             Core              Modules
   |                   |                   |                  |                  |
   | construct Runtime |                   |                  |                  |
   |------------------>|                   |                  |                  |
   |                   | build backend     |                  |                  |
   |                   |------------------>|                  |                  |
   |                   | load config       |                  |                  |
   |                   |------------------------------------->|                  |
   |                   | bind event sink    |                  |                  |
   |                   | register backend   |                  |                  |
   |                   | query monitors     |----------------->|                  |
   |                   | core.init()        |----------------->|                  |
   |                   | start modules      |                  |----------------->|
   |                   | backend.on_start() |----------------->|                  |
   |                   | adopt startup wins |----------------->|                  |
   |                   | enter Running      |                  |                  |
```

### 8.6 Backend Effects

The core communicates native intent through a compact effect set:

```text
MapWindow
UnmapWindow
FocusWindow
FocusRoot
UpdateWindow
WarpPointer
RaiseWindow
LowerWindow
CloseWindow
```

`WindowFlush` carries geometry and border dirtiness for concrete native updates.

### 8.7 Module and Lua Integration

```text
+-------------------+       +------------------+
| Lua config        | ----> | LuaHost          |
+-------------------+       +--------+---------+
                                     |
                                     v
                            +------------------+
                            | Modules          |
                            | - bar            |
                            | - keybindings    |
                            | - keyboard       |
                            | - audio          |
                            | - sysinfo        |
                            | - debug_ui       |
                            +--------+---------+
                                     |
                                     v
                            +------------------+
                            | Runtime / Core   |
                            +------------------+
```

Properties:

- the core does not parse Lua directly
- Lua is hosted by `LuaHost`
- modules expose the feature surface to Lua
- reloadable runtime state is snapshot-based and transactional

## 9. Backend Contract

All backend implementations conform to the same abstract interface.

```text
+-------------------------------------------------------------+
| Backend                                                     |
|-------------------------------------------------------------|
| event_fd()                                                  |
| pump_events(max_events_per_tick)                            |
| render_frame()                                              |
| on_reload_applied()                                         |
| shutdown()                                                  |
| prepare_exec_restart()                                      |
| scan_existing_windows()                                     |
| on_start(Core&)                                             |
| ports()                                                     |
| create_window(WindowId)                                     |
+-------------------------------------------------------------+
```

Backend ports are the stable capability seam for runtime and modules:

- `InputPort`
- `MonitorPort`
- `RenderPort`
- `KeyboardPort`
- `TrayHostPort` where applicable
- optional `GLPort` on debug X11 builds

## 10. X11 Architecture

### 10.1 Process Model

X11 mode is a single-process window manager.

```text
+-------------------------------------------------------------+
| sirenwm-x11                                                 |
|-------------------------------------------------------------|
| main.cpp -> Runtime -> Core -> X11Backend                   |
|                                  |                          |
|                                  v                          |
|                            X11 / XCB server                 |
+-------------------------------------------------------------+
```

### 10.2 Responsibilities of `X11Backend`

`X11Backend` is responsible for:

- becoming the active X11 WM
- EWMH and ICCCM integration
- direct X event pumping
- startup adoption of existing windows
- focus arbitration and pointer tracking
- monitor discovery and RandR updates
- tray hosting via XEmbed
- applying backend effects and window flushes

### 10.3 Internal Layout

```text
backends/x11/
  src/backend/
    x11_backend.cpp   constructor/startup/shutdown
    loop.cpp          event pump + backend effects
    events.cpp        X event handlers
    ewmh.cpp          EWMH/ICCCM integration
    adopt.cpp         startup adoption and restart restore
  src/ports/
    input_port.cpp
    keyboard_port.cpp
    monitor_port.cpp
    render_port.cpp
    gl_port.cpp       optional debug UI path
  src/x11/
    xconn.cpp
    x11_atoms.cpp
    tray_host.cpp
```

### 10.4 X11 Startup and Adoption

```text
Runtime              X11Backend            X server            Core
   |                     |                    |                 |
   | on_start()          |                    |                 |
   |-------------------->| select WM role     |                 |
   |                     |------------------->|                 |
   |                     | query root state   |                 |
   |                     |------------------->|                 |
   |                     | scan existing wins |                 |
   |                     |------------------->|                 |
   | adopt snapshot      |------------------------------------->|
   |                     |                    |                 |
```

This adoption path is essential for:

- normal startup against a non-empty X server
- exec-restart restore
- keeping X11 session state continuous across WM process replacement

### 10.5 X11 Steady-State Event Flow

```text
X server            X11Backend            Core              Runtime/modules
   |                    |                  |                      |
   | X event            |                  |                      |
   |------------------->| decode           |                      |
   |                    | emit command ---->                      |
   |                    |----------------->| mutate               |
   |                    |                  | queue effects        |
   |                    |                  | post typed events -->|
   |                    | render_frame()   |                      |
   |                    | apply X changes  |                      |
   |<-------------------|                  |                      |
```

### 10.6 X11-Specific Architectural Notes

The X11 backend contains native mechanisms that do not exist in the Wayland
controller path, including:

- focus priority arbitration
- pointer barriers for fullscreen confinement
- EWMH desktop and client-list maintenance
- window-type and hint interpretation
- XEmbed tray ownership and icon reparenting

These are backend-local behaviors. They must not leak into the core.

## 11. Wayland Architecture

### 11.1 Process Model

Wayland mode is a two-process architecture.

```text
+---------------------------------------------------+
| WM process: sirenwm-wayland                       |
|---------------------------------------------------|
| main.cpp                                          |
| Runtime                                           |
| Core                                              |
| DisplayServerBackend                              |
| modules / Lua                                     |
+-------------------------+-------------------------+
                          |
                          | AF_UNIX SOCK_SEQPACKET
                          | private control channel
                          v
+---------------------------------------------------+
| display-server process: sirenwm-wayland           |
|                       --display-server            |
|---------------------------------------------------|
| libwlserver                                       |
| - Display                                         |
| - Compositor / Seat / Output / SHM               |
| - XdgShell / XdgDecoration                        |
| - DisplayState                                    |
| - BackendControlServer                            |
| - OverlayManager                                  |
| - OutputX11                                       |
| - XWayland + XWM                                  |
+---------------------------------------------------+
```

### 11.2 Responsibilities of `DisplayServerBackend`

`DisplayServerBackend` is the WM-side IPC backend. It is responsible for:

- opening the control socket to the display-server
- performing handshake and snapshot bootstrap
- maintaining local mirrors of outputs and surfaces
- translating IPC events into typed runtime signals and core commands
- translating backend effects into control messages back to the server

It does not own Wayland resources.

### 11.3 Responsibilities of `libwlserver`

`libwlserver` owns:

- Wayland server support wrappers
- Wayland protocol globals and resource lifetimes
- display-server runtime state
- XWayland and XWM bridge
- nested X11 presentation output
- execution of controller commands received through `BackendControlServer`

### 11.4 `libwlserver` Internal Layout

```text
+----------------------------------------------------------------+
| libwlserver                                                    |
|----------------------------------------------------------------|
| support/    wl::Display, wl::EventLoop, wl::Global             |
| protocol/   Compositor, Shm, Seat, Output, XdgShell,           |
|             XdgDecoration, XWayland shell glue                 |
| runtime/    DisplayState, OverlayManager, display_server.cpp   |
| ipc/        BackendControlServer                               |
| x11/        OutputX11, XWayland, XWM bridge                    |
+----------------------------------------------------------------+
```

### 11.5 Control Plane (`libipc`)

`libipc` is the private control plane between the two processes.

Transport properties:

- Unix domain sockets
- `SOCK_SEQPACKET`
- explicit message header with magic and version
- optional FD passing for overlay buffer updates

Protocol families:

```text
Handshake / session:
  Hello, SnapshotRequest, SnapshotBegin, SnapshotEnd

Topology and surface lifecycle:
  OutputAdded, OutputRemoved,
  SurfaceCreated, SurfaceMapped, SurfaceUnmapped,
  SurfaceDestroyed, SurfaceCommitted,
  SurfaceTitleChanged, SurfaceAppIdChanged

Input:
  Key, Button, PointerMotion, PointerEnter

Control:
  ConfigureSurface, SetSurfaceVisible, SetSurfaceBorder,
  SetSurfaceActivated, SetSurfaceStacking, CloseSurface,
  WarpPointer, SetKeyboardIntercepts, GrabPointer, UngrabPointer

Overlay:
  CreateOverlay, UpdateOverlay, DestroyOverlay,
  OverlayExpose, OverlayButton, OverlayReleased
```

This protocol is private to SirenWM. It is not a public compositor API.

### 11.6 Attach and Snapshot Sequence

```text
DisplayServerBackend                 BackendControlServer / DisplayState
        |                                         |
        | connect                                 |
        |---------------------------------------->|
        | Hello(WmController)                     |
        |---------------------------------------->|
        |                             Hello(DisplayServerHost)
        |<----------------------------------------|
        | SnapshotRequest                         |
        |---------------------------------------->|
        |                             SnapshotBegin
        |<----------------------------------------|
        |                             OutputAdded*
        |<----------------------------------------|
        |                             SurfaceCreated*
        |<----------------------------------------|
        |                             SurfaceMapped*
        |<----------------------------------------|
        |                             SurfaceCommitted*
        |<----------------------------------------|
        |                             SnapshotEnd
        |<----------------------------------------|
```

After `SnapshotEnd`, the connection switches to steady-state events and
controller commands.

### 11.7 Overlay Rendering Path

The WM process renders overlays, but the display-server owns presentation.

```text
module/bar or render client
        |
        v
DisplayServerRenderWindow
        |
        | Cairo draws into image surface
        v
SharedBuffer (memfd + mmap)
        |
        | UpdateOverlay + passed fd
        v
BackendControlServer
        |
        v
OverlayManager
        |
        v
OutputX11 repaint
```

This preserves the intended split:

- WM decides what to render
- display-server decides how and where to present it
- no Wayland surface ownership leaks into the WM controller

### 11.8 Presentation Path

```text
Wayland clients / XWayland clients
              |
              v
      wlserver protocol state
              |
              v
        Compositor buffer views
              |
              v
          OutputX11 repaint
              |
              v
         nested X11 window
```

### 11.9 XWayland Flow

XWayland is fully owned by the display-server process.

```text
X11 app            XWayland          XWM / DisplayState       WM controller
  |                   |                    |                       |
  | connect           |                    |                       |
  |------------------>|                    |                       |
  | create window     |                    |                       |
  |------------------>| track as X surface |                       |
  |                   |------------------->| emit SurfaceCreated   |
  |                   |                    |---------------------->|
  |                   |                    | emit mapped/commit    |
  |                   |                    |---------------------->|
  |                   |                    |                       | Core decides
  |                   |                    |<----------------------| configure/activate
  |                   | apply X11 action   |                       |
  |<------------------|                    |                       |
```

The key architectural point is that XWayland interop belongs to `libwlserver`,
not to `DisplayServerBackend`.

### 11.10 Wayland Steady-State Event Flow

```text
Wayland/XWayland        libwlserver           DisplayServerBackend      Core
      |                      |                        |                  |
      | native lifecycle     |                        |                  |
      |--------------------->| update DisplayState    |                  |
      |                      | emit IPC event         |                  |
      |                      |----------------------->| translate        |
      |                      |                        |----------------->|
      |                      |                        |                  | mutate
      |                      |                        |                  | queue effects
      |                      |<-----------------------| control messages |
      | apply native action  |                        |                  |
```

## 12. Reload and Restart Sequences

### 12.1 Config Reload

Reload is runtime-coordinated and transactional.

```text
User action / keybinding    Runtime             Config/Lua            Core/modules
         |                    |                     |                      |
         | request_reload()   |                     |                      |
         |------------------->| mark pending        |                      |
         |                    | tick() consumes     |                      |
         |                    |-------------------->| reload config        |
         |                    |                     | rebuild state        |
         |                    |<--------------------| success or failure   |
         |                    | commit or rollback  |                      |
         |                    |------------------------------------------->|
```

Properties:

- reload is not in-place mutation without guardrails
- failure rolls back to the previous valid runtime state
- the process remains alive on reload failure

### 12.2 Exec-Restart

```text
User action           Runtime              Backend                Process image
    |                    |                    |                         |
    | request_exec_restart()                  |                         |
    |------------------->|                    |                         |
    |                    | save restart state |                         |
    |                    |------------------->| prepare_exec_restart()  |
    |                    |<-------------------|                         |
    |                    | execvp(self) -------------------------------->|
```

Behavior by backend:

- X11 restarts by re-adopting windows from the X server plus saved restart
  metadata
- Wayland relies on the display-server surviving and the WM controller
  reattaching by snapshot

### 12.3 Wayland Controller Reattach

```text
New WM process        display-server         DisplayState / control
      |                    |                        |
      | connect            |                        |
      |------------------->|                        |
      | Hello              |                        |
      |------------------->|                        |
      | SnapshotRequest    |                        |
      |------------------->|----------------------->|
      |                    | emit full snapshot     |
      |<--------------------------------------------|
      | rebuild mirrors and resume control          |
```

This is the key architectural reason for the IPC split in Wayland mode.

## 13. Testing Architecture

SirenWM validates behavior at three layers.

### 13.1 Unit and Module Tests

```text
build-test/
  sirenwm_core_tests
  sirenwm_module_tests
```

These verify reducers, module logic, and typed runtime contracts.

### 13.2 X11 Integration Tests

`tests/integration/run_tests.sh` launches:

```text
Xephyr -> sirenwm-x11 -> test X11 clients
```

Coverage includes:

- layout and focus behavior
- EWMH properties
- workspace switching
- bar and tray behavior
- fullscreen handling
- restart behavior

### 13.3 Wayland Integration Tests

`tests/integration/run_tests_wayland.sh` launches:

```text
Xephyr or Xvfb
  -> sirenwm-wayland --display-server
  -> sirenwm-wayland
  -> Wayland and XWayland test clients
```

Coverage includes:

- socket bring-up
- IPC handshake and snapshot bootstrap
- xdg-shell lifecycle
- overlay path activity
- reload survival
- XWayland interop
- stress and shutdown behavior

## 14. Constraints for Future Changes

Any change should preserve these structural properties:

1. Do not move domain authority out of `Core`.
2. Do not let `DisplayServerBackend` acquire Wayland resource ownership.
3. Do not let `libipc` depend on `Core`, `Runtime`, or `libwlserver` internals.
4. Do not let modules bypass ports, events, or hooks to mutate backend state.
5. Keep reload and restart behavior explicit and testable.
6. Keep shared logic in shared targets; do not recreate soft boundaries through
   ad-hoc includes.

## 15. Primary Source Index

The most useful source anchors for this architecture are:

```text
src/main.cpp
core/include/runtime/runtime.hpp
core/include/domain/core.hpp
core/src/runtime/runtime.cpp
core/src/core/core_dispatch.cpp
backends/x11/include/x11_backend.hpp
backends/wayland/include/display_server_backend.hpp
libipc/include/swm/ipc/backend_protocol.hpp
libwlserver/include/wl/server/runtime/display_state.hpp
libwlserver/src/runtime/display_server.cpp
libwlserver/src/ipc/backend_control_server.cpp
```

These files are the fastest path to validating the architecture described in
this document.
