-- swm.base — root base class for all SirenWM Lua objects.
--
-- Two operations:
--   Base:extend()  — create a subclass
--   Base:new(opts) — create an instance (opts = optional field table)
--
-- Subclass example:
--   local Widget = Base:extend()
--   function Widget:render() return "" end
--
--   local w = Widget:new({ interval = 1 })
--   function w:render() return "hello" end
--
-- settings proxy:
--   obj.settings = {...}  →  calls obj:on_settings_update(...)

local Base = {}
Base.__index = Base

-- Create a subclass of this class.
function Base:extend()
    local cls = {}
    cls.__index = cls
    setmetatable(cls, { __index = self })
    return cls
end

-- Create an instance of this class.  opts is an optional table of initial fields.
function Base:new(opts)
    local inst = setmetatable({}, self)
    if opts then
        for k, v in pairs(opts) do inst[k] = v end
    end
    return inst
end

-- Virtual — called whenever obj.settings = {...} is assigned.
function Base:on_settings_update(s) end

-- Wrap this instance in a proxy that intercepts .settings assignment.
-- obj._impl points back to the unwrapped instance.
function Base:_proxy()
    local impl  = self
    local proxy = setmetatable({}, {
        __newindex = function(_, key, val)
            if key == "settings" then
                impl._settings = val
                impl:on_settings_update(val)
            else
                rawset(impl, key, val)
            end
        end,
        __index = impl,
    })
    proxy._impl = impl
    return proxy
end

return Base
