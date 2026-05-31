-- touch_camera.lua — phone touch camera controls for the Android player.
-- Auto-loads (global script, ScriptLifecycle::Always); update() runs in play mode.
--   * one finger drag  -> look around (yaw/pitch)
--   * two-finger pinch -> dolly forward/back (spread = forward)
-- Touch deltas from input.get_touch() are normalized to the surface (a full-screen swipe = 1.0),
-- so behavior is resolution-independent. Tune the two sensitivities below to taste.

local LOOK_RADIANS_PER_SWIPE = math.pi -- a full-screen drag turns ~180 degrees
local DOLLY_UNITS_PER_PINCH = 40.0     -- world units moved per full-diagonal pinch

local function update(dt)
    local cam = get_camera()
    if not cam then
        return
    end

    local t = input.get_touch()
    if not t then
        return
    end

    if t.fingers >= 2 then
        -- Pinch to move along the view direction. cam:move("forward", s) adds front * s, so a
        -- negative s (fingers pinching together) walks backward.
        if t.pinch ~= 0.0 then
            cam:move("forward", t.pinch * DOLLY_UNITS_PER_PINCH)
        end
    elseif t.fingers == 1 and (t.dx ~= 0.0 or t.dy ~= 0.0) then
        -- cam:rotate() multiplies by the camera's rotation_speed internally; divide it out so the
        -- on-screen turn rate is deterministic regardless of what the scene set rotation_speed to.
        local rs = cam:get_rotation_speed()
        if rs == nil or rs <= 0.0 then
            rs = 1.0
        end
        local scale = LOOK_RADIANS_PER_SWIPE / rs
        cam:rotate(t.dx * scale, t.dy * scale)
    end
end

hooks {
    update = update,
}
