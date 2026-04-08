-- swm.widgets.netdisk — network IP and disk usage widget
local Widget = require("swm.widget")
local sys    = require("sysinfo")

local w = Widget:new({ interval = 10 })

function w:render()
    local ip    = sys.net_ip()
    local disks = sys.disks()
    local parts = {}
    local skip  = { ["/boot/efi"] = true }
    for _, d in ipairs(disks) do
        if not skip[d.mountpoint] then
            table.insert(parts, string.format("%s %.0f%%", d.mountpoint, d.percent))
        end
    end
    return string.format(" [IP %s][%s] ", ip, table.concat(parts, "]["))
end

return w
