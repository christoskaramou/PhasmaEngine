-- Shared 2D shape helpers for AgainstTheHero.

ath_shapes2d = {}

local M = ath_shapes2d

local function value(opts, key, fallback)
    local v = opts and opts[key]
    if v == nil then
        return fallback
    end
    return v
end

local function color_or_default(opts)
    return value(opts, "color", vec4(0.95, 0.82, 0.38, 1.0))
end

local function apply_material(node, opts)
    if not node or not material then
        return
    end

    local color = color_or_default(opts)
    material.set(node, "base_color", color)
    if material.set_render_type then
        material.set_render_type(node, color.w and color.w < 1.0 and "alpha_blend" or "opaque")
    end
end

local function set_node_2d_transform(node, opts)
    local x = value(opts, "x", 0.0)
    local y = value(opts, "y", 0.0)
    local z = value(opts, "depth", value(opts, "z", 0.0))
    local angle = value(opts, "angle", 0.0)

    node:set_position(vec3(x, y, z))
    node:set_rotation(vec3(0.0, 0.0, math.deg(angle)))
end

local function should_add_physics(opts)
    return value(opts, "physics", true) and physics2d and physics2d.is_available()
end

local function body_type(opts)
    return value(opts, "body_type", value(opts, "body", "static"))
end

local function result(kind, node, body_id, parts, opts)
    return {
        kind = kind,
        node = node,
        body_id = body_id or 0,
        parts = parts or {},
        depth = value(opts, "depth", value(opts, "z", 0.0)),
    }
end

function M.configure_camera(cam, opts)
    opts = opts or {}
    cam = cam or scene.get_active_camera()
    if not cam then
        return nil
    end

    local x = value(opts, "x", 0.0)
    local y = value(opts, "y", 0.0)
    local depth = value(opts, "depth", 0.0)
    local distance = value(opts, "distance", 64.0)

    cam:set_projection_mode("orthographic")
    cam:set_orthographic_size(value(opts, "size", 18.0))
    cam:set_near(value(opts, "near", 0.01))
    cam:set_far(value(opts, "far", 1000.0))
    cam:set_position(vec3(x, y, depth + distance))
    cam:look_at(vec3(x, y, depth))
    return cam
end

function M.box(opts)
    opts = opts or {}

    local width = value(opts, "width", value(opts, "w", 1.0))
    local height = value(opts, "height", value(opts, "h", 1.0))
    local node = primitives.quad(width, height)
    if opts.name then
        node:set_name(opts.name)
    end
    set_node_2d_transform(node, opts)
    apply_material(node, opts)

    local body_id = 0
    if should_add_physics(opts) then
        body_id = physics2d.add_box(node, body_type(opts), width, height, opts)
    end

    return result("box", node, body_id, { node }, opts)
end

function M.circle(opts)
    opts = opts or {}

    local radius = value(opts, "radius", value(opts, "r", 0.5))
    local segments = value(opts, "segments", 48)
    local node = primitives.circle(radius, segments)
    if opts.name then
        node:set_name(opts.name)
    end
    set_node_2d_transform(node, opts)
    apply_material(node, opts)

    local body_id = 0
    if should_add_physics(opts) then
        body_id = physics2d.add_circle(node, body_type(opts), radius, opts)
    end

    return result("circle", node, body_id, { node }, opts)
end

function M.capsule(opts)
    opts = opts or {}

    local height = value(opts, "height", value(opts, "h", 1.0))
    local radius = value(opts, "radius", value(opts, "r", 0.25))
    height = math.max(height, radius * 2.0)

    local root = scene.add_empty_node(opts.name or "capsule2d")
    set_node_2d_transform(root, opts)

    local parts = {}
    local segment = math.max(0.0, height - radius * 2.0)
    if segment > 0.001 then
        local core = primitives.quad(radius * 2.0, segment)
        core:set_name((opts.name or "capsule2d") .. "_core")
        core:set_parent(root)
        core:set_position(vec3(0.0, 0.0, 0.0))
        apply_material(core, opts)
        table.insert(parts, core)
    end

    local top = primitives.circle(radius, value(opts, "segments", 32))
    top:set_name((opts.name or "capsule2d") .. "_top")
    top:set_parent(root)
    top:set_position(vec3(0.0, segment * 0.5, 0.0))
    apply_material(top, opts)
    table.insert(parts, top)

    local bottom = primitives.circle(radius, value(opts, "segments", 32))
    bottom:set_name((opts.name or "capsule2d") .. "_bottom")
    bottom:set_parent(root)
    bottom:set_position(vec3(0.0, -segment * 0.5, 0.0))
    apply_material(bottom, opts)
    table.insert(parts, bottom)

    local body_id = 0
    if should_add_physics(opts) then
        body_id = physics2d.add_capsule(root, body_type(opts), height, radius, opts)
    end

    return result("capsule", root, body_id, parts, opts)
end

function M.set_position(shape, x, y)
    if not shape or not shape.node then
        return
    end

    shape.node:set_position(vec3(x, y, shape.depth or 0.0))
    if physics2d and shape.body_id and shape.body_id ~= 0 then
        local transform = physics2d.get_transform(shape.body_id)
        physics2d.set_transform(shape.body_id, x, y, transform and transform.angle or 0.0)
    end
end

function M.set_velocity(shape, x, y)
    if physics2d and shape and shape.body_id and shape.body_id ~= 0 then
        physics2d.set_velocity(shape.body_id, x, y)
    end
end

function M.destroy(shape)
    if not shape then
        return
    end

    if physics2d and shape.body_id and shape.body_id ~= 0 then
        physics2d.remove_body(shape.body_id)
    end

    if shape.node then
        shape.node:remove()
    end
end
