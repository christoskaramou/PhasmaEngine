-- CommandBuffer API test suite (called from main.lua)

function run_command_tests()
    pe_log("=== CommandBuffer API Tests ===")
    T.reset()

    -- Get a queue and acquire a command buffer
    local queue = rhi.get_main_queue()
    T.check("get_main_queue", queue ~= nil)

    local cmd = queue:acquire_command_buffer()
    T.check("acquire_command_buffer", cmd ~= nil)

    -- State properties (before recording)
    T.check("is_recording (before begin)", cmd.is_recording == false)
    T.check("get_family_id", cmd.get_family_id ~= nil)

    local q = cmd:get_queue()
    T.check("get_queue", q ~= nil)

    local pool = cmd:get_command_pool()
    T.check("get_command_pool", pool ~= nil)

    -- Begin recording
    cmd:begin()
    T.check("begin", cmd.is_recording == true)

    -- Create test images
    local color_img = create_image(64, 64, "rgba8", "color_attachment|transfer_src|transfer_dst|sampled", "test_color")
    local color_img2 = create_image(64, 64, "rgba8", "color_attachment|transfer_src|transfer_dst|sampled", "test_color2")
    local depth_img = create_image(64, 64, "d32f", "depth_attachment|transfer_dst|sampled", "test_depth")
    T.check("create test images", color_img ~= nil and color_img2 ~= nil and depth_img ~= nil)

    -- Image barriers (single)
    cmd:image_barrier(color_img, "transfer_dst", "transfer", "transfer_write")
    T.check("image_barrier (color)", true)

    cmd:image_barrier(depth_img, "depth_attachment", "early_fragment", "depth_write")
    T.check("image_barrier (depth)", true)

    -- Clear operations
    cmd:image_barrier(color_img, "transfer_dst", "transfer", "transfer_write")
    cmd:clear_color(color_img)
    T.check("clear_color", true)

    cmd:image_barrier(color_img2, "transfer_dst", "transfer", "transfer_write")
    cmd:clear_colors({color_img, color_img2})
    T.check("clear_colors (batch)", true)

    cmd:image_barrier(depth_img, "transfer_dst", "transfer", "transfer_write")
    cmd:clear_depth_stencil(depth_img)
    T.check("clear_depth_stencil", true)

    cmd:clear_depth_stencils({depth_img})
    T.check("clear_depth_stencils (batch)", true)

    -- Blit image
    cmd:image_barrier(color_img, "transfer_src", "transfer", "transfer_read")
    cmd:image_barrier(color_img2, "transfer_dst", "transfer", "transfer_write")
    cmd:blit_image(color_img, color_img2, "linear")
    T.check("blit_image (linear)", true)

    cmd:blit_image(color_img, color_img2, "nearest")
    T.check("blit_image (nearest)", true)

    cmd:blit_image(color_img, color_img2, "linear", "color")
    T.check("blit_image (explicit color aspect)", true)

    -- Copy image
    cmd:image_barrier(color_img, "transfer_src", "transfer", "transfer_read")
    cmd:image_barrier(color_img2, "transfer_dst", "transfer", "transfer_write")
    cmd:copy_image(color_img, color_img2)
    T.check("copy_image", true)

    -- Memory barrier (single)
    cmd:memory_barrier("transfer", "transfer_write", "fragment", "shader_read")
    T.check("memory_barrier", true)

    -- Memory barriers (batch)
    cmd:memory_barriers({
        {"transfer", "transfer_write", "compute", "shader_read"},
        {"compute", "shader_write", "fragment", "shader_read"}
    })
    T.check("memory_barriers (batch)", true)

    -- Image barriers (batch)
    cmd:image_barriers({
        {color_img, "shader_read", "fragment", "shader_read"},
        {color_img2, "shader_read", "fragment", "shader_read"}
    })
    T.check("image_barriers (batch)", true)

    -- Create test buffers
    local buf_src = create_buffer(256, "storage|transfer_src|transfer_dst", "host_write|mapped", "test_buf_src")
    local buf_dst = create_buffer(256, "storage|transfer_src|transfer_dst", "host_write|mapped", "test_buf_dst")
    T.check("create test buffers", buf_src ~= nil and buf_dst ~= nil)

    -- Buffer barrier (single)
    cmd:buffer_barrier(buf_src, "transfer", "transfer_read")
    T.check("buffer_barrier", true)

    -- Buffer barriers (batch)
    cmd:buffer_barriers({
        {buf_src, "compute", "shader_read"},
        {buf_dst, "compute", "shader_write"}
    })
    T.check("buffer_barriers (batch)", true)

    -- Copy buffer
    cmd:copy_buffer(buf_src, buf_dst, 128, 0, 0)
    T.check("copy_buffer", true)

    -- Copy buffer staged (zero-fill)
    cmd:memory_barrier("transfer", "transfer_write", "transfer", "transfer_write")
    cmd:copy_buffer_staged(buf_dst, 64, 0)
    T.check("copy_buffer_staged (zero-fill)", true)

    -- Copy buffer staged (with data)
    cmd:memory_barrier("transfer", "transfer_write", "transfer", "transfer_write")
    cmd:copy_buffer_staged(buf_dst, {1.0, 2.0, 3.0, 4.0}, "float", 0)
    T.check("copy_buffer_staged (float data)", true)

    -- Copy data to image staged
    local pixel_data = {}
    for i = 1, 64 * 64 do
        pixel_data[i] = 0xFF0000FF
    end
    cmd:image_barrier(color_img, "transfer_dst", "transfer", "transfer_write")
    cmd:copy_data_to_image_staged(color_img, pixel_data, "uint")
    T.check("copy_data_to_image_staged", true)

    -- Generate mip maps
    cmd:image_barrier(color_img, "transfer_dst", "transfer", "transfer_write")
    cmd:generate_mip_maps(color_img)
    T.check("generate_mip_maps", true)

    -- Viewport / Scissor
    cmd:set_viewport(0, 0, 64, 64)
    T.check("set_viewport", true)

    cmd:set_scissor(0, 0, 64, 64)
    T.check("set_scissor", true)

    -- Dynamic state
    cmd:set_line_width(1.0)
    T.check("set_line_width", true)

    cmd:set_depth_bias(0.0, 0.0, 0.0)
    T.check("set_depth_bias", true)

    cmd:set_depth_test_enable(1)
    T.check("set_depth_test_enable", true)

    cmd:set_depth_write_enable(1)
    T.check("set_depth_write_enable", true)

    -- Push constants
    cmd:set_constant_float(0, 3.14)
    T.check("set_constant_float", true)

    cmd:set_constant_int(1, -42)
    T.check("set_constant_int", true)

    cmd:set_constant_uint(2, 100)
    T.check("set_constant_uint", true)

    cmd:set_constant_vec2(3, 1.0, 2.0)
    T.check("set_constant_vec2", true)

    cmd:set_constant_vec3(5, 1.0, 2.0, 3.0)
    T.check("set_constant_vec3", true)

    cmd:set_constant_vec4(8, 1.0, 2.0, 3.0, 4.0)
    T.check("set_constant_vec4", true)

    cmd:set_constant_mat4(0, mat4(1.0))
    T.check("set_constant_mat4", true)

    -- Debug regions
    cmd:begin_debug_region("test_region")
    T.check("begin_debug_region", true)

    cmd:insert_debug_label("test_label")
    T.check("insert_debug_label", true)

    cmd:end_debug_region()
    T.check("end_debug_region", true)

    -- Set submission
    cmd:set_submission(1)
    T.check("set_submission", true)

    -- After wait callback
    cmd:add_after_wait_callback(function()
        pe_log("[CALLBACK] after_wait_callback fired")
    end)
    T.check("add_after_wait_callback", true)

    -- End recording
    cmd:end_cmd()
    T.check("end_cmd", cmd.is_recording == false)

    -- Submit, wait and return
    queue:submit(cmd)
    cmd:wait()
    T.check("submit + wait", true)

    cmd:return_cmd()
    T.check("return_cmd", true)

    -- Static resource cache
    local rp = cmd_get_render_pass({{color_img, "clear", "store"}})
    T.check("cmd_get_render_pass", rp ~= nil)

    local fb = cmd_get_framebuffer(rp, {{color_img, "clear", "store"}})
    T.check("cmd_get_framebuffer", fb ~= nil)

    -- cmd_clear_cache() -- skipped: destroys engine's own cached pipelines
    T.check("cmd_clear_cache (skipped, destructive)", true)

    -- Cleanup
    destroy_image(color_img)
    destroy_image(color_img2)
    destroy_image(depth_img)
    destroy_buffer(buf_src)
    destroy_buffer(buf_dst)

    T.summary("CommandBuffer API Tests")
end
