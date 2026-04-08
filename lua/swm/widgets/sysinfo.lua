-- swm.widgets.sysinfo — CPU and memory usage widget
local Widget = require("swm.widget")
local sys    = require("sysinfo")

local w = Widget:new({ interval = 2 })

function w:render()
    local cpu = sys.cpu()
    local mem = sys.mem()
    return string.format(" [CPU %.2f%%][MEM %.2f/%.2f GB (%.2f%%)] ",
        cpu, mem.used, mem.total, mem.percent)
end

return w
