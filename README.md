# SirenWM

X11 tiling window manager with Lua configuration and a clean module architecture.

![SirenWM](picture.png)

## About

SirenWM is a keyboard-first tiling WM for X11. Configuration is plain Lua — no recompile needed for everyday changes. The codebase has explicit architecture boundaries so adding behavior means writing Lua or a module, not touching core logic.

## Features

- Tile and monocle layouts with configurable master factor, gap, and border
- Multi-monitor with RandR hotplug, compose graph, and workspace migration
- Per-monitor workspace pools with deterministic topology restore
- Full state preservation across hot reload and exec-restart
- Focus-follows-mouse with cross-monitor support
- Window rules by WM_CLASS
- Hot config reload and exec-restart with Lua syntax pre-check
- Cairo/Pango status bar with widget API and system tray (XEmbed)
- Fullscreen and borderless game support (EWMH, MOTIF, Wine/Proton, SDL2, LibGDX)
- Pointer barriers: cursor confined to monitor while a fullscreen or borderless window is active
- Mod+drag move and resize for floating windows
- Autostart process management
- ICCCM WM_DELETE_WINDOW and WM_TAKE_FOCUS support

## Getting Started

### 1. Dependencies

```bash
# Debian/Ubuntu
sudo apt install \
  cmake pkg-config g++ \
  libx11-dev libx11-xcb-dev \
  libxcb1-dev libxcb-randr0-dev libxcb-keysyms1-dev \
  libxkbcommon-dev libxkbfile-dev libxfixes-dev liblua5.4-dev \
  libcairo2-dev libpango1.0-dev libfontconfig1-dev libspdlog-dev
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

Add to your `.xinitrc` or display manager session:

```bash
exec /path/to/output/sirenwm
```

## Documentation

- [`CONFIG.md`](CONFIG.md) — full Lua configuration reference
- [`init.lua.example`](init.lua.example) — minimal single-monitor starter config

## License

GPL-2.0 — see [`LICENSE`](LICENSE).
