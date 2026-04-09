-- swm.autostart — managed process spawning for SirenWM
--
-- Usage:
--   local autostart = require("autostart")
--   autostart.settings = {
--       { cmd = "picom --experimental-backends", policy = "once" },
--       { cmd = "dunst",                         policy = "restart" },
--   }
--
-- Policies:
--   "once"             — start once; survives exec-restart (default)
--   "restart"          — restart unconditionally on any exit
--   "restart-on-error" — restart only if exit code ~= 0

local Module = require("swm.module")

local M       = Module:new()
local entries = {}
local started = false

local function basename(cmd)
    local tok = cmd:match("^(%S+)")
    return tok and tok:match("([^/]+)$")
end

local function is_already_running(cmd)
    local bin = basename(cmd)
    if not bin then return false end
    local f = io.popen("pgrep -x '" .. bin .. "' >/dev/null 2>&1 && echo y || echo n")
    if not f then return false end
    local r = f:read("*l")
    f:close()
    return r == "y"
end

local function spawn_entry(e)
    local h = siren.spawn(e.cmd)
    if h then
        e.pid     = h:pid()
        e.handle  = h
        e.running = true
    end
end

local function kill_entry(e)
    if e.handle and e.handle:alive() then e.handle:kill() end
    e.running = false
    e.pid     = -1
    e.handle  = nil
end

local function parse(raw_list)
    local out = {}
    for _, raw in ipairs(raw_list) do
        out[#out + 1] = {
            cmd     = raw.cmd,
            policy  = raw.policy or "once",
            pid     = -1,
            running = false,
        }
    end
    return out
end

siren.on("wm.started", function()
    started = true
    local list = M._settings and parse(M._settings) or {}
    entries = list
    for _, e in ipairs(entries) do
        if not (e.policy == "once" and is_already_running(e.cmd)) then
            spawn_entry(e)
        end
    end
end)

siren.on("process.exited", function(ev)
    for _, e in ipairs(entries) do
        if e.pid == ev.pid then
            e.running = false
            e.pid     = -1
            e.handle  = nil
            if e.policy == "restart" then
                spawn_entry(e)
            elseif e.policy == "restart-on-error" and ev.exit_code ~= 0 then
                spawn_entry(e)
            end
            return
        end
    end
end)

siren.on("wm.stopping", function(ev)
    for _, e in ipairs(entries) do
        if not (ev.exec_restart and e.policy == "once") then
            kill_entry(e)
        end
    end
    entries = {}
end)

function M:on_settings_update(s)
    if not started then return end
    -- Reload: reconcile running entries with new settings.
    local new_list  = parse(s)
    local new_by_cmd = {}
    for _, n in ipairs(new_list) do new_by_cmd[n.cmd] = n end

    local kept = {}
    for _, e in ipairs(entries) do
        if new_by_cmd[e.cmd] then
            e.policy     = new_by_cmd[e.cmd].policy
            kept[#kept + 1] = e
        else
            kill_entry(e)
        end
    end

    local old_by_cmd = {}
    for _, e in ipairs(kept) do old_by_cmd[e.cmd] = true end

    for _, n in ipairs(new_list) do
        if not old_by_cmd[n.cmd] then
            spawn_entry(n)
            kept[#kept + 1] = n
        end
    end

    entries = kept
end

return M:_proxy()
