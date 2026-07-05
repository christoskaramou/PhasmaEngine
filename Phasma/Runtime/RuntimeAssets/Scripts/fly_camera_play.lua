-- fly_camera_play.lua
-- Attachable camera controller. Drives the scene's ACTIVE camera via get_camera(), so the node it
-- lives on only needs spatial meaning (it is auto-attached to new cameras).
--
-- TWO MODES, chosen automatically every frame:
--   * FREE-FLY (kinematic): moves the camera directly. Used when there is no physics body, OR in the
--     editor (edit mode) even with a body -- physics only simulates during play, so we fly to author.
--   * PHYSICS FPS: during PLAY with a physics body, adds velocity to the body (so it obeys gravity and
--     collides), and the engine rides the camera on the body. Drop a Physics Component (Dynamic
--     Capsule) on the camera to turn a fly camera into a walking one -- the script only "adds forces".
--
-- Controls: hold Right Mouse to look. Fly: while RMB held WASD move, Left Shift boosts, Space/Left Ctrl
-- go up/down. Physics FPS: WASD walk anytime (no RMB needed), Left Shift boosts, Space jumps.

local props = exposed {
    speed_multiplier = 1.0,  -- fly speed scale
    boost_multiplier = 4.0,  -- while Left Shift is held (both modes)
    invert_y = false,
    move_smoothing = 12.0,   -- fly mode only: higher = snappier, 0 = instant
    walk_speed = 6.0,        -- physics mode ground speed (m/s)
    jump_speed = 5.0,        -- physics mode jump velocity (m/s)
}

local skip_next_rotation = false
-- fly-mode smoothed axes carried between frames
local move_fwd, move_side, move_up = 0.0, 0.0, 0.0
local since_jump = 1.0 -- physics: seconds since last jump, gates re-jump

local function approach(current, target, rate, delta)
    if rate <= 0.0 then return target end
    local t = 1.0 - math.exp(-rate * delta)
    return current + (target - current) * t
end

-- Mouse look while Right Mouse is held (relative-mouse capture). Returns whether RMB is down.
local function do_look(cam)
    local rmb = input.is_right_mouse_down()
    if not engine.is_play_mode() and input.is_viewport_focused and not input.is_viewport_focused() then
        rmb = false
    end
    if rmb then
        if not input.is_relative_mouse() then
            input.set_relative_mouse(true)
            skip_next_rotation = true -- swallow the first jumpy delta after capture
        end
        local m = input.get_mouse_delta()
        if not skip_next_rotation then
            local my = props.invert_y and -m.y or m.y
            cam:rotate(m.x, my)
        else
            skip_next_rotation = false
        end
    elseif input.is_relative_mouse() then
        input.set_relative_mouse(false)
    end
    return rmb
end

-- Camera-relative horizontal basis, flattened onto the ground plane.
local function ground_basis(cam)
    local f = cam:get_front()
    local r = cam:get_right()
    local fx, fz = f.x, f.z
    local fl = math.sqrt(fx * fx + fz * fz)
    if fl > 1e-4 then fx, fz = fx / fl, fz / fl end
    local rx, rz = r.x, r.z
    local rl = math.sqrt(rx * rx + rz * rz)
    if rl > 1e-4 then rx, rz = rx / rl, rz / rl end
    return fx, fz, rx, rz
end

-- PHYSICS FPS: WASD -> horizontal body velocity (camera-relative); keep vertical for gravity/jump.
-- WASD walk anytime (no RMB gate -- this is a character controller, not a fly cam). The engine rides
-- the camera on the body, so we never set the camera position here.
local function update_physics(cam, delta)
    local fx, fz, rx, rz = ground_basis(cam)
    local dx, dz = 0.0, 0.0
    if input.is_key_down("W") then dx = dx + fx; dz = dz + fz end
    if input.is_key_down("S") then dx = dx - fx; dz = dz - fz end
    if input.is_key_down("D") then dx = dx + rx; dz = dz + rz end
    if input.is_key_down("A") then dx = dx - rx; dz = dz - rz end
    local dl = math.sqrt(dx * dx + dz * dz)
    if dl > 1e-4 then dx, dz = dx / dl, dz / dl end

    local speed = props.walk_speed
    if input.is_key_down("Left Shift") then speed = speed * props.boost_multiplier end

    local v = physics.get_velocity(self) or { x = 0, y = 0, z = 0 }
    local vy = v.y
    since_jump = since_jump + delta
    -- grounded heuristic: near-zero vertical speed + jump cooldown (avoids a mid-air double jump).
    if input.is_key_down("Space") and math.abs(vy) < 2.0 and since_jump > 0.4 then
        vy = props.jump_speed
        since_jump = 0.0
    end
    physics.set_velocity(self, dx * speed, vy, dz * speed)
    physics.set_angular_velocity(self, 0.0, 0.0, 0.0) -- keep the capsule upright
end

-- FREE-FLY (kinematic): move the camera directly, with eased start/stop.
local function update_fly(cam, delta, rmb)
    local tf, ts, tu = 0.0, 0.0, 0.0
    if rmb then
        if input.is_key_down("W") then tf = tf + 1.0 end
        if input.is_key_down("S") then tf = tf - 1.0 end
        if input.is_key_down("D") then ts = ts + 1.0 end
        if input.is_key_down("A") then ts = ts - 1.0 end
        if input.is_key_down("Space") then tu = tu + 1.0 end
        if input.is_key_down("Left Ctrl") then tu = tu - 1.0 end
    end
    move_fwd = approach(move_fwd, tf, props.move_smoothing, delta)
    move_side = approach(move_side, ts, props.move_smoothing, delta)
    move_up = approach(move_up, tu, props.move_smoothing, delta)

    local mag = math.sqrt(move_fwd * move_fwd + move_side * move_side + move_up * move_up)
    if mag > 0.0001 then
        local boost = (rmb and input.is_key_down("Left Shift")) and props.boost_multiplier or 1.0
        local speed = cam:get_speed() * delta * props.speed_multiplier * boost
        local scale = mag > 1.0 and (1.0 / mag) or 1.0
        local fwd_amt = move_fwd * scale * speed
        local side_amt = move_side * scale * speed
        local up_amt = move_up * scale * speed
        if fwd_amt > 0.0 then cam:move("forward", fwd_amt)
        elseif fwd_amt < 0.0 then cam:move("backward", -fwd_amt) end
        if side_amt > 0.0 then cam:move("right", side_amt)
        elseif side_amt < 0.0 then cam:move("left", -side_amt) end
        if up_amt ~= 0.0 then
            local p = cam:get_position()
            cam:set_position(vec3(p.x, p.y + up_amt, p.z))
        end
    else
        move_fwd, move_side, move_up = 0.0, 0.0, 0.0
    end
end

local function update(dt)
    local cam = get_camera()
    if not cam then return end
    local delta = dt or (engine.get_metrics().delta_ms / 1000.0)
    local rmb = do_look(cam)
    -- Physics FPS only during play (bodies simulate only then); in the editor, fly to author freely.
    if self and engine.is_play_mode() and physics.has_body(self) then
        update_physics(cam, delta)
    else
        update_fly(cam, delta, rmb)
    end
end

local function init()
    pe_log("[fly_camera_play] active - RMB look; fly: RMB+WASD; physics: WASD walk, Space jump, Shift boost")
end

hooks {
    init = init,
    update = update,
}
