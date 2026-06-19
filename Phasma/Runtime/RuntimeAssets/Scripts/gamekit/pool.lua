-- gamekit/pool.lua — recycle a set of pre-authored scene nodes.
--
-- The performance rule for PhasmaEngine mini-games (see Warbound/ATH memory) is:
-- author runtime objects in the .pescene up front, disabled and parked offscreen,
-- then enable/disable instead of creating nodes per frame. A pool wraps that:
-- members are named "<prefix>_001".."<prefix>_NNN" and start disabled. acquire()
-- enables the next free one; release() disables it and parks it back at Y=-1000
-- (the same convention Warbound's skirmish pools use), so TLAS/transform churn is
-- bounded by the authored pool size.

local PARK_Y = -1000.0

local Pool = {}
Pool.__index = Pool

-- Build a pool from authored members named "<prefix>_NNN" (1-based, zero-padded).
-- max bounds the scan (default 256). All discovered members start free.
function Pool.new(prefix, max)
    local self = setmetatable({ prefix = prefix, free = {}, active = {} }, Pool)
    max = max or 256
    if scene and scene.find_model then
        for i = 1, max do
            local node = scene.find_model(string.format("%s_%03d", prefix, i))
            if node ~= nil then
                self.free[#self.free + 1] = node
            end
        end
    end
    self.capacity = #self.free
    return self
end

-- Enable and hand out the next free member, or nil if the pool is exhausted.
-- The caller positions it (acquire does not move it off the park spot for you).
function Pool:acquire()
    local node = table.remove(self.free)
    if not node then return nil end
    self.active[#self.active + 1] = node
    if node.set_enabled then node:set_enabled(true) end
    return node
end

-- Disable a member, park it offscreen, and return it to the free list.
function Pool:release(node)
    if not node then return end
    for i = #self.active, 1, -1 do
        if self.active[i] == node then
            table.remove(self.active, i)
            if node.set_enabled then node:set_enabled(false) end
            if node.set_position then node:set_position(vec3(0.0, PARK_Y, 0.0)) end
            self.free[#self.free + 1] = node
            return
        end
    end
end

-- Call fn(node) for each active member. Iterates a snapshot, so fn may release().
function Pool:each_active(fn)
    local snapshot = {}
    for i = 1, #self.active do snapshot[i] = self.active[i] end
    for _, node in ipairs(snapshot) do fn(node) end
end

function Pool:active_count() return #self.active end
function Pool:free_count() return #self.free end

-- Release every active member.
function Pool:release_all()
    self:each_active(function(node) self:release(node) end)
end

return Pool
