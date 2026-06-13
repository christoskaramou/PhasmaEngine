-- camera_fly.lua
-- phasma: editor-only
-- Replaces C++ camera movement (Window::SmoothMouseRotation + WASD handling)
-- Uses update_editor() so play-mode camera behavior stays project-owned

-- Variables declared here are editable from the Properties panel when this
-- script is attached to a node. The returned table is live: changes made in
-- the editor are immediately visible to the functions below.
local props = exposed {
    speed_multiplier = 1.0,
    invert_y = false,
    -- Movement smoothing rate: higher = snappier, lower = floatier glide.
    -- Set to 0 to disable smoothing (instant start/stop, original feel).
    move_smoothing = 12.0,
}

local skip_next_rotation = false

-- Smoothed movement axes carried between frames so the camera eases into
-- motion on key-press and coasts to a stop on release (lerp-style follow).
local move_fwd = 0.0  -- +forward (W) / -backward (S)
local move_side = 0.0 -- +right (D) / -left (A)

-- Frame-rate independent exponential smoothing toward a target value.
local function approach(current, target, rate, delta)
    if rate <= 0.0 then
        return target
    end
    local t = 1.0 - math.exp(-rate * delta)
    return current + (target - current) * t
end

local function update_editor()
    local cam = get_camera()
    if not cam then return end

    if rawget(_G, "phasma_editor_fly_camera_blocked") == true then
        if input.is_relative_mouse() then
            input.set_relative_mouse(false)
        end
        skip_next_rotation = false
        move_fwd, move_side = 0.0, 0.0
        return
    end

    local metrics = engine.get_metrics()
    local delta = metrics.delta_ms / 1000.0 -- seconds

    local rmb = input.is_right_mouse_down() and input.is_viewport_focused()

    -- Mouse look (right-click held)
    if rmb then
        if not input.is_relative_mouse() then
            input.set_relative_mouse(true)
            skip_next_rotation = true
        end

        local mouse = input.get_mouse_delta()

        if not skip_next_rotation then
            local my = props.invert_y and -mouse.y or mouse.y
            cam:rotate(mouse.x, my)
        else
            skip_next_rotation = false
        end
    else
        if input.is_relative_mouse() then
            input.set_relative_mouse(false)
        end
    end

    -- Desired movement from input (only while right-click held). When the
    -- button or keys are released the target falls to zero and the smoothed
    -- axes decay, so the camera glides to a stop instead of snapping.
    local target_fwd = 0.0
    local target_side = 0.0
    if rmb then
        if input.is_key_down("W") then target_fwd = target_fwd + 1.0 end
        if input.is_key_down("S") then target_fwd = target_fwd - 1.0 end
        if input.is_key_down("D") then target_side = target_side + 1.0 end
        if input.is_key_down("A") then target_side = target_side - 1.0 end
    end

    move_fwd = approach(move_fwd, target_fwd, props.move_smoothing, delta)
    move_side = approach(move_side, target_side, props.move_smoothing, delta)

    -- Apply the smoothed velocity, skipping once it decays to a negligible amount.
    local mag = math.sqrt(move_fwd * move_fwd + move_side * move_side)
    if mag > 0.0001 then
        local speed = cam:get_speed() * delta * props.speed_multiplier
        -- Clamp the combined vector to unit length so diagonals are not faster.
        local scale = mag > 1.0 and (1.0 / mag) or 1.0
        local fwd_amt = move_fwd * scale * speed
        local side_amt = move_side * scale * speed

        if fwd_amt > 0.0 then
            cam:move("forward", fwd_amt)
        elseif fwd_amt < 0.0 then
            cam:move("backward", -fwd_amt)
        end

        if side_amt > 0.0 then
            cam:move("right", side_amt)
        elseif side_amt < 0.0 then
            cam:move("left", -side_amt)
        end
    else
        move_fwd = 0.0
        move_side = 0.0
    end
end

hooks {
    update_editor = update_editor
}
