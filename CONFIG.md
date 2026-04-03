# SirenWM Configuration Reference

Config file: `~/.config/sirenwm/init.lua`

The config is plain Lua — variables, functions, and loops all work.
SirenWM reads the `siren` global table after executing the file.

---

## Part 1 — Core

The core handles window state, workspaces, monitors, and focus.
These settings are always available, no modules required.

### `siren.modules`

Ordered list of feature modules to load. Must be set before any
module-specific config keys are used.

```lua
siren.modules = { "layout", "monitors", "keybindings", "rules", "bar", "process" }
```

### `siren.modifier`

Primary modifier key. Used as `Mod` token in bind specs and as the
default modifier for mouse drag actions.

```lua
siren.modifier = "mod4"   -- super/win
```

Accepted values: `shift`, `ctrl`/`control`, `alt`/`mod1`,
`mod2`–`mod5`, `super`/`win` (alias for `mod4`).

### `siren.theme`

Visual defaults shared across all modules (bar, borders, fonts).

```lua
siren.theme = {
  font       = "monospace:size=9",
  bg         = "#111111",
  fg         = "#bbbbbb",
  alt_bg     = "#222222",
  alt_fg     = "#eeeeee",
  accent     = "#005577",
  gap        = 4,
  border = {
    thickness  = 1,
    focused    = "#005577",
    unfocused  = "#222222",
  },
}
```

| Field              | Type    | Description                              |
| ------------------ | ------- | ---------------------------------------- |
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
  focus_new_window    = true,
}
```

| Key                   | Type    | Default | Description                                      |
| --------------------- | ------- | ------- | ------------------------------------------------ |
| `follow_moved_window` | boolean | `false` | Switch to target workspace when moving a window there |
| `focus_new_window`    | boolean | `true`  | Automatically focus newly mapped windows         |

### `siren.workspaces`

Global workspace pool. IDs are 1-based in the Lua API.

```lua
siren.workspaces = {
  { name = "[1]", monitor = "primary" },
  { name = "[2]", monitor = "primary" },
  { name = "[3]", monitor = "right"   },
}
```

| Field     | Type   | Required | Description                                     |
| --------- | ------ | -------- | ----------------------------------------------- |
| `name`    | string | yes      | Display name shown in the bar                   |
| `monitor` | string | no       | Preferred monitor alias for this workspace      |

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

| Field      | Type    | Required | Description                                    |
| ---------- | ------- | -------- | ---------------------------------------------- |
| `name`     | string  | yes      | Alias used in `compose_monitors` and workspaces|
| `output`   | string  | yes      | RandR output name (e.g. `HDMI-1`, `eDP-1`)    |
| `width`    | integer | yes      | Horizontal resolution in pixels                |
| `height`   | integer | yes      | Vertical resolution in pixels                  |
| `rotation` | string  | yes      | `normal`, `left`, `right`, or `inverted`       |
| `enabled`  | boolean | yes      | Whether to activate this output                |
| `refresh`  | integer | no       | Target refresh rate in Hz                      |

### `siren.compose_monitors`

Describes how monitors are positioned relative to each other.

```lua
siren.compose_monitors = {
  primary = "primary",
  layout  = {
    { monitor = "primary" },
    { monitor = "right", relative_to = "primary", side = "right", shift = 0 },
  },
}
```

| Field         | Type    | Required    | Description                               |
| ------------- | ------- | ----------- | ----------------------------------------- |
| `primary`     | string  | yes         | Alias of the primary monitor              |
| `layout`      | array   | yes         | Ordered placement entries                 |
| `monitor`     | string  | yes         | Monitor alias                             |
| `relative_to` | string  | non-primary | Anchor monitor alias                      |
| `side`        | string  | non-primary | `left`, `right`, `top`, or `bottom`       |
| `shift`       | integer | no          | Pixel offset along the perpendicular axis |

### Runtime control functions

```lua
siren.spawn("picom")   -- spawn a process
siren.reload()         -- hot-reload init.lua in-process (no reconnect)
siren.restart()        -- replace the WM process via exec (preserves windows)
```

### Window and workspace actions

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
siren.windows.adj_master_factor(d)  -- e.g. -0.02
siren.windows.inc_master(d)         -- e.g. 1

-- Workspaces
siren.workspace.switch(n)           -- switch to workspace n (1-based)

-- Monitors
siren.monitor.focused()             -- returns { index, x, y, width, height, name }
siren.monitor.list()                -- returns array of { index, x, y, width, height, name }
siren.monitor.focus(n)              -- focus monitor n (1-based)
```

---

## Part 2 — Modules

Modules are optional feature sets loaded in the order specified by
`siren.modules`. Each module registers its own config keys and Lua APIs
after the core is initialized.

---

### `layout`

Tiling layout engine. Manages how windows are arranged on screen.

```lua
siren.layout = {
  name          = "tile",
  master_factor = 0.55,
}
```

| Field           | Type   | Default  | Description                                |
| --------------- | ------ | -------- | ------------------------------------------ |
| `name`          | string | `"tile"` | Initial layout algorithm                   |
| `master_factor` | float  | `0.55`   | Master area ratio, clamped to `[0.1, 0.9]` |

Built-in layouts: `"tile"`, `"monocle"`.

Gap and border width come from `siren.theme.gap` and `siren.theme.border.thickness`.

---

### `monitors`

Applies the monitor topology from `siren.monitors` / `siren.compose_monitors`
via RandR and handles hotplug events. No extra config keys beyond what is
described in the Core section.

---

### `keybindings`

Key and mouse binding engine.

#### `siren.binds` — keyboard bindings

