-- Image API test suite (called from main.lua)

function run_image_tests()
    pe_log("=== Image API Tests ===")
    T.reset()

    -- Create test images
    local img = create_image(256, 128, "rgba8", "color_attachment|sampled|transfer_src|transfer_dst", "test_img")
    T.check("create_image", img ~= nil)

    local img_mips = create_image(64, 64, "rgba16f", "sampled|storage|transfer_dst", "test_img_mips", 4)
    T.check("create_image with mips", img_mips ~= nil)

    local img_depth = create_image(128, 128, "d32f", "depth_attachment|sampled", "test_depth_img")
    T.check("create_image depth", img_depth ~= nil)

    -- get_width
    T.check("get_width", img.get_width == 256)

    -- get_height
    T.check("get_height", img.get_height == 128)

    -- get_width_f
    T.check("get_width_f", img.get_width_f == 256.0)

    -- get_height_f
    T.check("get_height_f", img.get_height_f == 128.0)

    -- get_name
    T.check("get_name", img.get_name == "test_img")

    -- get_format
    T.check("get_format", img.get_format == "rgba8")
    T.check("get_format depth", img_depth.get_format == "d32f")

    -- get_mip_levels
    T.check("get_mip_levels (default)", img.get_mip_levels == 1)
    T.check("get_mip_levels (4)", img_mips.get_mip_levels == 4)

    -- get_array_layers
    T.check("get_array_layers", img.get_array_layers == 1)

    -- get_samples
    T.check("get_samples", img.get_samples == 1)

    -- has_generated_mips
    T.check("has_generated_mips", img.has_generated_mips == false)

    -- get_clear_color (default is 0,0,0,1)
    local r, g, b, a = img:get_clear_color()
    T.check("get_clear_color r", r == 0.0)
    T.check("get_clear_color g", g == 0.0)
    T.check("get_clear_color b", b == 0.0)
    T.check("get_clear_color a", a == 1.0)

    -- set_clear_color
    img:set_clear_color(1.0, 0.5, 0.25, 0.75)
    local r2, g2, b2, a2 = img:get_clear_color()
    T.check("set_clear_color r", r2 == 1.0)
    T.check("set_clear_color g", g2 == 0.5)
    T.check("set_clear_color b", b2 == 0.25)
    T.check("set_clear_color a", a2 == 0.75)

    -- get_current_info
    local info = img:get_current_info()
    T.check("get_current_info", info ~= nil)
    T.check("get_current_info base_array_layer", info.base_array_layer ~= nil)

    local info_mip = img:get_current_info(0, 0)
    T.check("get_current_info with layer/mip", info_mip ~= nil)

    -- CreateRTV / HasRTV / GetRTV
    T.check("has_rtv (before)", img:has_rtv() == false)
    img:create_rtv()
    T.check("has_rtv (after)", img:has_rtv() == true)
    local rtv = img:get_rtv()
    T.check("get_rtv", rtv ~= nil)

    -- CreateSRV / HasSRV / GetSRV
    T.check("has_srv (before)", img:has_srv() == false)
    img:create_srv("2d")
    T.check("has_srv (after)", img:has_srv() == true)
    local srv = img:get_srv()
    T.check("get_srv", srv ~= nil)

    -- CreateSRV with mip / HasSRV with mip / GetSRV with mip
    img:create_srv("2d", 0)
    T.check("has_srv mip 0", img:has_srv(0) == true)
    local srv_mip = img:get_srv(0)
    T.check("get_srv mip 0", srv_mip ~= nil)

    -- CreateUAV / HasUAV / GetUAV (needs storage usage)
    img_mips:create_uav("2d", 0)
    T.check("has_uav mip 0", img_mips:has_uav(0) == true)
    local uav = img_mips:get_uav(0)
    T.check("get_uav mip 0", uav ~= nil)

    -- CalculateMips (static)
    T.check("calculate_mips 256x256", calculate_mips(256, 256) == 9)
    T.check("calculate_mips 1x1", calculate_mips(1, 1) == 1)
    T.check("calculate_mips 512x256", calculate_mips(512, 256) == 10)

    -- Cleanup
    destroy_image(img)
    destroy_image(img_mips)
    destroy_image(img_depth)

    T.summary("Image API Tests")
end
