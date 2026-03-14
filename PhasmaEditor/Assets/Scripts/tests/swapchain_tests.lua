-- Swapchain API test suite (called from main.lua)

function run_swapchain_tests()
    pe_log("=== Swapchain API Tests ===")
    T.reset()

    local swapchain = rhi.get_swapchain()
    T.check("get_swapchain", swapchain ~= nil)

    -- GetImageCount
    T.check("get_image_count", swapchain.get_image_count >= 2)

    -- GetImage
    local img = swapchain:get_image(0)
    T.check("get_image(0)", img ~= nil)

    T.summary("Swapchain API Tests")
end
