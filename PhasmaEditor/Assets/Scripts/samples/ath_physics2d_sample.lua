-- Run with: local code = assert(fs.read("Scripts/samples/ath_physics2d_sample.lua")); assert(load(code))()

ath_physics2d_sample = ath_physics2d_sample or {}

local M = ath_physics2d_sample
local UPDATE_ID = "ath_physics2d_sample"

local colors = {
    floor = vec4(0.18, 0.20, 0.23, 1.0),
    wall = vec4(0.22, 0.24, 0.28, 1.0),
    ramp = vec4(0.38, 0.36, 0.31, 1.0),
    paddle = vec4(0.12, 0.76, 0.47, 1.0),
    sensor_idle = vec4(0.12, 0.38, 1.0, 0.28),
    sensor_active = vec4(0.04, 1.0, 0.52, 0.45),
    box = vec4(0.96, 0.63, 0.16, 1.0),
    circle = vec4(0.94, 0.26, 0.35, 1.0),
    capsule = vec4(0.42, 0.70, 1.0, 1.0),
}

local state = {
    bodies = {},
    sensor = nil,
    sensor_body = 0,
    sensor_overlaps = 0,
    sensor_active = false,
    paddle = nil,
    paddle_dir = 1.0,
    r_was_down = false,
    previous_render_mode = nil,
}

local function log(message)
    if pe_log then
        pe_log("[ATH2D Sample] " .. message)
    end
end

local function require_runtime()
    if not ath_shapes2d then
        log("ath_shapes2d is not loaded")
        return false
    end

    if not physics2d or not physics2d.is_available() then
        log("physics2d is unavailable; configure with PE_PHYSICS2D=ON")
        return false
    end

    return true
end

local function set_shape_color(shape, color)
    if not material or not shape then
        return
    end

    local render_type = color.w and color.w < 1.0 and "alpha_blend" or "opaque"
    for _, node in ipairs(shape.parts or {}) do
        if node and node:is_valid() then
            material.set_render_type(node, render_type)
            material.set(node, "base_color", color)
        end
    end
end

local function apply_sample_render_settings()
    if not settings or not settings.get_render_mode or not settings.set_render_mode then
        return
    end

    state.previous_render_mode = settings.get_render_mode()
    if state.previous_render_mode ~= "raster" then
        settings.set_render_mode("raster")
        log("render mode set to raster for the 2D physics sample")
    end
end

local function restore_render_settings()
    if settings and settings.set_render_mode and state.previous_render_mode then
        settings.set_render_mode(state.previous_render_mode)
    end
    state.previous_render_mode = nil
end

local function dynamic_shape(shape, x, y, vx, vy, spin)
    shape.spawn = { x = x, y = y, vx = vx, vy = vy, spin = spin or 0.0 }
    table.insert(state.bodies, shape)
    ath_shapes2d.set_velocity(shape, vx, vy)
    if physics2d and shape.body_id ~= 0 then
        physics2d.set_angular_velocity(shape.body_id, spin or 0.0)
    end
    return shape
end

local function reset_body(shape)
    if not shape or not shape.spawn or not physics2d then
        return
    end

    physics2d.set_transform(shape.body_id, shape.spawn.x, shape.spawn.y, 0.0)
    physics2d.set_velocity(shape.body_id, shape.spawn.vx, shape.spawn.vy)
    physics2d.set_angular_velocity(shape.body_id, shape.spawn.spin or 0.0)
end

local function configure_camera()
    local cam = scene.get_active_camera()
    if not cam then
        cam = scene.add_camera()
        scene.set_active_camera(cam)
    end
    ath_shapes2d.configure_camera(cam, { size = 13.5, distance = 48.0 })
    if scene.add_directional_light then
        scene.add_directional_light()
    end
end

local function build_static_bounds()
    ath_shapes2d.box({
        name = "ATH2D Floor",
        x = 0.0,
        y = -4.8,
        width = 14.4,
        height = 0.55,
        body = "static",
        friction = 0.85,
        color = colors.floor,
    })

    ath_shapes2d.box({
        name = "ATH2D Left Wall",
        x = -7.45,
        y = -0.2,
        width = 0.45,
        height = 9.7,
        body = "static",
        friction = 0.7,
        color = colors.wall,
    })

    ath_shapes2d.box({
        name = "ATH2D Right Wall",
        x = 7.45,
        y = -0.2,
        width = 0.45,
        height = 9.7,
        body = "static",
        friction = 0.7,
        color = colors.wall,
    })

    ath_shapes2d.box({
        name = "ATH2D Ramp",
        x = -3.1,
        y = -2.35,
        width = 4.5,
        height = 0.35,
        angle = math.rad(-13.0),
        body = "static",
        friction = 0.8,
        color = colors.ramp,
    })

    ath_shapes2d.box({
        name = "ATH2D Upper Ramp",
        x = 3.1,
        y = 0.55,
        width = 4.2,
        height = 0.35,
        angle = math.rad(12.0),
        body = "static",
        friction = 0.8,
        color = colors.ramp,
    })
end

