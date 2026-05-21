local sensor = nil
local direction = 1.0
local registered = false
local overlapping = {}
local overlap_count = 0

local colors = {
    sensor_idle = { base = vec4(0.08, 0.35, 1.0, 0.38), emissive = vec3(0.02, 0.09, 0.45) },
    sensor_touching = { base = vec4(0.0, 0.95, 0.25, 0.58), emissive = vec3(0.0, 0.60, 0.14) },
    block_idle = { base = vec4(0.78, 0.82, 0.88, 1.0), emissive = vec3(0.06, 0.07, 0.08) },
    block_touching = { base = vec4(1.0, 0.86, 0.05, 1.0), emissive = vec3(0.70, 0.48, 0.0) },
}

local function set_color(node, color, translucent)
    if not node or not node:is_valid() then
        return
    end

    material.set_render_type(node, translucent and "alpha_blend" or "opaque")
    material.set(node, "base_color", color.base)
    material.set(node, "emissive", color.emissive)
end

local function node_key(node)
    if node and node:is_valid() then
        return node:get_name()
    end
    return "<invalid>"
end

local function set_sensor_color()
    if overlap_count > 0 then
        set_color(sensor, colors.sensor_touching, true)
    else
        set_color(sensor, colors.sensor_idle, true)
    end
end

local function reset_state()
    overlapping = {}
    overlap_count = 0
    _G.trigger_color_demo = {
        enters = 0,
        exits = 0,
        overlapping = 0,
    }
end

local function register()
    if registered then
        return
    end

    reset_state()
    sensor = scene.find_model("Moving Trigger Mesh")

    set_sensor_color()
    for _, name in ipairs({ "Collision Mesh A", "Collision Mesh B", "Collision Mesh C" }) do
        set_color(scene.find_model(name), colors.block_idle, false)
    end

    if not sensor or not sensor:is_valid() then
        pe_log("[TriggerDemo] missing Moving Trigger Mesh")
        return
    end

    physics.on_trigger_enter(sensor, function(other, self_trigger)
        local key = node_key(other)
        if not overlapping[key] then
            overlapping[key] = true
            overlap_count = overlap_count + 1
        end

        _G.trigger_color_demo.enters = _G.trigger_color_demo.enters + 1
        _G.trigger_color_demo.overlapping = overlap_count
        set_color(self_trigger, colors.sensor_touching, true)
        set_color(other, colors.block_touching, false)
        pe_log(string.format("[TriggerDemo] ENTER Moving Trigger Mesh overlapping %s", key))
    end)

    physics.on_trigger_exit(sensor, function(other, self_trigger)
        local key = node_key(other)
        if overlapping[key] then
            overlapping[key] = nil
            overlap_count = math.max(0, overlap_count - 1)
        end

        _G.trigger_color_demo.exits = _G.trigger_color_demo.exits + 1
        _G.trigger_color_demo.overlapping = overlap_count
        set_sensor_color()
        set_color(other, colors.block_idle, false)
        pe_log(string.format("[TriggerDemo] EXIT Moving Trigger Mesh left %s", key))
    end)

    registered = true
    pe_log("[TriggerDemo] ready: the blue trigger mesh moves; it turns green while overlapping collision meshes")
end

hooks {
    init = function()
        register()
    end,

    update = function()
        register()

        if not sensor or not sensor:is_valid() then
            return
        end

        local pos = sensor:get_position()
        if pos.x > 3.15 then
            direction = -1.0
        elseif pos.x < -3.15 then
            direction = 1.0
        end

        local y_velocity = (0.48 - pos.y) * 6.0 + 0.08
        physics.set_velocity(sensor, direction * 2.45, y_velocity, 0.0)
    end,

    destroy = function()
        registered = false
    end,
}
