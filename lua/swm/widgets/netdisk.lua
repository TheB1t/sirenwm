-- swm.widgets.netdisk — network IP and disk usage widget
local Widget = require("swm.widget")
local sys    = require("sysinfo")

local w = Widget:new({ interval = 10 })

function w:update()
    self.ip    = sys.net_ip()
    self.disks = sys.disks()
end

function w:render()
    if not self.disks then return "" end
    local parts = {}
    local skip  = { ["/boot/efi"] = true }
    for _, d in ipairs(self.disks) do
        if not skip[d.mountpoint] then
            table.insert(parts, string.format("%s %.0f%%", d.mountpoint, d.percent))
        end
    end
    return string.format(" [IP %s][%s] ", self.ip or "?", table.concat(parts, "]["))
end

return w
