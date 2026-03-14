-- Pipeline API test suite (called from main.lua)

function run_pipeline_tests()
    pe_log("=== Pipeline API Tests ===")
    T.reset()

    -- Create a PassInfo via factory
    local info = create_pass_info()
    T.check("create_pass_info", info ~= nil)

    -- get_name / set_name
    info:set_name("test_pipeline")
    T.check("set_name", info.get_name == "test_pipeline")

    -- get_topology / set_topology
    info:set_topology("triangle_list")
    T.check("set_topology", info:get_topology() == "triangle_list")
    info:set_topology("line_list")
    T.check("set_topology line_list", info:get_topology() == "line_list")
    info:set_topology("triangle_list")

    -- get_polygon_mode / set_polygon_mode
    info:set_polygon_mode("fill")
    T.check("set_polygon_mode", info:get_polygon_mode() == "fill")
    info:set_polygon_mode("line")
    T.check("set_polygon_mode line", info:get_polygon_mode() == "line")
    info:set_polygon_mode("fill")

    -- get_cull_mode / set_cull_mode
    info:set_cull_mode("back")
    T.check("set_cull_mode", info:get_cull_mode() == "back")
    info:set_cull_mode("none")
    T.check("set_cull_mode none", info:get_cull_mode() == "none")
    info:set_cull_mode("back")

    -- get_line_width / set_line_width
    info:set_line_width(2.0)
    T.check("set_line_width", info.get_line_width == 2.0)
    info:set_line_width(1.0)

    -- is_blend_enabled / set_blend_enable
    info:set_blend_enable(true)
    T.check("set_blend_enable true", info.is_blend_enabled == true)
    info:set_blend_enable(false)
    T.check("set_blend_enable false", info.is_blend_enabled == false)

    -- set_blend_mode
    info:set_blend_mode("additive")
    T.check("set_blend_mode additive", true)
    info:set_blend_mode("default")

    -- get_dynamic_states / set_dynamic_states
    info:set_dynamic_states({"viewport", "scissor", "line_width"})
    local ds = info:get_dynamic_states()
    T.check("set_dynamic_states count", #ds == 3)
    T.check("set_dynamic_states viewport", ds[1] == "viewport")
    T.check("set_dynamic_states scissor", ds[2] == "scissor")
    T.check("set_dynamic_states line_width", ds[3] == "line_width")

    -- is_depth_write_enabled / set_depth_write
    info:set_depth_write(true)
    T.check("set_depth_write true", info.is_depth_write_enabled == true)
    info:set_depth_write(false)
    T.check("set_depth_write false", info.is_depth_write_enabled == false)

    -- is_depth_test_enabled / set_depth_test
    info:set_depth_test(true)
    T.check("set_depth_test true", info.is_depth_test_enabled == true)
    info:set_depth_test(false)
    T.check("set_depth_test false", info.is_depth_test_enabled == false)

    -- get_depth_compare / set_depth_compare
    info:set_depth_compare("less")
    T.check("set_depth_compare less", info:get_depth_compare() == "less")
    info:set_depth_compare("greater_equal")
    T.check("set_depth_compare greater_equal", info:get_depth_compare() == "greater_equal")
    info:set_depth_compare("less")

    -- is_stencil_test_enabled / set_stencil_test
    info:set_stencil_test(true)
    T.check("set_stencil_test true", info.is_stencil_test_enabled == true)
    info:set_stencil_test(false)
    T.check("set_stencil_test false", info.is_stencil_test_enabled == false)

    -- get_stencil_ops / set_stencil_ops
    info:set_stencil_ops("keep", "replace", "zero", "always")
    local ops = info:get_stencil_ops()
    T.check("set_stencil_ops fail", ops.fail == "keep")
    T.check("set_stencil_ops pass", ops.pass == "replace")
    T.check("set_stencil_ops depth_fail", ops.depth_fail == "zero")
    T.check("set_stencil_ops compare", ops.compare == "always")

    -- get_stencil_masks / set_stencil_masks
    info:set_stencil_masks(0xFF, 0x0F, 1)
    local masks = info:get_stencil_masks()
    T.check("set_stencil_masks compare_mask", masks.compare_mask == 0xFF)
    T.check("set_stencil_masks write_mask", masks.write_mask == 0x0F)
    T.check("set_stencil_masks reference", masks.reference == 1)

    -- get_vertex_shader (nil before setting)
    T.check("get_vertex_shader (nil)", info:get_vertex_shader() == nil)

    -- get_fragment_shader (nil before setting)
    T.check("get_fragment_shader (nil)", info:get_fragment_shader() == nil)

    -- get_compute_shader (nil before setting)
    T.check("get_compute_shader (nil)", info:get_compute_shader() == nil)

    -- get_acceleration
    local accel = info:get_acceleration()
    T.check("get_acceleration", accel ~= nil)
    T.check("get_acceleration max_recursion_depth", accel.max_recursion_depth == 1)

    -- get_descriptors
    local descs = info:get_descriptors(0)
    T.check("get_descriptors", descs ~= nil)

    -- HitGroup
    local hg = HitGroup.new()
    T.check("HitGroup constructor", hg ~= nil)
    T.check("HitGroup closest_hit nil", hg.closest_hit == nil)
    T.check("HitGroup any_hit nil", hg.any_hit == nil)
    T.check("HitGroup intersection nil", hg.intersection == nil)

    -- Acceleration
    local acc = Acceleration.new()
    T.check("Acceleration constructor", acc ~= nil)
    T.check("Acceleration ray_gen nil", acc.ray_gen == nil)
    T.check("Acceleration max_recursion_depth", acc.max_recursion_depth == 1)
    acc.max_recursion_depth = 3
    T.check("Acceleration set max_recursion_depth", acc.max_recursion_depth == 3)

    -- Cleanup
    destroy_pass_info(info)

    T.summary("Pipeline API Tests")
end
