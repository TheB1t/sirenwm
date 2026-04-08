# SirenWM Configuration Reference

Config file: `~/.config/sirenwm/init.lua`

The config is plain Lua — variables, functions, and loops all work.

---

## Loading modules

C++ modules are loaded with `require()` and return a module object.
Lua modules from `~/.config/sirenwm/swm/` (or the system library) use the same `require()`.

**Required** (WM is unusable without these):

```lua
local kb  = require("keybindings")
local bar = require("bar")
```

**Optional** — use `siren.load()` instead of `require()` for modules that may be absent.
`siren.load()` returns a null-object that silently absorbs all field accesses and calls,
so the config keeps working even if a module is missing:

```lua
local kbd       = siren.load("keyboard")
local dbg       = siren.load("debug_ui")
local rules     = siren.load("rules")
local wallpaper = siren.load("wallpaper")
local autostart = siren.load("autostart")

-- Widgets are also optional:
local tags    = siren.load("widgets.tags")
local title   = siren.load("widgets.title")
local clock   = siren.load("widgets.clock")
```

Module short names resolve from `~/.config/sirenwm/` first, then the system
library, then `package.path`.

---

## Core settings (`siren.*`)

Core settings are assigned directly to the global `siren` table.
All must appear before the modules that depend on them.

### `siren.modifier`

Primary modifier key. Resolves to `mod` in bind specs and mouse drags.

```lua
siren.modifier = "mod4"   -- super/win key
```

Accepted values: `shift`, `ctrl`/`control`, `alt`/`mod1`, `mod2`–`mod5`,
`super`, `win`.

---

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
| `alt_fg`           | string  | Alternate foreground (active items)      |
| `accent`           | string  | Accent color (focused items, highlights) |
| `gap`              | integer | Gap between tiled windows in pixels      |
| `border.thickness` | integer | Window border width in pixels            |
| `border.focused`   | string  | Border color for the focused window      |
| `border.unfocused` | string  | Border color for all other windows       |

---

### `siren.workspaces`

Global workspace pool. IDs are 1-based in the Lua API.

```lua
siren.workspaces = {
    { name = "[1]",         monitor = "primary" },
    { name = "[2] Browser", monitor = "primary" },
    { name = "[3]",         monitor = "right"   },
}
```

| Field     | Type   | Required | Description                                |
| --------- | ------ | -------- | ------------------------------------------ |
| `name`    | string | yes      | Display name shown in the bar              |
| `monitor` | string | no       | Preferred monitor alias for this workspace |

---

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

---

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

| Field         | Type    | Required | Description                                |
| ------------- | ------- | -------- | ------------------------------------------ |
| `primary`     | string  | yes      | Alias of the primary monitor               |
| `layout`      | array   | yes      | Placement entries for non-primary monitors |
| `monitor`     | string  | yes      | Monitor alias                              |
| `relative_to` | string  | yes      | Anchor monitor alias                       |
| `side`        | string  | yes      | `left`, `right`, `top`, or `bottom`        |
| `shift`       | integer | no       | Pixel offset along the perpendicular axis  |

---

## Runtime API

### Lifecycle

```lua
siren.spawn("picom")   -- fork and exec; returns a ProcessHandle
siren.reload()         -- hot-reload init.lua in-process
siren.restart()        -- replace the WM process via exec (preserves windows)
```

`siren.spawn()` returns a process handle:

```lua
local p = siren.spawn("picom")
p:pid()       -- returns pid (integer)
p:alive()     -- returns true if process is still running
p:kill()      -- send SIGTERM; returns true on success
p:kill(9)     -- send signal by number
```

### Event bus

```lua
siren.on(event, fn)
```

Registers `fn` as a callback for the named event. Multiple callbacks per
event are supported. All handlers are cleared on hot-reload and re-registered
by re-running `init.lua`.

