-- swm.widgets.clock — date/time widget
local Widget = require("swm.widget")

local w = Widget:new({ interval = 1 })

function w:update()
    self.text = os.date(" [%d-%m-%Y %H:%M:%S %Z] ")
end

function w:render()
    return self.text or ""
end

return w
