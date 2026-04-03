# SirenWM Configuration Reference

Config file: `~/.config/sirenwm/init.lua`

The config is plain Lua — variables, functions, and loops all work.
SirenWM reads the `siren` global table after executing the file.

---

## Core

Core settings are always active — no module required.

### `siren.modules`

Ordered list of modules to load. Must be set before any module-specific
config keys.

```lua
siren.modules = { "bar", "monitors", "layout", "rules", "keybindings",
                  "sysinfo", "wallpaper", "process", "keyboard" }
```

### `siren.modifier`

Primary modifier key. Used as `mod` in bind specs and for mouse drag actions.

```lua
siren.modifier = "mod4"   -- super/win key
```

Accepted values: `shift`, `ctrl`, `alt`, `mod1`–`mod5`, `super`, `win`.

### `siren.theme`

Visual defaults shared across all modules.

```lua
siren.theme = {
    dpi          = 96,
    cursor_size  = 16,
    cursor_theme = "default",

    font   = "monospace:size=9",
    bg     = "#111111",
    fg     = "#bbbbbb",
    alt_bg = "#222222",
    alt_fg = "#eeeeee",
    accent = "#005577",

    gap    = 4,
    border = {
        thickness  = 1,
        focused    = "#005577",
        unfocused  = "#222222",
    },
}
```

| Field              | Type    | Description                              |
| ------------------ | ------- | ---------------------------------------- |
| `dpi`              | integer | DPI for font rendering                   |
| `cursor_size`      | integer | X cursor size in pixels                  |
| `cursor_theme`     | string  | X cursor theme name                      |
| `font`             | string  | Pango font description                   |
| `bg`               | string  | Base background color                    |
| `fg`               | string  | Base foreground color                    |
| `alt_bg`           | string  | Alternate background (inactive items)    |
| `alt_fg`           | string  | Alternate foreground (inactive items)    |
| `accent`           | string  | Accent color (focused items, highlights) |
| `gap`              | integer | Gap between tiled windows in pixels      |
| `border.thickness` | integer | Window border width in pixels            |
| `border.focused`   | string  | Border color for the focused window      |
| `border.unfocused` | string  | Border color for all other windows       |

### `siren.behavior`

```lua
siren.behavior = {
    follow_moved_window = false,
    focus_new_window    = false,
}
```

| Key                   | Type    | Default | Description                                           |
| --------------------- | ------- | ------- | ----------------------------------------------------- |
| `follow_moved_window` | boolean | `false` | Switch to target workspace when moving a window there |
| `focus_new_window`    | boolean | `true`  | Automatically focus newly mapped windows              |

### `siren.workspaces`

Global workspace pool. IDs are 1-based in the Lua API.

```lua
siren.workspaces = {
    { name = "[1]",         monitor = "primary" },
    { name = "[2] Browser", monitor = "primary" },
    { name = "[3]",         monitor = "right"   },
}
```

| Field     | Type   | Required | Description                               |
| --------- | ------ | -------- | ----------------------------------------- |
| `name`    | string | yes      | Display name shown in the bar             |
| `monitor` | string | no       | Preferred monitor alias for this workspace|

### `siren.monitors`

Physical output definitions.

```lua
siren.monitors = {
    {
        name     = "primary",
        output   = "HDMI-1",
        width    = 1920,
        height   = 1080,
        refresh  = 60,
        rotation = "normal",
        enabled  = true,
    },
}
```

| Field      | Type    | Required | Description                                     |
| ---------- | ------- | -------- | ----------------------------------------------- |
| `name`     | string  | yes      | Alias used in `compose_monitors` and workspaces |
| `output`   | string  | yes      | RandR output name (e.g. `HDMI-1`, `eDP-1`)      |
| `width`    | integer | yes      | Horizontal resolution in pixels                 |
| `height`   | integer | yes      | Vertical resolution in pixels                   |
| `rotation` | string  | yes      | `normal`, `left`, `right`, or `inverted`        |
| `enabled`  | boolean | yes      | Whether to activate this output                 |
| `refresh`  | integer | no       | Target refresh rate in Hz                       |

### `siren.compose_monitors`

Describes how monitors are positioned relative to each other.

```lua
siren.compose_monitors = {
    primary = "primary",
    layout  = {
        { monitor = "right", relative_to = "primary", side = "right", shift = 0 },
    },
}
```

