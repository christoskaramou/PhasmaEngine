-- Event API test suite (called from main.lua)

function run_event_tests()
    pe_log("=== Event API Tests ===")
    T.reset()

    -- Factory
    local ev = create_event("test_event")
    T.check("create_event", ev ~= nil)

    -- IsSet (should be false initially)
    T.check("is_set (initial)", ev.is_set == false)

    -- Create resources for set/wait
    local queue = rhi.get_main_queue()
    local cmd = queue:acquire_command_buffer()
    local img = create_image(16, 16, "rgba8", "sampled|transfer_src|transfer_dst|color_attachment", "test_event_img")
    T.check("create test image", img ~= nil)

    -- Set event
    cmd:begin()
    ev:set(cmd, img, "undefined", "shader_read", "color_output", "fragment", "color_write", "shader_read")
    T.check("set", true)

    -- Wait event
    ev:wait()
    T.check("wait", true)

    -- Reset event
    ev:reset("all_commands")
    T.check("reset (all_commands)", true)

    cmd:end_cmd()
    queue:submit(cmd)
    cmd:wait()
    cmd:return_cmd()

    -- Cleanup
    T.check("destroy_event", ev ~= nil)
    destroy_event(ev)
    T.check("destroy_image", img ~= nil)
    destroy_image(img)

    T.summary("Event API Tests")
end
