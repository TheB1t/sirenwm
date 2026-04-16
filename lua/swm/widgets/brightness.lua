-- swm.widgets.brightness — screen brightness widget
-- Uses sysinfo.brightness() (C++ /sys/class/backlight). Shows nothing if no backlight.
local Widget = require("swm.widget")
local sys    = require("sysinfo")

local w = Widget:new({ interval = 5 })

function w:update()
    self.bri = sys.brightness()
end

function w:render()
    local b = self.bri
    if not b or not b.present then return "" end
    return string.format(" [BRI %d%%] ", b.percent)
end

return w
