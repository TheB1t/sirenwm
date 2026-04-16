-- swm.widgets.kbd — keyboard layout indicator (reactive: refreshed on every repaint)
local Widget = require("swm.widget")
local sys    = require("sysinfo")

local w = Widget:new()

function w:update()
    self.layout = sys.kbd_layout() or "??"
end

function w:render()
    return string.format(" [%s] ", string.upper(self.layout or "??"))
end

return w
