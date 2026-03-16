-- camera_figure8.lua
-- Flies the camera in a figure-8 (lemniscate) pattern on Play mode
-- Uses update_play() to avoid conflicting with main.lua's init()/update()

local fig8_center = nil
local fig8_t = 0.0

-- Tuning
local fig8_speed = 0.3      -- how fast it traverses the path
local fig8_radius_x = 8.0   -- width of the 8
local fig8_radius_z = 4.0   -- depth of each lobe
local fig8_height = 3.0     -- vertical amplitude

local function update()
    local cam = get_camera()
    if not cam then return end

    -- Capture starting position on first frame
    if not fig8_center then
        fig8_center = cam:get_position()
    end

    local dt = engine.get_metrics().delta_ms / 1000.0
    fig8_t = fig8_t + fig8_speed * dt

    -- Lemniscate of Bernoulli
    local s = math.sin(fig8_t)
    local c = math.cos(fig8_t)
    local denom = 1.0 + s * s

    local x = fig8_center.x + fig8_radius_x * (c / denom)
    local z = fig8_center.z + fig8_radius_z * (s * c / denom)
    local y = fig8_center.y + fig8_height * math.sin(fig8_t * 2.0) * 0.3

    -- Look ahead: compute position slightly in the future
    local t2 = fig8_t + 0.05
    local s2 = math.sin(t2)
    local c2 = math.cos(t2)
    local d2 = 1.0 + s2 * s2
    local ahead = vec3(
        fig8_center.x + fig8_radius_x * (c2 / d2),
        fig8_center.y + fig8_height * math.sin(t2 * 2.0) * 0.3,
        fig8_center.z + fig8_radius_z * (s2 * c2 / d2)
    )

    cam:set_position(vec3(x, y, z))
    cam:look_at(ahead)
end

hooks {
    update = update
}
