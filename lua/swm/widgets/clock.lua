-- swm.widgets.clock — date/time widget
local Widget = require("swm.widget")

local w = Widget:new({ interval = 1 })

function w:render()
    return os.date(" [%d-%m-%Y %H:%M:%S %Z] ")
end

return w
