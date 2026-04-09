-- swm.rules — declarative window rules for SirenWM
--
-- Usage:
--   local rules = require("rules")
--   rules.settings = {
--       { class = "steam",   float = true },
--       { class = "firefox", workspace = 2 },
--   }

local Module = require("swm.module")

local M = Module:new()

local function match(rule, win)
    if rule.class then
        if not win.class or win.class:lower() ~= rule.class:lower() then
            return false
        end
    end
    if rule.instance then
        if not win.instance or win.instance:lower() ~= rule.instance:lower() then
            return false
        end
    end
    return rule.class ~= nil or rule.instance ~= nil
end

local function apply(rules_list, win)
    if win.from_restart then return end

    if win.type == "dialog" or win.type == "modal" then
        siren.win.set_floating(win.id, true)
    end

    for _, rule in ipairs(rules_list) do
        if match(rule, win) then
            if rule.float then
                siren.win.set_floating(win.id, true)
            end
            if rule.workspace then
                siren.win.move_to(win.id, rule.workspace)
            end
            return
        end
    end
end

siren.on("window.rules", function(win)
    if M._settings then apply(M._settings, win) end
end)

function M:on_settings_update(s)
    -- settings stored in _settings by base proxy; nothing else needed here.
end

return M:_proxy()
