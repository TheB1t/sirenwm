# SirenWM

X11 tiling window manager with Lua configuration and a clean module architecture.

![SirenWM](picture.png)

## About

SirenWM is a keyboard-first tiling WM for X11. Configuration is plain Lua — no recompile needed for everyday changes. The codebase has explicit architecture boundaries so adding behavior means writing Lua or a module, not touching core logic.

## Features

- Tile, monocle, and custom Lua layouts with configurable master factor, gap, and border
- Multi-monitor with RandR hotplug, compose graph, and workspace migration
- Per-monitor workspace pools with deterministic topology restore
- Full state preservation across hot reload and exec-restart
- Focus-follows-mouse with cross-monitor support; click-to-focus (dwm-style passive grab)
- Window rules, process autostart, and per-monitor wallpapers via Lua modules
- Hot config reload and exec-restart with Lua syntax pre-check; fallback to default config on error
- Cairo/Pango status bar with custom widget API and system tray (XEmbed)
- Urgency hint support: urgent workspaces highlighted in the bar
- Fullscreen and borderless game support (EWMH, MOTIF, Wine/Proton, SDL2, LibGDK); renderer steered to cursor monitor at launch
- Pointer barriers: cursor confined to monitor while a fullscreen or borderless window is active
- Mod+drag move and resize for floating windows; WM_NORMAL_HINTS size constraints enforced
- `siren.load()` for optional modules — returns a null-object on failure, config keeps working
- Runtime lifecycle FSM for deterministic init/reload/shutdown sequencing
- ImGui debug overlay for runtime WM state inspection (optional, `-DSIRENWM_DEBUG_UI=ON`)
- ICCCM WM_DELETE_WINDOW, WM_TAKE_FOCUS, WM_HINTS (InputHint, UrgencyHint) support

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
cmake --build build -j
# binary: output/sirenwm
```

### 3. Config

Copy the example config and adjust to your setup:

```bash
mkdir -p ~/.config/sirenwm
cp init.lua.example ~/.config/sirenwm/init.lua
```

Edit `output = "HDMI-1"` to match your actual RandR output name (`xrandr` to list them).

See [`CONFIG.md`](CONFIG.md) for the full configuration reference.

### 4. Run

**xinitrc:** add to your `.xinitrc`:

```bash
exec /path/to/output/sirenwm
```

**System install** (puts `sirenwm` in PATH and registers the DM session):

```bash
sudo cmake --install build
```

Then select "SirenWM" from your display manager's session list.

## Documentation

- [`CONFIG.md`](CONFIG.md) — full Lua configuration reference
- [`init.lua.example`](init.lua.example) — example multi-monitor config

## License

GPL-2.0 — see [`LICENSE`](LICENSE).
