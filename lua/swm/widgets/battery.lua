-- swm.widgets.battery — battery charge and status widget
-- Uses sysinfo.battery() (C++ /sys/class/power_supply). Shows nothing if no battery.
local Widget = require("swm.widget")
local sys    = require("sysinfo")

local w = Widget:new({ interval = 30 })

function w:update()
    self.bat = sys.battery()
end

function w:render()
    local b = self.bat
    if not b or not b.present then return "" end

    local icon
    if b.status == "Charging" or b.status == "Full" then
        icon = "CHR"
    elseif b.status == "Not charging" then
        icon = "FULL"
    else
        icon = "BAT"
    end

    return string.format(" [%s %d%%] ", icon, b.capacity)
end

return w
