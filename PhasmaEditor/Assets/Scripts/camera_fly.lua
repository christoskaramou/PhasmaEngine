-- camera_fly.lua
-- Replaces C++ camera movement (Window::SmoothMouseRotation + WASD handling)
-- Uses update_editor() so it runs every frame, not just in play mode

local skip_next_rotation = false

local function update_editor()
    local cam = get_camera()
    if not cam then return end

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
            cam:rotate(mouse.x, mouse.y)
        else
            skip_next_rotation = false
        end
    else
        if input.is_relative_mouse() then
            input.set_relative_mouse(false)
        end
    end

    -- WASD movement (only while right-click held)
    if rmb then
        local speed = cam:get_speed() * delta

        -- Diagonal normalization
        local fwd = input.is_key_down("W") or input.is_key_down("S")
        local side = input.is_key_down("A") or input.is_key_down("D")
        if fwd and side then
            speed = speed * 0.707
        end

        if input.is_key_down("W") then cam:move("forward", speed) end
        if input.is_key_down("S") then cam:move("backward", speed) end
        if input.is_key_down("A") then cam:move("left", speed) end
        if input.is_key_down("D") then cam:move("right", speed) end
    end
end

hooks {
    update_editor = update_editor
}
