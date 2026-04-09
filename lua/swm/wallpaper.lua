-- swm.wallpaper — set per-monitor wallpapers via xwallpaper
--
-- Usage:
--   local wallpaper = require("wallpaper")
--   wallpaper.settings = {
--       primary = { image = "/path/to/bg.png", mode = "zoom" },
--       left    = { image = "/path/to/bg2.png" },
--   }
--
-- mode: "stretch" (default), "zoom", "center", "tile"

local Module = require("swm.module")

local M    = Module:new()
local proc = nil
local modes = { stretch = true, zoom = true, center = true, tile = true }

local function apply(entries)
    if proc and proc:alive() then proc:kill() end
    local mons = siren.monitor.list()
    local args = {}
    for _, m in ipairs(mons) do
        local entry = entries[m.name]
        if entry and entry.image then
            local mode = (entry.mode and modes[entry.mode]) and entry.mode or "stretch"
            args[#args + 1] = "--output " .. m.output .. " --" .. mode .. " " .. entry.image
        end
    end
    if #args > 0 then
        proc = siren.spawn("xwallpaper " .. table.concat(args, " "))
    end
end

siren.on("wm.started", function()
    if M._settings then apply(M._settings) end
end)

siren.on("display.changed", function()
    if M._settings then apply(M._settings) end
end)

function M:on_settings_update(s)
    -- Only apply during reload if already started; on first start the
    -- siren.on("wm.started") handler above fires instead.
end

-- Manual re-application hook wired to reload.
siren.on("wm.reloaded", function()
    if M._settings then apply(M._settings) end
end)

return M:_proxy()