| Field         | Type    | Required    | Description                               |
| ------------- | ------- | ----------- | ----------------------------------------- |
| `primary`     | string  | yes         | Alias of the primary monitor              |
| `layout`      | array   | yes         | Placement entries for non-primary monitors|
| `monitor`     | string  | yes         | Monitor alias                             |
| `relative_to` | string  | yes         | Anchor monitor alias                      |
| `side`        | string  | yes         | `left`, `right`, `top`, or `bottom`       |
| `shift`       | integer | no          | Pixel offset along the perpendicular axis |

### Runtime API

```lua
siren.spawn("picom")   -- spawn a process
siren.reload()         -- hot-reload init.lua in-process
siren.restart()        -- replace the WM process via exec (preserves windows)
siren.theme_get()      -- returns the resolved theme table
```

### Window and workspace API

```lua
-- Windows
siren.windows.close()
siren.windows.focus_next()
siren.windows.focus_prev()
siren.windows.move_to(n)            -- move focused window to workspace n (1-based)
siren.windows.move_to_monitor(n)    -- move focused window to monitor n (1-based)
siren.windows.toggle_floating()
siren.windows.zoom()                -- swap focused window with master
siren.windows.set_layout("tile")
siren.windows.adj_master_factor(d)  -- e.g. -0.05
siren.windows.inc_master(d)         -- e.g. 1

-- Workspaces
siren.workspace.switch(n)           -- switch to workspace n (1-based)

-- Monitors
siren.monitor.focused()             -- returns { index, x, y, width, height, name }
siren.monitor.list()                -- returns array of monitor info tables
siren.monitor.focus(n)              -- focus monitor n (1-based)
```

---

## Modules

Modules are optional feature sets loaded in the order specified by
`siren.modules`.

---

### `layout`

Tiling layout engine.

```lua
siren.layout = {
    name          = "tile",
    master_factor = 0.55,
}
```

| Field           | Type   | Default  | Description                                 |
| --------------- | ------ | -------- | ------------------------------------------- |
| `name`          | string | `"tile"` | Initial layout: `"tile"` or `"monocle"`     |
| `master_factor` | float  | `0.55`   | Master area ratio, clamped to `[0.1, 0.9]`  |

Gap and border come from `siren.theme`.

---

### `monitors`

Applies the monitor topology from `siren.monitors` / `siren.compose_monitors`
via RandR and handles hotplug. No extra config keys.

---

### `keybindings`

Key and mouse binding engine.

#### `siren.binds`

```lua
siren.binds = {
    { "mod+Return",      function() siren.spawn("alacritty") end },
    { "mod+shift+q",     function() siren.windows.close() end },
    { "mod+j",           function() siren.windows.focus_next() end },
    { "mod+shift+space", function() siren.windows.toggle_floating() end },
    { "mod+1",           function() siren.workspace.switch(1) end },
    { "mod+shift+1",     function() siren.windows.move_to(1) end },
    { "mod+ctrl+1",      function() siren.monitor.focus(1) end },
}
```

`mod` resolves to `siren.modifier`. Tokens are case-insensitive.

#### `siren.mouse`

```lua
siren.mouse = {
    { "mod+Button1", "move"   },
    { "mod+Button3", "resize" },
    { "mod+Button2", "float"  },
}
```

Built-in action strings: `"move"`, `"resize"`, `"float"`.

---

### `rules`

Assigns windows to workspaces or forces floating state based on WM_CLASS.
Rules are applied once when a window is first mapped.

```lua
siren.rules = {
    { class = "librewolf",  workspace = 2 },
    { class = "steam",      workspace = 7, isfloating = true },
    { class = "steam", instance = "steamwebhelper", workspace = 7 },
}
```

| Field        | Type    | Required        | Description                                    |
| ------------ | ------- | --------------- | ---------------------------------------------- |
| `class`      | string  | at least one of | WM_CLASS class component (case-insensitive)    |
| `instance`   | string  | at least one of | WM_CLASS instance component (case-insensitive) |
| `workspace`  | integer | no              | Target workspace (1-based)                     |
| `isfloating` | boolean | no              | Force floating state                           |

---

### `bar`

Status bar at the top and/or bottom of each monitor.

```lua
siren.bar = {
    widgets = {
        clock = {
            fn       = function() return os.date(" [%H:%M:%S] ") end,
            interval = 1,
        },
        load = function() return string.format(" %.2f ", siren.sys.loadavg()["1"]) end,
    },

    top = {
        height = 18,
        left   = { "tags" },
        center = { "title" },
        right  = { "tray" },
    },
    bottom = {
        height = 18,
        left   = { "clock" },
        center = { "load" },
        right  = { "sysinfo" },
    },
}
```

