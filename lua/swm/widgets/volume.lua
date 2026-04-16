-- swm.widgets.volume — audio volume widget (output + input)
-- Uses the C++ audio module (ALSA). Shows nothing if the module is unavailable.
local Widget = require("swm.widget")

local ok, audio = pcall(require, "audio")
if not ok then audio = nil end

local w = Widget:new({ interval = 2 })

local function format_vol(label, info)
    if not info or not info.available then return nil end
    if info.muted then
        return label .. " MUTE"
    end
    return string.format("%s %d%%", label, info.percent)
end

function w:update()
    if not audio then return end
    self.out = audio.output()
    self.in_ = audio.input()
end

function w:render()
    if not audio then return "" end

    local parts = {}

    local s = format_vol("OUT", self.out)
    if s then parts[#parts + 1] = s end

    s = format_vol("IN", self.in_)
    if s then parts[#parts + 1] = s end

    if #parts == 0 then return "" end
    return " [" .. table.concat(parts, "][") .. "] "
end

return w
