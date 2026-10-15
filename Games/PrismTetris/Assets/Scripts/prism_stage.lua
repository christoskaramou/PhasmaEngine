-- Scene-wide look shared by every board: post-processing, a camera fitted to the HUD's board cutout,
-- key light, aurora backdrop, particle textures, camera shake and bloom flares.
local Stage = {}
Stage.__index = Stage

local DESIGN_WIDTH = 1600
local DESIGN_HEIGHT = 1000
local BOARD_ROWS = 20
local BOARD_X = { you = -7.5, jev = 7.5 }
-- Board rows in the 1600x1000 design canvas per layout, and the race cutouts (columns) per seat; the HUD art
-- in tools/make_art.py draws the same cutouts.
local LAYOUTS = {
    solo = { center_x = BOARD_X.you, top = 200, height = 680 },
    versus = { center_x = 0, top = 250, height = 600 },
}
local CUTOUTS = { you = { 410, 740 }, jev = { 860, 1190 } }
local BOARD_CENTER_Y = 10
local FOV_DEGREES = 45
local LIFT = 5
local SWAY = 0.15
local SHAKE_TIME = 0.35
local PULSE_TIME = 0.6
local BLOOM = 3.5
local PULSE_BLOOM = 1.0
local AURORA_GLOW = 2.2
Stage.BLOOM = BLOOM
Stage.BOARD_X = BOARD_X
Stage.CUTOUTS = CUTOUTS

local POST = {
    { "hdr", true },
    { "tonemapping", true },
    { "bloom", true },
    { "bloom_strength", BLOOM },
    { "bloom_range", 2.0 },
    { "ssr", true },
    { "taa", true },
    { "color_grading", true },
    { "color_grading_saturation", 1.1 },
    { "color_grading_contrast", 1.05 },
}

local AURORA_TINTS = {
    { 1.0, 1.0, 1.0 },
    { 0.8, 1.0, 1.25 },
    { 1.25, 0.85, 1.15 },
    { 1.15, 1.05, 0.8 },
}

local function vec(values)
    return vec3(values[1], values[2], values[3])
end

local function paint(node, base, emissive, roughness, metallic)
    material.set(node, "base_color", vec4(base[1], base[2], base[3], 1))
    material.set(node, "roughness", roughness)
    material.set(node, "metallic", metallic)
    material.set(node, "emissive", vec(emissive))
    return node
end

function Stage.new()
    local self = setmetatable({
        time = 0,
        shake = 0,
        shake_strength = 0,
        pulse = 0,
        pulsing = false,
        tint = { 1, 1, 1 },
        tint_target = { 1, 1, 1 },
        side = 1,
        width = 0,
        height = 0,
        layout = LAYOUTS.solo,
        eye = { 0, BOARD_CENTER_Y, 60 },
        target = { 0, BOARD_CENTER_Y, 0 },
    }, Stage)

    self:apply_post()
    self:build_lights()
    self:build_set()
    self:build_particles()
    self:build_camera()
    return self
end

function Stage:apply_post()
    -- Without a scene-settings node the engine forces every post effect off.
    local node = scene.add_empty_node("PrismSceneSettings")
    node:set_scene_settings(true)

    for _, entry in ipairs(POST) do
        settings.set(entry[1], entry[2])
    end
end

function Stage:build_camera()
    self.camera = get_camera()
    self.camera:set_projection_mode("perspective")
    self.camera:set_fov(FOV_DEGREES)

    local surface = runtime_ui.get_surface_size()
    self:resize(surface.width or DESIGN_WIDTH, surface.height or DESIGN_HEIGHT)
end

