# SirenWM

SirenWM is a tiling window manager with selectable X11 and Wayland builds,
transactional Lua reloads, integrated modules, and a strict split between the
WM core and platform backends.

![SirenWM](picture.png)

## Overview

SirenWM is organized around three layers:

- `Runtime` owns lifecycle, configuration loading, modules, and the unified
  event loop.
- `Core` owns authoritative window-manager state and reducer-style command
  handling.
- backends own native protocol integration and presentation.

There are two build profiles:

- `sirenwm-x11`: direct X11 window-manager backend
- `sirenwm-wayland`: WM controller process plus SirenWM display-server process

The current Wayland implementation uses SirenWM's own embedded display-server
and presents through a nested X11 output. It is not a generic Wayland client
backend for third-party compositors.

## Highlights

- Tile, monocle, and Lua-defined layouts
- Multi-monitor workspace composition
- Hot config reload with rollback on failure
- Exec-restart flow that preserves session state
- Statically linked modules for bars, keybindings, keyboard, audio, sysinfo,
  and debug tooling
- X11 support with EWMH, ICCCM, RandR, XEmbed tray, and fullscreen interop
- Wayland support through a private IPC split between the WM controller and the
  embedded display-server
- XWayland support in Wayland mode
- Integration tests for both X11 and Wayland paths

## Architecture at a Glance

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
                                  |      +-----------------------------+
                                  |                                    |
                                  v                                    v
                       +----------------------+             +----------------------+
                       | X11Backend           |             | DisplayServerBackend |
                       | direct X11 adapter   |             | WM-side IPC adapter  |
                       +----------+-----------+             +----------+-----------+
                                  |                                    |
                                  v                                    v
                            +-----------+                      +------------------+
                            | X11 server |                      | libwlserver      |
                            +-----------+                      | display-server   |
                                                               +------------------+
```

For the full architectural description, see [`ARCHITECTURE.md`](ARCHITECTURE.md).

## Requirements

| Requirement | Minimum |
| ----------- | ------- |
| C++ compiler | GCC 12 or Clang 16 |
| CMake | 3.14 |
| Lua | 5.4 |
| X11 build | X11 server |
| Wayland build | SirenWM display-server mode, currently hosted in X11 |

## Dependencies

### X11 backend

#### Debian / Ubuntu

```bash
sudo apt install \
  cmake pkg-config g++ \
  libx11-dev libx11-xcb-dev \
  libxcb1-dev libxcb-randr0-dev libxcb-keysyms1-dev \
  libxkbcommon-dev libxkbfile-dev libxfixes-dev liblua5.4-dev \
  libcairo2-dev libpango1.0-dev libfontconfig1-dev libfreetype-dev libpng-dev \
  libspdlog-dev
```

#### Fedora

```bash
sudo dnf install \
  cmake make pkgconf gcc-c++ \
  libX11-devel libxcb-devel xcb-util-keysyms-devel \
  libxkbcommon-devel libxkbfile-devel libXfixes-devel lua-devel \
  cairo-devel pango-devel fontconfig-devel freetype-devel libpng-devel \
  spdlog-devel
```

#### Arch Linux

```bash
sudo pacman -S \
  cmake make pkgconf gcc \
  libx11 libxcb xcb-util-keysyms \
  libxkbcommon libxkbfile libxfixes lua \
  cairo pango fontconfig freetype2 libpng \
  spdlog
```

### Wayland backend

#### Debian / Ubuntu

```bash
sudo apt install \
  cmake pkg-config gcc g++ \
  libwayland-dev libwayland-bin wayland-protocols \
  libxkbcommon-dev \
  libx11-dev libx11-xcb-dev libxfixes-dev \
  libxcb1-dev libxcb-randr0-dev libxcb-composite0-dev libxcb-keysyms1-dev \
  liblua5.4-dev libspdlog-dev \
  libcairo2-dev libpango1.0-dev libfontconfig1-dev libfreetype-dev libpng-dev
```

#### Arch Linux

```bash
sudo pacman -S \
  cmake make pkgconf gcc \
  wayland wayland-protocols \
  libxkbcommon \
  libx11 libxcb xcb-util-keysyms libxfixes \
  lua spdlog cairo pango fontconfig freetype2 libpng
```

## Build

```bash
# X11 build
cmake -S . -B build -DSIRENWM_BACKEND=x11
cmake --build build -j$(nproc)

# Wayland build
cmake -S . -B build-wayland -DSIRENWM_BACKEND=wayland
cmake --build build-wayland -j$(nproc)
```

Generated binaries:

```text
output/sirenwm-x11
output/sirenwm-wayland
```

### Optional debug UI

The ImGui debug overlay is currently available on the X11 build.

```bash
# Debian / Ubuntu
sudo apt install libegl-dev libgl-dev