local function build_sensor()
    state.sensor = ath_shapes2d.box({
        name = "ATH2D Sensor Gate",
        x = 5.35,
        y = -0.7,
        width = 1.0,
        height = 7.0,
        body = "static",
        is_sensor = true,
        color = colors.sensor_idle,
    })
    state.sensor_body = state.sensor.body_id
end

local function build_paddle()
    state.paddle = ath_shapes2d.box({
        name = "ATH2D Kinematic Paddle",
        x = 0.0,
        y = -3.35,
        width = 2.6,
        height = 0.34,
        body = "kinematic",
        friction = 0.5,
        restitution = 0.2,
        color = colors.paddle,
    })
    ath_shapes2d.set_velocity(state.paddle, 2.7, 0.0)
end

local function build_dynamic_stack()
    dynamic_shape(ath_shapes2d.circle({
        name = "ATH2D Ball A",
        x = -4.7,
        y = 3.65,
        radius = 0.38,
        body = "dynamic",
        density = 1.0,
        friction = 0.35,
        restitution = 0.62,
        color = colors.circle,
    }), -4.7, 3.65, 2.2, 0.0, 4.4)

    dynamic_shape(ath_shapes2d.box({
        name = "ATH2D Box A",
        x = -2.3,
        y = 4.6,
        width = 0.74,
        height = 0.74,
        angle = math.rad(11.0),
        body = "dynamic",
        density = 1.2,
        friction = 0.6,
        restitution = 0.22,
        color = colors.box,
    }), -2.3, 4.6, 1.15, 0.0, -2.2)

    dynamic_shape(ath_shapes2d.capsule({
        name = "ATH2D Capsule A",
        x = 0.55,
        y = 5.35,
        height = 1.55,
        radius = 0.28,
        angle = math.rad(90.0),
        body = "dynamic",
        density = 0.9,
        friction = 0.45,
        restitution = 0.28,
        color = colors.capsule,
    }), 0.55, 5.35, -0.8, 0.0, 2.8)

    dynamic_shape(ath_shapes2d.circle({
        name = "ATH2D Ball B",
        x = 3.4,
        y = 4.25,
        radius = 0.48,
        body = "dynamic",
        density = 0.8,
        friction = 0.28,
        restitution = 0.78,
        color = vec4(0.78, 0.43, 1.0, 1.0),
    }), 3.4, 4.25, -1.75, 0.0, -3.5)
end

local function update_sensor_contacts()
    if not physics2d or state.sensor_body == 0 then
        return
    end

    local contacts = physics2d.get_contacts()
    local changed = false
    for _, contact in ipairs(contacts) do
        if contact.sensor and (contact.a == state.sensor_body or contact.b == state.sensor_body) then
            if contact.began then
                state.sensor_overlaps = state.sensor_overlaps + 1
                changed = true
            elseif contact.ended then
                state.sensor_overlaps = math.max(0, state.sensor_overlaps - 1)
                changed = true
            end
        end
    end

    if changed then
        log("sensor overlaps: " .. tostring(state.sensor_overlaps))
    end

    local active = state.sensor_overlaps > 0
    if active ~= state.sensor_active then
        state.sensor_active = active
        set_shape_color(state.sensor, active and colors.sensor_active or colors.sensor_idle)
    end
end

local function update_paddle()
    if not state.paddle or state.paddle.body_id == 0 then
        return
    end

    local transform = physics2d.get_transform(state.paddle.body_id)
    if not transform then
        return
    end

    if transform.x > 4.4 then
        state.paddle_dir = -1.0
    elseif transform.x < -4.4 then
        state.paddle_dir = 1.0
    end

    ath_shapes2d.set_velocity(state.paddle, state.paddle_dir * 2.7, 0.0)
end

local function recycle_fallen_bodies()
    for _, body in ipairs(state.bodies) do
        local transform = physics2d.get_transform(body.body_id)
        if not transform or transform.y < -7.2 or math.abs(transform.x) > 9.2 then
            reset_body(body)
        end
    end
end

local function update()
    local r_down = input and input.is_key_down("R")
    if r_down and not state.r_was_down then
        M.reset()
        state.r_was_down = true
        return
    end
    state.r_was_down = r_down

    if not physics2d or physics2d.is_paused() then
        return
    end

    update_paddle()
    recycle_fallen_bodies()
    update_sensor_contacts()
end

function M.stop()
    if script then
        script.remove_update(UPDATE_ID)
    end
    if physics2d then
        physics2d.clear()
    end
    restore_render_settings()
    state.bodies = {}
    state.sensor = nil
    state.sensor_body = 0
    state.sensor_overlaps = 0
    state.sensor_active = false
    state.paddle = nil
end

function M.reset()
    if not require_runtime() then
        return false
    end

    M.stop()
    scene.clear()
    physics2d.clear()
    physics2d.set_gravity(0.0, -10.0)
    physics2d.set_paused(false)
    apply_sample_render_settings()

    state.bodies = {}
    state.sensor_overlaps = 0
    state.sensor_active = false
    state.paddle_dir = 1.0
    state.r_was_down = false

    configure_camera()
    build_static_bounds()
    build_sensor()
    build_paddle()
    build_dynamic_stack()

    script.on_update(UPDATE_ID, update, "always")
    log("ready; press R while the viewport is focused to reset")
    return true
end

M.reset()
