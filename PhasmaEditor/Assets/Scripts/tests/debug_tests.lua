-- Debug API test suite (called from main.lua)

function run_debug_tests()
    pe_log("=== Debug API Tests ===")
    T.reset()

    -- Create test resources to name
    local buf = create_buffer(64, "uniform", "host_write|mapped", "debug_test_buf")
    T.check("create test buffer", buf ~= nil)

    local img = create_image(16, 16, "rgba8", "sampled|transfer_dst", "debug_test_img")
    T.check("create test image", img ~= nil)

    -- Object naming
    debug_utils.set_buffer_name(buf, "renamed_buffer")
    T.check("set_buffer_name", true)

    debug_utils.set_image_name(img, "renamed_image")
    T.check("set_image_name", true)

    -- Queue naming
    local queue = rhi.get_main_queue()
    debug_utils.set_queue_name(queue, "lua_main_queue")
    T.check("set_queue_name", true)

    -- Command buffer / pool naming
    local cmd = queue:acquire_command_buffer()
    debug_utils.set_command_buffer_name(cmd, "lua_cmd")
    T.check("set_command_buffer_name", true)

    local pool = cmd:get_command_pool()
    debug_utils.set_command_pool_name(pool, "lua_cmd_pool")
    T.check("set_command_pool_name", true)

    cmd:return_cmd()

    -- Frame capture (just verify callable, no RenderDoc needed)
    debug_utils.start_frame_capture()
    T.check("start_frame_capture", true)

    debug_utils.end_frame_capture()
    T.check("end_frame_capture", true)

    debug_utils.trigger_capture()
    T.check("trigger_capture", true)

    -- Cleanup
    destroy_image(img)
    destroy_buffer(buf)

    T.summary("Debug API Tests")
end
