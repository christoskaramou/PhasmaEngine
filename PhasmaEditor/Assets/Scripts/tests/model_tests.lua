-- Model binding test suite

function run_model_tests()
    pe_log("=== Model Tests ===")
    T.reset()

    -- primitives.quad
    local q = primitives.quad()
    T.check("primitives.quad", q ~= nil)
    if q then q:remove() end

    -- primitives.skinned_strip_2d
    local strip = primitives.skinned_strip_2d(2.0, 0.5, 8, 4)
    T.check("primitives.skinned_strip_2d", strip ~= nil)
    if strip then
        local strip_info = strip:get_mesh_info()
        T.check("skinned strip mesh_info", strip_info ~= nil)
        if strip_info then
            T.check("skinned strip vertex_count", strip_info.vertex_count == 18)
            T.check("skinned strip index_count", strip_info.index_count == 48)
        end
        T.check("skinned strip joint_count", animation.get_joint_count(strip) == 4)
        T.check("skinned strip ik solve", animation.solve_strip_ik_2d(strip, vec2(0.75, 0.35), 4))
    end
    scene.clear()

    -- attached skinned_strip_2d keeps its skeleton source
    local mixed = primitives.cube(1.0)
    T.check("mixed host cube created", mixed ~= nil)
    if mixed then
        scene.attach_primitive(mixed, "skinned_strip_2d")
        T.check("attached skinned strip mesh_count", mixed:get_mesh_count() == 2)
        T.check("attached skinned strip joint_count", animation.get_joint_count(mixed) == 6)
        T.check("attached skinned strip ik solve", animation.solve_strip_ik_2d(mixed, vec2(1.0, 0.25), 4))
    end
    scene.clear()

    -- get_scale / get_rotation defaults
    local cube = primitives.cube(1.0)
    T.check("cube created", cube ~= nil)
    if not cube then
        T.summary("Model Tests")
        return
    end

    local scale = cube:get_scale()
    T.check("default scale x", scale.x == 1.0)
    T.check("default scale y", scale.y == 1.0)
    T.check("default scale z", scale.z == 1.0)

    local rot = cube:get_rotation()
    T.check("default rotation", rot ~= nil)

    -- set_transform then read back
    cube:set_transform(vec3(2, 4, 8), vec3(0, 0, 0), vec3(2, 2, 2))
    local pos = cube:get_position()
    T.check("set_transform position x", pos.x == 2.0)
    T.check("set_transform position y", pos.y == 4.0)

    local s2 = cube:get_scale()
    T.check("set_transform scale", s2.x == 2.0 and s2.y == 2.0 and s2.z == 2.0)

    -- get_bounding_box
    local bb = cube:get_bounding_box()
    T.check("bounding_box has min", bb.min ~= nil)
    T.check("bounding_box has max", bb.max ~= nil)
    T.check("bounding_box has center", bb.center ~= nil)
    T.check("bounding_box has size", bb.size ~= nil)

    -- is_valid
    T.check("is_valid", cube:is_valid())

    -- get/set name
    cube:set_name("test_cube")
    T.check("set/get name", cube:get_name() == "test_cube")

    -- get_mesh_index
    local mi = cube:get_mesh_index()
    T.check("get_mesh_index returns int", type(mi) == "number")

    -- get_mesh_info
    local info = cube:get_mesh_info()
    T.check("get_mesh_info", info ~= nil)
    if info then
        T.check("mesh_info has vertex_count", type(info.vertex_count) == "number")
        T.check("mesh_info has index_count", type(info.index_count) == "number")
    end

    -- get_children
    local children = cube:get_children()
    T.check("get_children returns table", type(children) == "table")

    -- scene.get_model_count
    local count = scene.get_model_count()
    T.check("scene.get_model_count > 0", count > 0)

    -- cleanup
    cube:remove()

    T.summary("Model Tests")
end