**`top` / `bottom` fields:**

| Field    | Type    | Description                                  |
| -------- | ------- | -------------------------------------------- |
| `height` | integer | Bar height in pixels                         |
| `font`   | string  | Pango font, falls back to `siren.theme.font` |
| `left`   | array   | Widget names for the left zone               |
| `center` | array   | Widget names for the center zone             |
| `right`  | array   | Widget names for the right zone              |
| `colors` | table   | Color overrides (see below)                  |

**`colors` keys:** `normal_bg`, `normal_fg`, `focused_bg`, `focused_fg`,
`bar_bg`, `status_fg`. All fall back to `siren.theme`.

**Built-in widget names:** `tags`, `title`, `tray`.

**`widgets`:** key-value table. Each value is either a plain function or
`{ fn = function, interval = seconds }`. The function must return a string.
Widget output is cached and refreshed at the given interval (default: every
base tick, ~1 s). Widgets that don't use an interval are called on every
redraw (e.g. `kbd` callback style).

---

### `rules` — already covered above

---

### `process`

Process autostart and lifecycle management.

```lua
siren.autostart = {
    { cmd = "picom --experimental-backends", policy = "once"    },
    { cmd = "nm-applet",                     policy = "restart" },
}
```

| Field    | Type   | Required | Description                        |
| -------- | ------ | -------- | ---------------------------------- |
| `cmd`    | string | yes      | Shell command to execute           |
| `policy` | string | no       | Restart policy (default: `"once"`) |

**Policies:**

| Value              | Behavior                                     |
| ------------------ | -------------------------------------------- |
| `once`             | Start once; do not restart on exit           |
| `restart`          | Always restart on exit                       |
| `restart-on-error` | Restart only on non-zero exit code           |

Processes with policy `once` survive `siren.restart()`.

---

### `wallpaper`

Sets a wallpaper per monitor. Keyed by monitor alias.

```lua
siren.wallpaper = {
    primary = { image = "/path/to/bg.png", mode = "stretch" },
    right   = { image = "/path/to/bg.jpg", mode = "zoom"    },
}
```

| Field   | Type   | Required | Description                                      |
| ------- | ------ | -------- | ------------------------------------------------ |
| `image` | string | yes      | Path to image file                               |
| `mode`  | string | no       | `stretch` (default), `zoom`, `center`, or `tile` |

---

### `sysinfo`

Read-only system metrics exposed as `siren.sys.*`. Intended for use in
bar widgets.

| Function                 | Returns                                                                         |
| ------------------------ | ------------------------------------------------------------------------------- |
| `siren.sys.cpu()`        | CPU usage percent as a number                                                   |
| `siren.sys.mem()`        | Table: `{ used, total, percent }` (GB / percent)                                |
| `siren.sys.uptime()`     | Uptime in seconds as a number                                                   |
| `siren.sys.loadavg()`    | Table: `{ ["1"], ["5"], ["15"] }` (load averages)                               |
| `siren.sys.net_ip()`     | Primary non-loopback IP address string                                          |
| `siren.sys.disks()`      | Array of `{ device, mountpoint, total, used, free, percent }` (bytes / percent) |
| `siren.sys.kbd_layout()` | Current keyboard layout name string (e.g. `"us"`)                               |

Example:

```lua
local m = siren.sys.mem()
string.format("MEM %.1f/%.1f GB (%.0f%%)", m.used, m.total, m.percent)

local d = siren.sys.disks()
for _, disk in ipairs(d) do
    print(disk.mountpoint, disk.percent)
end
```

---

### `keyboard`

Applies XKB keyboard layout and options at startup and on reload.
Restores the original layout on exit.

```lua
siren.keyboard = {
    layouts = "us,ru",
    options = "grp:alt_shift_toggle,terminate:ctrl_alt_bksp",
}
```

| Field     | Type   | Required | Description                                      |
| --------- | ------ | -------- | ------------------------------------------------ |
| `layouts` | string | yes      | Comma-separated XKB layout names                 |
| `options` | string | no       | Comma-separated XKB options                      |

The bar `kbd` widget reads the active layout via `siren.sys.kbd_layout()`
and updates automatically on XKB state change events — no polling.
