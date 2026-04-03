# SirenWM

X11 tiling window manager with Lua configuration and a clean module architecture.

## About

SirenWM is a keyboard-first tiling WM for X11. It covers the common tiling workflow — workspaces, multi-monitor, floating, focus cycling, window rules — and exposes all of it through a Lua config file rather than requiring you to patch and recompile the source.

The goal is a maintainable codebase with explicit architecture boundaries, where adding or changing behavior means writing Lua or a feature module — not touching core window management logic.

**Good fit if you want:**

- Lua config with hot reload and no compile step for everyday changes
- Multi-monitor with explicit output composition and workspace pinning
- A codebase you can read and extend without archeology

**Not a good fit if you want:**

- A compositor (no blur, transparency pipeline, or animations)
- Wayland
- A minimal ~2000 line codebase à la dwm

## Features

- Tile and monocle layouts with configurable master factor, gap, and border
- Multi-monitor with RandR hotplug, compose graph, and workspace migration
- Per-monitor workspace pools with deterministic topology restore
- Full state preservation across hot reload and exec-restart
- Focus-follows-mouse with cross-monitor support
- Window rules by WM_CLASS
- Hot config reload and exec-restart with Lua syntax pre-check
- Cairo/Pango status bar with widget API and system tray (XEmbed)
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
  libxkbcommon-dev liblua5.4-dev \
  libcairo2-dev libpango1.0-dev libfontconfig1-dev
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