| Event               | Callback args                                          | When fired                                      |
| ------------------- | ------------------------------------------------------ | ----------------------------------------------- |
| `"start"`           | —                                                      | After runtime start; monitors and windows ready |
| `"reload"`          | —                                                      | At the start of a hot-reload cycle              |
| `"stop"`            | `{ exec_restart }`                                     | Before shutdown or exec-restart                 |
| `"child_exit"`      | `{ pid, exit_code }`                                   | A spawned child process exited                  |
| `"display_change"`  | —                                                      | Monitor topology changed (hotplug)              |
| `"window_rules"`    | `{id, class, instance, workspace, type, from_restart}` | Before rules applied to a new window            |
| `"window_map"`      | `{id, class, instance, workspace}`                     | Window mapped (shown)                           |
| `"window_unmap"`    | `{id, withdrawn}`                                      | Window unmapped (hidden or closed)              |
| `"focus_change"`    | `{id}`                                                 | Focus changed (`id=0` means focus cleared)      |
| `"workspace_switch"`| `{workspace}`                                          | Active workspace changed (1-indexed)            |

`window_rules` fields:

- `type` ∈ `"normal"`, `"dialog"`, `"utility"`, `"splash"`, `"modal"`
- `from_restart` — `true` when window was restored from a restart snapshot;
  skip rules that would overwrite saved state

### Window operations — `siren.win`

```lua
siren.win.close()
siren.win.focus_next()
siren.win.focus_prev()
siren.win.toggle_floating()

-- by window id (used inside siren.on("window_rules") callbacks):
siren.win.set_floating(id, bool)

-- move focused window to workspace n (1-based):
siren.win.move_to(n)
-- move window by id to workspace n (1-based):
siren.win.move_to(id, n)

-- move focused window to monitor n (1-based):
siren.win.move_to_monitor(n)
-- move window by id to monitor n (1-based):
siren.win.move_to_monitor(id, n)
```

### Layout operations — `siren.layout`

Built-in layouts: `"tile"`, `"monocle"`, `"unmanaged"`.

```lua
siren.layout.set("tile")        -- switch active layout by name
siren.layout.zoom()             -- swap focused window with master
siren.layout.adj_master(d)      -- adjust master factor, e.g. -0.05
siren.layout.inc_master(d)      -- change number of master windows, e.g. 1
```

#### Custom layouts

```lua
siren.layout.register("grid", function(ctx)
    -- ctx: windows (array of ids), monitor {pos, size}, gap, border,
    --      master_factor, nmaster
    local cols = math.ceil(math.sqrt(#ctx.windows))
    local rows = math.ceil(#ctx.windows / cols)
    local cw   = math.floor(ctx.monitor.size.x / cols)
    local ch   = math.floor(ctx.monitor.size.y / rows)
    for i, id in ipairs(ctx.windows) do
        local col = (i - 1) % cols
        local row = math.floor((i - 1) / cols)
        siren.layout.place(id,
            ctx.monitor.pos + Vec2(col * cw + ctx.gap, row * ch + ctx.gap),
            Vec2(cw - ctx.gap*2 - ctx.border*2, ch - ctx.gap*2 - ctx.border*2),
            ctx.border)
    end
end)
```

`siren.layout.place(id, pos, size [, border])` — positions a window.
`pos` and `size` are Vec2 tables. Valid only inside a layout callback.

#### Vec2

`Vec2(x, y)` — constructor available globally before `init.lua` loads.

```lua
local a = Vec2(10, 20)
print(a + Vec2(5, 5))  -- Vec2(15, 25)
print(a.x, a.y)        -- 10  20
```

### Workspace operations — `siren.ws`

```lua
siren.ws.switch(n)   -- switch to workspace n (1-based)
```

### Monitor queries — `siren.monitor`

```lua
siren.monitor.focused()  -- returns monitor table or nil
siren.monitor.list()     -- returns array of monitor tables
siren.monitor.focus(n)   -- focus monitor n (1-based)
```

Monitor table fields:

- `pos` — Vec2 with monitor position
- `size` — Vec2 with monitor dimensions
- `name` — logical alias (e.g. `"primary"`); falls back to `output` if no alias configured
- `output` — RandR output name (e.g. `"eDP-1"`); always set

