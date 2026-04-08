-- swm.widget — base class for bar widgets.
--
-- Lua widget:
--   local w = Widget:new({ interval = 1 })
--   function w:render() return "text" end
--
-- Built-in (C++) widget:
--   local tags = Widget.builtin("tags")

local Base   = require("swm.base")
local Widget = Base:extend()

Widget.interval = 0  -- 0 = every redraw

-- Virtual — override to produce status text.
function Widget:render() return "" end

-- Factory for built-in (C++) widgets.
-- Returns a plain table recognised by the C++ bar parser via __builtin.
function Widget.builtin(name)
    return { __builtin = name }
end

return Widget
