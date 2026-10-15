-- Headless stand-ins for the engine's 3D bindings. They record calls so luajit tests can check the
-- renderer; `record.frozen = true` makes any later node creation or first-time material property count
-- as a load-time rule violation.
local record = {
    nodes = {},
    bursts = {},
    emitters = {},
    textures = {},
    settings = {},
    material_sets = {},
    emissive_batches = 0,
    position_batches = 0,
    frozen = false,
    created_while_frozen = 0,
    settings_nodes = 0,
    point_lights = {},
    directional_lights = {},
    area_lights = {},
    light_nodes = {},
    camera = {},
}

local function new_node(kind)
    if record.frozen then
        record.created_while_frozen = record.created_while_frozen + 1
    end

    local node = {
        kind = kind,
        visible = true,
        position = { 0, 0, 0 },
        scale = { 1, 1, 1 },
        rotation = { 0, 0, 0 },
        material = {},
    }

    function node:set_visible(value)
        self.visible = value
    end

    function node:set_position(v)
        self.position = { v.x, v.y, v.z }
    end

    function node:set_scale(v)
        self.scale = { v.x, v.y, v.z }
    end

    function node:set_rotation(v)
        self.rotation = { v.x, v.y, v.z }
    end

    function node:set_scene_settings(value)
        if value then
            record.settings_nodes = record.settings_nodes + 1
        end
    end

    record.nodes[#record.nodes + 1] = node
    return node
end

vec3 = function(x, y, z)
    return { x = x, y = y, z = z }
end

vec4 = function(x, y, z, w)
    return { x = x, y = y, z = z, w = w }
end

primitives = {
    cube = function(size)
        local node = new_node("cube")
        node.size = size or 1
        return node
    end,
    plane = function(width, depth)
        local node = new_node("plane")
        node.size = { width, depth }
        return node
    end,
}

material = {
    set = function(node, property, value)
        if record.frozen and node.material[property] == nil then
            record.created_while_frozen = record.created_while_frozen + 1
        end

        record.material_sets[property] = (record.material_sets[property] or 0) + 1
        node.material[property] = value
    end,
    set_emissives = function(batch, count)
        record.emissive_batches = record.emissive_batches + 1
        for i = 0, count - 1 do
            local node = batch[i * 4 + 1]
            node.material.emissive = {
                x = batch[i * 4 + 2],
                y = batch[i * 4 + 3],
                z = batch[i * 4 + 4],
            }
        end
    end,
    set_texture = function(node, kind, path)
        node.material["texture_" .. kind] = path
    end,
}

scene = {
    add_empty_node = function(name)
        local node = new_node("empty")
        node.name = name
        return node
    end,
    set_positions = function(batch, count)
        record.position_batches = record.position_batches + 1
        for i = 0, count - 1 do
            local node = batch[i * 4 + 1]
            node.position = {
                batch[i * 4 + 2],
                batch[i * 4 + 3],
                batch[i * 4 + 4],
            }
        end
    end,
    find_model = function(name)
        return record.light_nodes[name]
    end,
}

local function add_light(list, prefix)
    local node = new_node("light")
    local total = #record.point_lights + #record.directional_lights + #record.area_lights
    local entry = {
        index = #list,
        name = prefix .. " " .. (total + 1),
        intensity = 0,
        color = { 0, 0, 0 },
        radius = 0,
        node = node,
    }

    record.light_nodes[entry.name] = node
    list[#list + 1] = entry
end

lights = {
    add_point = function()
        add_light(record.point_lights, "Point Light")
    end,
    add_directional = function()
        add_light(record.directional_lights, "Directional Light")
    end,
    add_area = function()
        add_light(record.area_lights, "Area Light")
    end,
    get_counts = function()
        return {
            point = #record.point_lights,
            directional = #record.directional_lights,
            area = #record.area_lights,
        }
    end,
    get_area_lights = function()
        return record.area_lights
    end,
    set_area_light = function(index, position, color, intensity, range, width, height)
        local entry = record.area_lights[index + 1]
        entry.position = { position.x, position.y, position.z }
        entry.color = { color.x, color.y, color.z }
        entry.intensity = intensity
        entry.radius = range
        entry.width = width
        entry.height = height
    end,
    get_point_lights = function()
        return record.point_lights
    end,
    get_directional_lights = function()
        return record.directional_lights
    end,
    set_point_light = function(index, position, color, intensity, radius)
        local entry = record.point_lights[index + 1]
        entry.position = { position.x, position.y, position.z }
        entry.color = { color.x, color.y, color.z }
        entry.intensity = intensity
        entry.radius = radius
    end,
    set_directional_light = function(index, _, color, intensity)
        local entry = record.directional_lights[index + 1]
        entry.color = { color.x, color.y, color.z }
        entry.intensity = intensity
    end,
}

particles = {
    load_texture = function(path)
        record.textures[#record.textures + 1] = path
        return #record.textures
    end,
    emit_burst = function(options)
        record.bursts[#record.bursts + 1] = options
        return #record.bursts
    end,
    add_emitter = function(options)
        record.emitters[#record.emitters + 1] = options
        return #record.emitters - 1
    end,
}

settings = {
    set = function(name, value)
        record.settings[name] = value
    end,
    get = function(name)
        return record.settings[name]
    end,
}

local camera = record.camera

function camera:set_projection_mode(mode)
    self.projection = mode
end

function camera:set_fov(degrees)
    self.fov = degrees
end

function camera:set_position(v)
    self.position = v
end

function camera:look_at(v)
    self.look = v
end

get_camera = function()
    return camera
end

return record