---

## Modules

All C++ and Lua modules follow the same convention:
assign to `module.settings` to configure; the module applies the settings
immediately and re-applies them automatically on `siren.reload()`.

---

### `keybindings`

Key and mouse binding engine.

```lua
local kb = require("keybindings")
```

#### `kb.binds`

```lua
kb.binds = {
    { "mod+Return",       function() siren.spawn("alacritty") end },
    { "mod+shift+q",      function() siren.win.close() end },
    { "mod+j",            function() siren.win.focus_next() end },
    { "mod+shift+space",  function() siren.win.toggle_floating() end },
    { "mod+shift+Return", function() siren.layout.zoom() end },
    { "mod+1",            function() siren.ws.switch(1) end },
    { "mod+shift+1",      function() siren.win.move_to(1) end },
    { "mod+ctrl+1",       function() siren.monitor.focus(1) end },
}
```

`mod` resolves to `siren.modifier`. Tokens are case-insensitive.

#### `kb.mouse`

```lua
kb.mouse = {
    { "mod+Button1", "move"   },
    { "mod+Button3", "resize" },
    { "mod+Button2", "float"  },
}
```

Built-in action strings: `"move"`, `"resize"`, `"float"`.

---

### `keyboard`

Applies XKB keyboard layout and options at startup and on reload.
Restores the original layout on exit.

```lua
local kbd = require("keyboard")

kbd.settings = {
    layouts = "us,ru",
    options = "grp:alt_shift_toggle,terminate:ctrl_alt_bksp",
}
```

| Field     | Type   | Required | Description                      |
| --------- | ------ | -------- | -------------------------------- |
| `layouts` | string | yes      | Comma-separated XKB layout names |
| `options` | string | no       | Comma-separated XKB options      |

---

### `bar`

Status bar at the top and/or bottom of each monitor.

```lua
local bar = require("bar")

local tags    = require("widgets.tags")    -- built-in
local title   = require("widgets.title")   -- built-in
local tray    = require("widgets.tray")    -- built-in
local clock   = require("widgets.clock")   -- Lua widget
local sysinfo = require("widgets.sysinfo") -- Lua widget

bar.settings = {
    top = {
        height = 18,
        left   = { tags },
        center = { title },
        right  = { tray },
    },
    bottom = {
        height = 18,
        left   = { clock },
        right  = { sysinfo },
    },
}
```

**`top` / `bottom` fields:**

| Field    | Type    | Description                                  |
| -------- | ------- | -------------------------------------------- |
| `height` | integer | Bar height in pixels                         |
| `font`   | string  | Pango font, falls back to `siren.theme.font` |
| `left`   | array   | Widget objects for the left zone             |
| `center` | array   | Widget objects for the center zone           |
| `right`  | array   | Widget objects for the right zone            |
| `colors` | table   | Color overrides (see below)                  |

**`colors` keys:** `normal_bg`, `normal_fg`, `focused_bg`, `focused_fg`,
`bar_bg`, `status_fg`. All fall back to `siren.theme`.

#### Widgets

Widgets are objects with a `render()` method. Built-in widgets are created
via `Widget.builtin(name)`.

**Writing a custom widget:**

```lua
local Widget = require("swm.widget")

local mywidget = Widget:new({ interval = 5 })
function mywidget:render()
    return string.format(" [load: %.2f] ", ...) 
end
```

| Field      | Type    | Description                                       |
| ---------- | ------- | ------------------------------------------------- |
| `interval` | integer | Refresh interval in seconds; `0` = every redraw   |
| `render()` | method  | Returns the text string to display in the bar     |

**Built-in widget names:** `"tags"`, `"title"`, `"tray"`.

---

### `sysinfo`

Read-only system metrics.

```lua
local sys = require("sysinfo")
```

