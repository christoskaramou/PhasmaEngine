-- Framebuffer API test suite (called from main.lua)

function run_framebuffer_tests()
    pe_log("=== Framebuffer API Tests ===")
    T.reset()

    -- Create a color image and get a framebuffer via the resource cache
    local img = create_image(128, 64, "rgba8", "color_attachment|sampled", "test_fb_img")
    T.check("create test image", img ~= nil)

    local rp = cmd_get_render_pass({{img, "clear", "store"}})
    T.check("cmd_get_render_pass", rp ~= nil)

    local fb = cmd_get_framebuffer(rp, {{img, "clear", "store"}})
    T.check("cmd_get_framebuffer", fb ~= nil)

    -- get_width
    T.check("get_width", fb.get_width == 128)

    -- get_height
    T.check("get_height", fb.get_height == 64)

    -- get_size
    local w, h = fb:get_size()
    T.check("get_size width", w == 128)
    T.check("get_size height", h == 64)

    -- Cleanup
    T.check("destroy_image", img ~= nil)
    destroy_image(img)

    T.summary("Framebuffer API Tests")
end