# Fedora
sudo dnf install mesa-libEGL-devel mesa-libGL-devel

# Arch Linux
sudo pacman -S mesa

cmake -S . -B build -DSIRENWM_BACKEND=x11 -DSIRENWM_DEBUG_UI=ON
cmake --build build -j$(nproc)
```

## Configuration

On first start, SirenWM writes a default config to:

```text
~/.config/sirenwm/init.lua
```

To start from the annotated example instead:

```bash
mkdir -p ~/.config/sirenwm
cp init.lua.example ~/.config/sirenwm/init.lua
```

Minimal example:

```lua
local kb  = require("keybindings")
local bar = require("bar")

siren.modifier = "mod4"

siren.workspaces = {
  { name = "[1]" },
  { name = "[2]" },
  { name = "[3]" },
}

siren.theme = {
  font = "monospace:size=9",
  bg = "#111111",
  fg = "#cccccc",
  accent = "#005577",
  gap = 4,
  border = { thickness = 1 },
}

bar.settings = {
  top = {
    height = 18,
    left   = { siren.load("widgets.tags") },
    center = { siren.load("widgets.title") },
  },
}

kb.binds = {
  { "mod+Return",  function() siren.spawn("xterm") end },
  { "mod+shift+q", function() siren.win.close() end },
  { "mod+r",       function() siren.reload() end },
}
```

For the full configuration reference, see [`CONFIG.md`](CONFIG.md).

## Run

### X11

Typical `~/.xinitrc` entry:

```bash
exec /path/to/output/sirenwm-x11
```

### Wayland

The Wayland build is designed to run against SirenWM's own display-server.

Controller mode:

```bash
/path/to/output/sirenwm-wayland
```

If `WAYLAND_DISPLAY` is not already set, this will spawn the display-server
process automatically.

Display-server only:

```bash
/path/to/output/sirenwm-wayland --display-server --size 1920x1080
```

Important operational note:

- the current display-server presents through an X11 output window
- for development and tests, use an existing X session, Xephyr, or Xvfb
- `output/run-debug-wl.sh` is the easiest local nested debug entrypoint

System install:

```bash
sudo cmake --install build
sudo cmake --install build-wayland
```

Install the build profile you want to run. Session desktop files are installed
for the selected backend build.

## Tests

```bash
# X11 integration
bash tests/integration/run_tests.sh

# Wayland integration
bash tests/integration/run_tests_wayland.sh

# unit and module tests
cmake -S . -B build-test -DBUILD_TESTING=ON
cmake --build build-test -j$(nproc)
ctest --test-dir build-test --output-on-failure
```

The Wayland integration suite runs a nested process stack:

```text
Xephyr or Xvfb
  -> sirenwm-wayland --display-server
  -> sirenwm-wayland
  -> Wayland and XWayland test clients
```

## Default Keybindings

`mod` defaults to the Super/Win key and is configurable in Lua.

| Binding | Action |
| ------- | ------ |
| `mod+Return` | Launch `xterm` |
| `mod+shift+q` | Close focused window |
| `mod+j` / `mod+k` | Focus next / previous window |
| `mod+shift+Return` | Zoom focused window to master |
| `mod+shift+space` | Toggle floating |
| `mod+h` / `mod+l` | Shrink / grow master area |
| `mod+i` / `mod+d` | Increase / decrease master count |
| `mod+t` | Switch to tile layout |
| `mod+m` | Switch to monocle layout |
| `mod+1` ... `mod+9` | Switch to workspace 1 ... 9 |
| `mod+shift+1` ... `mod+shift+9` | Move window to workspace 1 ... 9 |
| `mod+ctrl+1` ... `mod+ctrl+8` | Focus monitor 1 ... 8 |
| `mod+ctrl+shift+1` ... `mod+ctrl+shift+8` | Move window to monitor 1 ... 8 |
| `mod+r` | Hot reload |
| `mod+shift+r` | Exec-restart |
| `mod+Button1` | Drag-move floating window |
| `mod+Button3` | Drag-resize floating window |
| `mod+Button2` | Toggle floating |

## Documentation

- [`ARCHITECTURE.md`](ARCHITECTURE.md) — full system architecture
- [`CONFIG.md`](CONFIG.md) — Lua configuration reference
- [`init.lua.example`](init.lua.example) — annotated example configuration

## License

GPL-2.0 — see [`LICENSE`](LICENSE).
