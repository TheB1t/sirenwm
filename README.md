# SirenWM

X11 tiling window manager. Configuration is plain Lua — no recompile for everyday changes.

![SirenWM](picture.png)

## About

SirenWM is a keyboard-first tiling WM for X11. It runs without a compositor, display daemon, or D-Bus dependency. The C++ core handles X11 protocol, window layout, and the event loop; everything user-visible — keybindings, rules, autostart, wallpaper, widgets — is written in Lua and hot-reloaded without restarting the WM. The config is a plain Lua script: variables, loops, and functions all work.

## Requirements

| Requirement | Minimum |
| ----------- | ------- |
| X server | X11 (XCB, RandR) — Wayland not supported |
| C++ compiler | GCC 12 / Clang 16 (C++20) |
| CMake | 3.14 |
| Lua | 5.4 |

## Features

### Window management

- Tile and monocle layouts; custom layouts in Lua with full geometry control
- Master factor, gap, and border width adjustable at runtime
- Multi-monitor with RandR hotplug, compose graph, and workspace migration
- Per-monitor workspace pools with deterministic topology restore after restart
- Focus-follows-mouse and click-to-focus; pointer barriers confine cursor to active fullscreen monitor
- Floating windows with mod+drag move/resize; WM_NORMAL_HINTS size constraints enforced
- Fullscreen and borderless game support (EWMH, MOTIF, Wine/Proton, SDL2, LibGDK)

### Configuration

- Hot config reload (`mod+r`) and exec-restart (`mod+shift+r`) with Lua syntax pre-check
- Fallback to built-in default config when user config fails to parse
- `siren.load()` for optional modules — returns a null-object on failure so the config keeps working
- Window rules, process autostart, per-monitor wallpapers — all via Lua modules

### Status bar

- Cairo/Pango bar at top and/or bottom of each monitor
- Built-in widgets: workspace tags, focused window title, system tray (XEmbed)
- Lua widget API: write `render()`, set `interval`, drop into any bar zone
- Urgent workspaces highlighted in the tag strip

### Protocol compliance

- ICCCM: WM_DELETE_WINDOW, WM_TAKE_FOCUS, WM_HINTS (InputHint, UrgencyHint), WM_NORMAL_HINTS
- EWMH: `_NET_WM_STATE` fullscreen, `_NET_ACTIVE_WINDOW`, `_NET_CLOSE_WINDOW`, client list

### Developer

- ImGui debug overlay for live WM state inspection (`-DSIRENWM_DEBUG_UI=ON`)
- Runtime lifecycle FSM (Idle → Configured → Starting → Running → Stopping → Stopped)

## Architecture

SirenWM is split into a C++ core and Lua modules. The boundary is intentional:
the core never reads `init.lua` directly — it exposes an API and the Lua layer drives it.

| Layer | What lives here |
| ----- | --------------- |
| C++ core | X11/XCB backend, event loop, window manager logic, layout engine, bar renderer |
| C++ modules | `keybindings`, `bar`, `keyboard`, `sysinfo`, `debug_ui` |
| Lua modules | `rules`, `wallpaper`, `autostart` — ship as `lua/swm/*.lua` |
| Lua widgets | `widgets.tags`, `widgets.title`, `widgets.clock`, `widgets.sysinfo`, … |
| User config | `~/.config/sirenwm/init.lua` — loads modules, sets options, defines binds |

C++ modules are loaded with `require("name")` and return an API table.
Lua modules use the same `require()` — or `siren.load()` for optional ones.

## Getting Started

### 1. Dependencies

```bash
# Debian/Ubuntu
sudo apt install \
  cmake pkg-config g++ \
  libx11-dev libx11-xcb-dev \
  libxcb1-dev libxcb-randr0-dev libxcb-keysyms1-dev \
  libxkbcommon-dev libxkbfile-dev libxfixes-dev liblua5.4-dev \
  libcairo2-dev libpango1.0-dev libfontconfig1-dev libspdlog-dev \
  libegl-dev libgl-dev
```

### 2. Build

```bash
cmake -S . -B build
cmake --build build -j$(nproc)
# binary: output/sirenwm
```

### 3. Config

A default config is written to `~/.config/sirenwm/init.lua` automatically on first run.
To start from the full annotated example instead:

```bash
mkdir -p ~/.config/sirenwm
cp init.lua.example ~/.config/sirenwm/init.lua
```

Edit `output = "HDMI-1"` to match your actual RandR output name (`xrandr` to list them).

Minimal working config:

```lua
local kb  = require("keybindings")
local bar = require("bar")

siren.modifier = "mod4"   -- Super/Win key

siren.workspaces = { { name = "[1]" }, { name = "[2]" }, { name = "[3]" } }

siren.theme = { font = "monospace:size=9", bg = "#111111", fg = "#cccccc",
                accent = "#005577", gap = 4, border = { thickness = 1 } }

bar.settings = {
    top = { height = 18,
            left   = { siren.load("widgets.tags") },
            center = { siren.load("widgets.title") } }
}

kb.binds = {
    { "mod+Return",  function() siren.spawn("xterm") end },
    { "mod+shift+q", function() siren.win.close() end },
    { "mod+r",       function() siren.reload() end },
}
for i = 1, 9 do
    table.insert(kb.binds, { "mod+"..i, function() siren.ws.switch(i) end })
end
```

### 4. Run

**xinitrc** — add to `~/.xinitrc`:

```bash
exec /path/to/output/sirenwm
```

**System install** (puts `sirenwm` in PATH and registers the display manager session):

```bash
sudo cmake --install build
```

Then select "SirenWM" from your display manager's session list.

## Default Keybindings

`mod` = Super/Win key (configurable via `siren.modifier`).

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
| `mod+1`…`mod+9` | Switch to workspace 1–9 |
| `mod+shift+1`…`mod+shift+9` | Move window to workspace 1–9 |
| `mod+ctrl+1`…`mod+ctrl+8` | Focus monitor 1–8 |
| `mod+ctrl+shift+1`…`mod+ctrl+shift+8` | Move window to monitor 1–8 |
| `mod+r` | Hot-reload config |
| `mod+shift+r` | Exec-restart (preserves windows) |
| `mod+Button1` | Drag-move floating window |
| `mod+Button3` | Drag-resize floating window |
| `mod+Button2` | Toggle floating |

## Documentation

- [`CONFIG.md`](CONFIG.md) — full Lua configuration reference
- [`init.lua.example`](init.lua.example) — annotated multi-monitor config

## License

GPL-2.0 — see [`LICENSE`](LICENSE).
