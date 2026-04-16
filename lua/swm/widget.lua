-- swm.widget — base class for bar widgets.
--
-- Lua widget:
--   local w = Widget:new({ interval = 5 })
--   function w:update() self.cpu = sys.cpu() end  -- mutates state by `interval` seconds
--   function w:render() return string.format("%.0f%%", self.cpu or 0) end  -- cheap, pure
--
-- update() is the expensive path (IO, syscalls). render() runs on every bar
-- repaint and must stay cheap. Widgets without an update() fall back to
-- calling render() on the interval schedule (legacy compatibility).
--
-- Built-in (C++) widget:
--   local tags = Widget.builtin("tags")

local Base   = require("swm.base")
local Widget = Base:extend()

Widget.interval = 0  -- 0 = reactive (update+render on every repaint)

-- Virtual — override to refresh widget state from expensive sources.
function Widget:update() end

-- Virtual — override to produce the text drawn on the bar.
function Widget:render() return "" end

-- Factory for built-in (C++) widgets.
-- Returns a plain table recognised by the C++ bar parser via __builtin.
function Widget.builtin(name)
    return { __builtin = name }
end

return Widget
