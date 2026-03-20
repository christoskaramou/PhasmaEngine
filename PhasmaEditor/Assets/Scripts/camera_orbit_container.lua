-- camera_orbit_container.lua
-- Keeps the camera circling the violet container scene in a smooth ellipse.
-- Runs in the live editor and in play mode.

camera_orbit_container_enabled = true

local orbit_t = 0.0
local orbit_center = vec3(0.0, 0.9, 0.0)

-- Tuning
local orbit_speed = 0.55
local orbit_radius_x = 5.8
local orbit_radius_z = 7.2
local orbit_height = 2.35
local orbit_bob = 0.16

local function get_orbit_position(t)
    return vec3(
        orbit_center.x + math.sin(t) * orbit_radius_x,
        orbit_center.y + orbit_height + math.sin(t * 2.0) * orbit_bob,
        orbit_center.z + math.cos(t) * orbit_radius_z
    )
end

local function get_look_target()
    return orbit_center
end

local function aim_camera(cam, target)
    cam:look_at(target)

    local euler = cam:get_euler()
    cam:set_euler(vec3(-euler.x, euler.y, euler.z))
end

local function update_camera()
    local cam = get_camera()
    if not cam then
        return
    end

    local dt = engine.get_metrics().delta_ms / 1000.0
    orbit_t = orbit_t + orbit_speed * dt

    cam:set_fov(50.0)
    cam:set_position(get_orbit_position(orbit_t))
    aim_camera(cam, get_look_target())
end

local function destroy()
    camera_orbit_container_enabled = nil
end

hooks {
    -- update_editor = update_camera,
    -- update = update_camera,
    -- destroy = destroy
}
