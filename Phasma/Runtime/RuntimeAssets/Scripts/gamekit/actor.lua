-- gamekit/actor.lua — a light gameplay wrapper over a scene node.
--
-- Holds speed/hp and moves the node in the XZ ground plane (topdown convention:
-- vec2.x -> world X, vec2.y -> world Z). Node positions are LOCAL, but mini-game
-- actors are root nodes, so local == world. Movement is clamped to unit length so
-- diagonals are not faster.

local Actor = {}
Actor.__index = Actor

function Actor.wrap(node, opts)
    opts = opts or {}
    return setmetatable({
        node = node,
        speed = opts.speed or 5.0,
        hp = opts.hp or 100,
        max_hp = opts.hp or 100,
        dead = false,
        _on_death = nil,
    }, Actor)
end

function Actor:position()
    if self.node and self.node.get_position then return self.node:get_position() end
    return vec3(0.0, 0.0, 0.0)
end

-- Move by a 2D ground direction scaled by speed*dt. dir need not be normalized.
function Actor:move_dir(dir, dt)
    if not self.node or self.dead or not self.node.set_position then return end
    local dx = (dir and dir.x) or 0.0
    local dz = (dir and dir.y) or 0.0
    local mag = math.sqrt(dx * dx + dz * dz)
    if mag < 1e-4 then return end
    if mag > 1.0 then dx, dz = dx / mag, dz / mag end
    local step = self.speed * (dt or 0.0)
    local p = self.node:get_position()
    self.node:set_position(vec3(p.x + dx * step, p.y, p.z + dz * step))
end

-- Move toward a world target, stopping within stop_dist. Returns true on arrival.
function Actor:move_to(target, dt, stop_dist)
    if not self.node or self.dead or not self.node.set_position then return false end
    local p = self.node:get_position()
    local dx = (target.x or 0.0) - p.x
    local dz = (target.z or 0.0) - p.z
    local dist = math.sqrt(dx * dx + dz * dz)
    stop_dist = stop_dist or 0.1
    if dist <= stop_dist then return true end
    local step = math.min(self.speed * (dt or 0.0), dist)
    self.node:set_position(vec3(p.x + dx / dist * step, p.y, p.z + dz / dist * step))
    return false
end

function Actor:damage(n)
    if self.dead then return end
    self.hp = self.hp - (n or 0)
    if self.hp <= 0 then
        self.hp = 0
        self.dead = true
        if self._on_death then self._on_death(self) end
    end
end

function Actor:heal(n)
    if self.dead then return end
    self.hp = math.min(self.max_hp, self.hp + (n or 0))
end

function Actor:on_death(fn)
    self._on_death = fn
    return self
end

function Actor:hp_fraction()
    if self.max_hp <= 0 then return 0.0 end
    return self.hp / self.max_hp
end

return Actor
