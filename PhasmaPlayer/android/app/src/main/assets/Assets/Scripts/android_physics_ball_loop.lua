local SPAWN_INTERVAL = 5.0
local BALL_COUNT = 10

local balls = {}
local container = {}
local elapsed = 0.0
local batch = 0

local colors = {
    {0.95, 0.22, 0.22, 1.0},
    {0.15, 0.65, 1.00, 1.0},
    {0.25, 0.90, 0.35, 1.0},
    {1.00, 0.78, 0.18, 1.0},
    {0.85, 0.35, 1.00, 1.0},
    {1.00, 0.45, 0.12, 1.0},
    {0.20, 1.00, 0.82, 1.0},
    {0.95, 0.95, 0.95, 1.0},
    {0.55, 0.75, 1.00, 1.0},
    {1.00, 0.30, 0.60, 1.0},
}

local function make_node(name, primitive, pos, scale, color)
    local node = scene.add_empty_node(name)
    if not node then
        return nil
    end

    scene.attach_primitive(node, primitive)
    node:set_transform(
        vec3(pos[1], pos[2], pos[3]),
        vec3(0.0, 0.0, 0.0),
        vec3(scale[1], scale[2], scale[3]))

    material.set_render_type(node, "opaque")
    material.set(node, "base_color", vec4(color[1], color[2], color[3], color[4]))
    material.set(node, "roughness", 0.38)
    return node
end

local function add_static_box(name, pos, scale, color)
    local node = make_node(name, "cube", pos, scale, color)
    if node then
        physics.add_body(node, "static", "box", {friction = 0.65, restitution = 0.25})
        container[#container + 1] = node
    end
end

local function create_container()
    local wall = {0.62, 0.66, 0.72, 1.0}
    local floor = {0.28, 0.30, 0.34, 1.0}

    add_static_box("PhysicsContainer_Floor", {0.0, -0.15, 0.0}, {6.2, 0.25, 4.2}, floor)
    add_static_box("PhysicsContainer_LeftWall", {-3.15, 0.65, 0.0}, {0.25, 1.6, 4.2}, wall)
    add_static_box("PhysicsContainer_RightWall", {3.15, 0.65, 0.0}, {0.25, 1.6, 4.2}, wall)
    add_static_box("PhysicsContainer_BackWall", {0.0, 0.65, -2.15}, {6.2, 1.6, 0.25}, wall)
    add_static_box("PhysicsContainer_FrontLip", {0.0, 0.15, 2.15}, {6.2, 0.55, 0.25}, wall)
end

local function clear_balls()
    for _, ball in ipairs(balls) do
        if ball and ball:is_valid() then
            physics.remove_body(ball)
            scene.delete_node(ball)
        end
    end
    balls = {}
end

local function update_hud()
    runtime_ui.set_number("physics", "batch", "Batch", batch)
    runtime_ui.set_number("physics", "balls", "Balls", #balls)
    runtime_ui.set_number("physics", "loop", "Loop", math.floor(elapsed + 0.5))
    runtime_ui.show("physics")
end

local function spawn_batch()
    clear_balls()
    batch = batch + 1
    elapsed = 0.0

    for i = 1, BALL_COUNT do
        local x = -2.15 + ((i - 1) % 5) * 1.08 + (math.random() - 0.5) * 0.18
        local z = -0.65 + math.floor((i - 1) / 5) * 1.10 + (math.random() - 0.5) * 0.18
        local y = 3.35 + (math.random() * 0.45)
        local color = colors[((batch + i - 2) % #colors) + 1]
        local ball = make_node("LoopBall_" .. batch .. "_" .. i, "sphere", {x, y, z}, {0.35, 0.35, 0.35}, color)

        if ball then
            physics.add_body(ball, "dynamic", "sphere", {mass = 1.0, friction = 0.42, restitution = 0.55})
            physics.set_velocity(ball, (math.random() - 0.5) * 1.0, 0.0, (math.random() - 0.5) * 1.0)
            physics.set_angular_velocity(ball, math.random() * 2.0, math.random() * 2.0, math.random() * 2.0)
            balls[#balls + 1] = ball
        end
    end

    update_hud()
    pe_log("[AndroidPhysics] spawned batch " .. batch .. " (" .. #balls .. " balls)")
end

local function init()
    math.randomseed(1337)
    create_container()
    spawn_batch()
end

local function update(dt)
    elapsed = elapsed + (dt or 0.016)
    if elapsed >= SPAWN_INTERVAL then
        spawn_batch()
    else
        update_hud()
    end
end

hooks {
    init = init,
    update = update,
}
