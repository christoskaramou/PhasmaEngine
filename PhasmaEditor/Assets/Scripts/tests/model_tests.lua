-- Model binding test suite

function run_model_tests()
    pe_log("=== Model Tests ===")
    T.reset()

    -- primitives.quad
    local q = primitives.quad()
    T.check("primitives.quad", q ~= nil)
    if q then
        T.check("quad is_primitive", q:is_primitive())
        remove_model(q)
    end

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

    -- clone_model
    local clone = clone_model(cube, 16, 0, 0)
    T.check("clone_model", clone ~= nil)
    if clone then
        local cpos = clone:get_position()
        T.check("clone position", cpos.x == 16.0)
        remove_model(clone)
    end

    -- scatter_models
    local scattered = scatter_models(cube, 3, 4.0, 0, 0, 0, 1.0, 1.0)
    T.check("scatter_models returns table", type(scattered) == "table")
    T.check("scatter_models count", #scattered == 3)
    for _, m in ipairs(scattered) do
        remove_model(m)
    end

    -- focus_camera_on
    focus_camera_on(cube)
    T.check("focus_camera_on runs", true)

    -- per-node access
    local nc = cube:get_node_count()
    T.check("node count > 0", nc > 0)

    if nc > 0 then
        local nname = cube:get_node_name(0)
        T.check("get_node_name", type(nname) == "string")

        local npos = cube:get_node_position(0)
        T.check("get_node_position", npos ~= nil)

        local wpos = cube:get_node_world_position(0)
        T.check("get_node_world_position", wpos ~= nil)

        cube:set_node_position(0, vec3(4, 0, 0))
        local npos2 = cube:get_node_position(0)
        T.check("set_node_position", npos2.x == 4.0)

        local nm = cube:get_node_mesh(0)
        T.check("get_node_mesh returns int", type(nm) == "number")
    end

    -- cleanup
    remove_model(cube)

    T.summary("Model Tests")
end