function Stage:build_lights()
    lights.add_directional()
    local list = lights.get_directional_lights()
    local key = list[#list]
    lights.set_directional_light(key.index, vec3(0, 30, 20), vec3(0.75, 0.85, 1.0), 2.0)

    self.key_light = scene.find_model(key.name)
    if self.key_light then
        self.key_light:set_rotation(vec3(-35, 0, 0))
    end
end

function Stage:build_set()
    local glow = { AURORA_GLOW, AURORA_GLOW, AURORA_GLOW }
    self.aurora = paint(primitives.cube(1), { 0, 0, 0 }, glow, 1, 0)
    self.aurora:set_scale(vec3(150, 94, 0.2))
    self.aurora:set_position(vec3(self.layout.center_x, 16, -45 * self.side))
    material.set_texture(self.aurora, "emissive", "Textures/aurora.png")
end

-- "solo" frames your board alone; "versus" frames both boards. The camera and aurora centre on the layout,
-- and the next resize re-fits even at an unchanged window size.
function Stage:set_layout(name)
    self.layout = LAYOUTS[name]
    self.width = 0
    self.height = 0
    self.aurora:set_position(vec3(self.layout.center_x, 16, -45 * self.side))
end

function Stage:build_particles()
    -- No ambient motes: the engine's spawn sphere collapsed them into one horizontal band (2026-10-15).
    self.textures = {
        glow = particles.load_texture("Textures/glow.png"),
        shard = particles.load_texture("Textures/shard.png"),
        spark = particles.load_texture("Textures/spark.png"),
    }
end

function Stage:resize(width, height)
    if width <= 0 or height <= 0 then
        return
    end
    if width == self.width and height == self.height then
        return
    end

    self.width = width
    self.height = height
    local rows_top = self.layout.top
    local rows_height = self.layout.height
    local scale = math.min(width / DESIGN_WIDTH, height / DESIGN_HEIGHT)
    local top = (height - DESIGN_HEIGHT * scale) / 2
    local board_pixels = rows_height * scale
    local center_pixels = top + (rows_top + rows_height / 2) * scale
    local half_horizontal = math.rad(FOV_DEGREES) / 2
    local tan_vertical = math.tan(math.atan(math.tan(half_horizontal) * height / width))

    -- Straight-on fit: the 20 rows take the cutout's share of the screen height, aimed above the board
    -- centre by the cutout's offset from the screen centre.
    local view_height = BOARD_ROWS * height / board_pixels
    local distance = view_height / 2 / tan_vertical
    local offset = (center_pixels - height / 2) / height * view_height

    -- The lifted camera looks down, so correct the span and centre against the projected board edges.
    for _ = 1, 6 do
        self:aim(offset, distance)
        local board_top = self:project_row(BOARD_ROWS, tan_vertical, height, top, scale)
        local board_bottom = self:project_row(0, tan_vertical, height, top, scale)
        distance = distance * (board_bottom - board_top) / rows_height
        local drift = (board_top + board_bottom) / 2 - (rows_top + rows_height / 2)
        offset = offset - drift * BOARD_ROWS / rows_height
    end

    self:aim(offset, distance)
    self:place_camera(false)
end

function Stage:aim(offset, distance)
    local x = self.layout.center_x
    self.target = { x, BOARD_CENTER_Y + offset, 0 }
    self.eye = { x, BOARD_CENTER_Y + offset + LIFT, distance * self.side }
end

-- Design-space y of the board-plane point at world height `y`, seen through the current eye and target.
function Stage:project_row(y, tan_vertical, height, top, scale)
    local forward_y = self.target[2] - self.eye[2]
    local forward_z = self.target[3] - self.eye[3]
    local length = math.sqrt(forward_y * forward_y + forward_z * forward_z)
    forward_y = forward_y / length
    forward_z = forward_z / length

    -- Look-at with world up +Y and no roll: camera up is the forward vector turned a quarter in the y-z plane.
    local sign = forward_z < 0 and 1 or -1
    local up_y = -sign * forward_z
    local up_z = sign * forward_y
    local relative_y = y - self.eye[2]
    local relative_z = -self.eye[3]

    local depth = relative_y * forward_y + relative_z * forward_z
    local ndc = (relative_y * up_y + relative_z * up_z) / (depth * tan_vertical)
    return ((1 - ndc) * height / 2 - top) / scale
end

function Stage:update(dt, motion)
    self.time = self.time + dt
    self.shake = math.max(0, self.shake - dt / SHAKE_TIME)

    self:place_camera(motion)
    self:update_pulse(dt, motion)
    self:update_aurora(dt)
end

function Stage:place_camera(motion)
    local sway = motion and SWAY or 0
    local jolt = motion and self.shake_strength * self.shake * self.shake or 0
    local dx = math.sin(self.time * 0.47) * sway + (math.random() - 0.5) * jolt
    local dy = math.sin(self.time * 0.31) * sway * 0.6 + (math.random() - 0.5) * jolt
    local eye = self.eye
    local target = self.target

    self.camera:set_position(vec3(eye[1] + dx, eye[2] + dy, eye[3]))
    self.camera:look_at(vec3(target[1] + dx * 0.3, target[2] + dy * 0.3, target[3]))
end

function Stage:update_pulse(dt, motion)
    if not self.pulsing then
        return
    end

    self.pulse = motion and math.max(0, self.pulse - dt / PULSE_TIME) or 0
    settings.set("bloom_strength", BLOOM + PULSE_BLOOM * self.pulse)
    self.pulsing = self.pulse > 0
end

function Stage:update_aurora(dt)
    local blend = math.min(1, dt * 0.8)
    for i = 1, 3 do
        self.tint[i] = self.tint[i] + (self.tint_target[i] - self.tint[i]) * blend
    end

    local breath = AURORA_GLOW * (0.92 + 0.08 * math.sin(self.time * 0.4))
    material.set_emissives({
        self.aurora,
        breath * self.tint[1],
        breath * self.tint[2],
        breath * self.tint[3],
    }, 1)
end

function Stage:jolt(strength)
    self.shake_strength = math.max(strength, self.shake_strength * self.shake)
    self.shake = 1
end

function Stage:flare()
    self.pulse = 1
    self.pulsing = true
end

function Stage:shift(level)
    self.tint_target = AURORA_TINTS[(level - 1) % #AURORA_TINTS + 1]
end

function Stage:point_light()
    lights.add_point()
    local list = lights.get_point_lights()
    local entry = list[#list]
    local light = {
        index = entry.index,
        node = scene.find_model(entry.name),
    }

    self:set_light(light, { 0, 0, 0 }, { 0, 0, 0 }, 0, 1)
    return light
end

function Stage:area_light()
    lights.add_area()
    local list = lights.get_area_lights()
    local entry = list[#list]
    local light = {
        index = entry.index,
        node = scene.find_model(entry.name),
    }

    -- Area lights shine along their local -Z, which faces the board while the camera sits on +Z.
    if light.node and self.side < 0 then
        light.node:set_rotation(vec3(0, 180, 0))
    end

    self:set_area_light(light, { 0, 0, 0 }, { 0, 0, 0 }, 0, 1, 1, 1)
    return light
end

function Stage:set_area_light(light, position, color, intensity, range, width, height)
    local where = vec(position)
    lights.set_area_light(light.index, where, vec(color), intensity, range, width, height)

    if light.node then
        light.node:set_position(where)
    end
end

function Stage:set_light(light, position, color, intensity, radius)
    local where = vec(position)
    lights.set_point_light(light.index, where, vec(color), intensity, radius)

    -- The engine copies light positions from the light's node every frame.
    if light.node then
        light.node:set_position(where)
    end
end

return Stage