| Function          | Returns                                                                         |
| ----------------- | ------------------------------------------------------------------------------- |
| `sys.cpu()`       | CPU usage percent as a number                                                   |
| `sys.mem()`       | Table: `{ used, total, percent }` (GB / percent)                                |
| `sys.uptime()`    | Uptime in seconds as a number                                                   |
| `sys.loadavg()`   | Table: `{ ["1"], ["5"], ["15"] }` (load averages)                               |
| `sys.net_ip()`    | Primary non-loopback IP address string                                          |
| `sys.disks()`     | Array of `{ device, mountpoint, total, used, free, percent }` (bytes / percent) |
| `sys.kbd_layout()`| Current keyboard layout name string (e.g. `"us"`)                               |

---

### `debug_ui`

ImGui debug overlay. Requires build option `-DSIRENWM_DEBUG_UI=ON`.

```lua
local dbg = require("debug_ui")

-- toggle from a keybind:
{ "mod+F12", function() dbg.toggle() end }
```

| Method        | Description              |
| ------------- | ------------------------ |
| `dbg.toggle()`| Show / hide the overlay  |
| `dbg.show()`  | Show the overlay         |
| `dbg.hide()`  | Hide the overlay         |

---

### `rules`

Declarative window rules.

```lua
local rules = require("rules")

rules.settings = {
    { class = "steam",   float = true },
    { class = "firefox", workspace = 2 },
    { instance = "Navigator", workspace = 1 },
}
```

| Field       | Type    | Required        | Description                                    |
| ----------- | ------- | --------------- | ---------------------------------------------- |
| `class`     | string  | at least one of | WM_CLASS class component (case-insensitive)    |
| `instance`  | string  | at least one of | WM_CLASS instance component (case-insensitive) |
| `workspace` | integer | no              | Target workspace (1-based)                     |
| `float`     | boolean | no              | Force floating state                           |

Dialogs and modals are auto-floated regardless of rules.
Rules are skipped for windows restored from a restart snapshot.

---

### `wallpaper`

Per-monitor wallpapers via `xwallpaper`.

```lua
local wallpaper = require("wallpaper")

wallpaper.settings = {
    primary = { image = "/path/to/bg.png", mode = "stretch" },
    right   = { image = "/path/to/bg.jpg", mode = "zoom"    },
}
```

Entries are keyed by monitor alias (`siren.monitor.list()[n].name`).

| Field   | Type   | Required | Description                                      |
| ------- | ------ | -------- | ------------------------------------------------ |
| `image` | string | yes      | Path to image file                               |
| `mode`  | string | no       | `stretch` (default), `zoom`, `center`, or `tile` |

---

### `autostart`

Process autostart and lifecycle management.

```lua
local autostart = require("autostart")

autostart.settings = {
    { cmd = "picom --experimental-backends", policy = "once"    },
    { cmd = "nm-applet",                     policy = "restart" },
}
```

| Field    | Type   | Required | Description                        |
| -------- | ------ | -------- | ---------------------------------- |
| `cmd`    | string | yes      | Shell command to execute           |
| `policy` | string | no       | Restart policy (default: `"once"`) |

**Policies:**

| Value              | Behavior                                   |
| ------------------ | ------------------------------------------ |
| `once`             | Start once; do not restart on exit         |
| `restart`          | Always restart on exit                     |
| `restart-on-error` | Restart only on non-zero exit code         |

Processes with policy `once` survive `siren.restart()`.

---

## Writing Lua modules

### `swm.base`

Root base class. All SirenWM Lua objects inherit from it.

```lua
local Base = require("swm.base")

local MyObj = Base:new()
function MyObj:on_settings_update(s) ... end
return MyObj:_proxy()   -- exposes .settings assignment
```

### `swm.module`

Base class for configuration modules. Automatically re-calls
`on_settings_update` on `siren.reload()`.

```lua
local Module = require("swm.module")

local M = Module:new()
function M:on_settings_update(s)
    -- apply s
end
return M:_proxy()
```

### `swm.widget`

Base class for bar widgets.

```lua
local Widget = require("swm.widget")

local w = Widget:new({ interval = 2 })
function w:render()
    return " text "
end
return w
```