```lua
siren.binds = {
  { "Mod+Return",    function() siren.spawn("xterm") end },
  { "Mod+Shift+q",   function() siren.windows.close() end },
  { "Mod+j",         function() siren.windows.focus_next() end },
  { "Mod+k",         function() siren.windows.focus_prev() end },
  { "Mod+Return",    function() siren.windows.zoom() end },
  { "Mod+t",         function() siren.windows.toggle_floating() end },
  { "Mod+1",         function() siren.workspace.switch(1) end },
  { "Mod+Shift+1",   function() siren.windows.move_to(1) end },
  { "Mod+h",         function() siren.windows.adj_master_factor(-0.02) end },
  { "Mod+l",         function() siren.windows.adj_master_factor( 0.02) end },
  { "Mod+Shift+r",   function() siren.restart() end },
  { "Mod+r",         function() siren.reload() end },
}
```

Modifier tokens are case-insensitive. `Mod` resolves to `siren.modifier`.

#### `siren.mouse` — mouse drag actions

```lua
siren.mouse = {
  { "mod4+Button1", "move"   },
  { "mod4+Button3", "resize" },
  { "mod4+Button2", "float"  },
}
```

Built-in action strings: `"move"`, `"resize"`, `"float"`.

---

### `rules`

Assigns windows to workspaces and sets their floating state based on
WM_CLASS. Rules are applied once when a window is first mapped.
Windows restored from an exec-restart snapshot skip rules.

```lua
siren.rules = {
  { class = "librewolf",  workspace = 1, isfloating = false },
  { class = "telegram",   workspace = 3 },
  { instance = "steam",   workspace = 6, isfloating = true  },
}
```

| Field       | Type    | Required        | Description                                    |
| ----------- | ------- | --------------- | ---------------------------------------------- |
| `class`     | string  | at least one of | WM_CLASS class component (case-insensitive)    |
| `instance`  | string  | at least one of | WM_CLASS instance component (case-insensitive) |
| `workspace` | integer | no              | Target workspace (1-based)                     |
| `isfloating`| boolean | no              | Force floating state                           |

---

### `bar`

Status bar drawn at the top and/or bottom of each monitor.
Colors default to theme values and can be overridden per-bar.

```lua
siren.bar = {
  top = {
    height = 18,
    font   = "monospace:size=9",  -- optional, falls back to siren.theme.font
    colors = {
      normal_bg  = "#222222",
      normal_fg  = "#bbbbbb",
      focused_bg = "#005577",
      focused_fg = "#eeeeee",
      bar_bg     = "#111111",
      status_fg  = "#bbbbbb",
    },
  },
  -- bottom = { ... },  -- same schema

  widgets = {
    clock = function() return os.date("%H:%M") end,
    -- or with a custom refresh interval (seconds):
    load  = { fn = function() return siren.sys.loadavg() end, interval = 5 },
  },
}
```

**`top` / `bottom` fields:**

| Field    | Type    | Description                                 |
| -------- | ------- | ------------------------------------------- |
| `height` | integer | Bar height in pixels                        |
| `font`   | string  | Pango font, falls back to `siren.theme.font`|
| `colors` | table   | Color overrides (all fall back to theme)    |

**`colors` keys:** `normal_bg`, `normal_fg`, `focused_bg`, `focused_fg`,
`bar_bg`, `status_fg`.

**`widgets`:** key-value table. Values are either a plain function or
`{ fn = function, interval = seconds }`. All widget strings are
concatenated and shown in the status area on the right.

---

### `process`

Process autostart and lifecycle management.

```lua
siren.autostart = {
  { cmd = "picom --experimental-backends", policy = "once"             },
  { cmd = "nm-applet",                     policy = "restart"          },
  { cmd = "syncthing",                     policy = "restart-on-error" },
}
```

| Field    | Type   | Required | Description                        |
| -------- | ------ | -------- | ---------------------------------- |
| `cmd`    | string | yes      | Shell command to execute            |
| `policy` | string | no       | Restart policy (default: `"once"`) |

**Policies:**

| Value              | Behavior                                          |
| ------------------ | ------------------------------------------------- |
| `once`             | Start once; do not restart on exit                |
| `restart`          | Always restart on exit                            |
| `restart-on-error` | Restart only on non-zero exit code                |

Processes with policy `once` survive `siren.restart()` — a second copy
is not spawned if the process is already running.

---

### `wallpaper`

Sets a wallpaper per monitor via `xwallpaper`. Keyed by monitor alias.

```lua
siren.wallpaper = {
  primary = { image = "/path/to/bg.png", mode = "zoom"    },
  right   = { image = "/path/to/bg.png", mode = "stretch" },
}
```

| Field   | Type   | Required | Description                                      |
| ------- | ------ | -------- | ------------------------------------------------ |
| `image` | string | yes      | Path to image file                               |
| `mode`  | string | no       | `stretch` (default), `zoom`, `center`, or `tile` |

Requires `xwallpaper` to be installed.

---

### `sysinfo`

Read-only system metrics exposed as `siren.sys.*`. Intended for use in
bar widgets.

```lua
siren.bar = {
  widgets = {
    status = function()
      return string.format("cpu:%.0f%% mem:%s up:%s",
        siren.sys.cpu(),
        siren.sys.mem(),
        siren.sys.uptime())
    end,
  },
}
```

| Function              | Returns                                    |
| --------------------- | ------------------------------------------ |
| `siren.sys.cpu()`     | CPU usage percent as a number              |
| `siren.sys.mem()`     | Memory usage as a formatted string         |
| `siren.sys.uptime()`  | Uptime as a formatted string               |
| `siren.sys.loadavg()` | Load average as a formatted string         |
| `siren.sys.net_ip()`  | Primary non-loopback IP address string     |
| `siren.sys.disks()`   | Disk usage summary as a formatted string   |
