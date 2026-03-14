-- Surface API test suite (called from main.lua)

function run_surface_tests()
    pe_log("=== Surface API Tests ===")
    T.reset()

    local surface = rhi.get_surface()
    T.check("get_surface", surface ~= nil)

    -- GetActualExtent (width/height)
    T.check("get_width", surface.get_width > 0)
    T.check("get_height", surface.get_height > 0)

    -- GetFormat
    T.check("get_format", surface.get_format ~= nil)

    -- GetColorSpace
    T.check("get_color_space", surface.get_color_space ~= nil)

    -- GetPresentMode
    local mode = surface:get_present_mode()
    T.check("get_present_mode", mode ~= nil and #mode > 0)

    -- GetSupportedPresentModes
    local modes = surface:get_supported_present_modes()
    T.check("get_supported_present_modes", modes ~= nil and #modes > 0)

    -- fifo is guaranteed by Vulkan spec
    local has_fifo = false
    for _, m in ipairs(modes) do
        if m == "fifo" then has_fifo = true break end
    end
    T.check("supported modes contains fifo", has_fifo)

    T.summary("Surface API Tests")
end
