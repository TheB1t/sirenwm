-- swm.module — base class for configuration modules (autostart, rules, wallpaper…).
--
-- Automatically wires siren.on("wm.reloaded") to re-call on_settings_update
-- with the last assigned settings.
--
-- Usage:
--   local Module = require("swm.module")
--   local M = Module:new()
--   function M:on_settings_update(s) ... end
--   return M:_proxy()

local Base   = require("swm.base")
local Module = Base:extend()

function Module:new(opts)
    local inst = Base.new(self, opts)

    siren.on("wm.reloaded", function()
        if inst._settings then
            inst:on_settings_update(inst._settings)
        end
    end)

    return inst
end

return Module
