-- swm.widgets.kbd — keyboard layout indicator
local Widget = require("swm.widget")
local sys    = require("sysinfo")

local w = Widget:new()

function w:render()
    return string.format(" [%s] ", string.upper(sys.kbd_layout() or "??"))
end

return w
