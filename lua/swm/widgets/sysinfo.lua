-- swm.widgets.sysinfo — CPU and memory usage widget
local Widget = require("swm.widget")
local sys    = require("sysinfo")

local w = Widget:new({ interval = 2 })

function w:update()
    self.cpu = sys.cpu()
    self.mem = sys.mem()
end

function w:render()
    if not self.cpu or not self.mem then return "" end
    return string.format(" [CPU %.2f%%][MEM %.2f/%.2f GB (%.2f%%)] ",
        self.cpu, self.mem.used, self.mem.total, self.mem.percent)
end

return w
