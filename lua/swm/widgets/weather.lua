-- swm.widgets.weather — weather widget via wttr.in
-- Configure: require("swm.widgets.weather").city = "Moscow"
local Widget = require("swm.widget")

local w = Widget:new({ interval = 900 })  -- 15 minutes

w.city   = ""      -- empty = auto-detect by IP
w.format = "%c%t"  -- wttr.in format string: %c=icon %t=temperature

local function fetch(self)
    local loc = self.city ~= "" and self.city or ""
    local fmt = (self.format:gsub(" ", "+"))
    local url = string.format("wttr.in/%s?format=%s", loc, fmt)
    local f = io.popen('curl -sf --max-time 5 "' .. url .. '" 2>/dev/null', "r")
    if not f then return nil end
    local out = f:read("*a")
    f:close()
    if not out or #out == 0 or out:find("Unknown location") then return nil end
    return out:gsub("%s+$", "")
end

function w:update()
    local result = fetch(self)
    if result then
        self.cached = result
    end
end

function w:render()
    if not self.cached or #self.cached == 0 then return "" end
    return " [" .. self.cached .. "] "
end

return w
