-- gamekit/camera.lua — a top-down / angled follow camera.
--
-- Wraps the active camera object. topdown(target_node, opts) switches it to an
-- orthographic view that follows the target's ground position from above, pitched
-- by `angle` degrees. The controller exposes :update(dt) — add it to the loop's
-- "post" phase so it reacts to the frame's final positions. All camera methods are
-- nil-guarded, so a backend missing one (e.g. set_orthographic) degrades quietly.

local Camera = {}

local Controller = {}
Controller.__index = Controller

-- opts: height (units above target), angle (deg down from horizontal; 90 = straight
-- down), lerp (follow stiffness; higher = snappier), size (orthographic half-height;
-- nil = keep authored).
function Camera.topdown(target, opts)
    opts = opts or {}
    local cam = get_camera and get_camera() or nil
    local self = setmetatable({
        cam = cam,
        target = target,
        height = opts.height or 18.0,
        angle = opts.angle or 60.0,
        lerp = opts.lerp or 8.0,
        size = opts.size,
        _pos = nil,
    }, Controller)
    if cam then
        if cam.set_orthographic then cam:set_orthographic(true) end
        if self.size and cam.set_orthographic_size then cam:set_orthographic_size(self.size) end
    end
    self:update(0.0, true) -- snap to the target immediately
    return self
end

function Controller:_target_pos()
    local t = self.target
    if t and t.get_position then return t:get_position() end
    if t and t.position then return t:position() end -- accept an actor too
    return vec3(0.0, 0.0, 0.0)
end

function Controller:update(dt, snap)
    if not self.cam then return end
    local t = self:_target_pos()
    local rad = math.rad(self.angle)
    -- horizontal pull-back so the camera looks down at `angle` from `height` up
    local back = math.cos(rad) * self.height / math.max(0.001, math.sin(rad))
    local desired = vec3(t.x, t.y + self.height, t.z + back)
    if snap or not self._pos then
        self._pos = desired
    else
        local a = 1.0 - math.exp(-self.lerp * dt)
        self._pos = vec3(
            self._pos.x + (desired.x - self._pos.x) * a,
            self._pos.y + (desired.y - self._pos.y) * a,
            self._pos.z + (desired.z - self._pos.z) * a)
    end
    if self.cam.set_position then self.cam:set_position(self._pos) end
    if self.cam.look_at then self.cam:look_at(vec3(t.x, t.y, t.z)) end
end

return Camera
