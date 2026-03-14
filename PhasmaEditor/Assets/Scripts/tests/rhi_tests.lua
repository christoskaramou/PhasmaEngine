-- RHI API test suite (called from main.lua)

function run_rhi_tests()
    pe_log("=== RHI API Tests ===")
    T.reset()

    -- IsInstanceExtensionValid
    T.check("is_instance_extension_valid (VK_KHR_surface)", rhi.is_instance_extension_valid("VK_KHR_surface") == true)
    T.check("is_instance_extension_valid (invalid)", rhi.is_instance_extension_valid("VK_FAKE_extension") == false)

    -- IsDeviceExtensionValid
    T.check("is_device_extension_valid (VK_KHR_swapchain)", rhi.is_device_extension_valid("VK_KHR_swapchain") == true)
    T.check("is_device_extension_valid (invalid)", rhi.is_device_extension_valid("VK_FAKE_extension") == false)

    -- GetDepthFormat
    local df = rhi.get_depth_format()
    T.check("get_depth_format", df ~= nil and df ~= 0)

    -- GetFrameCounter
    local fc = rhi.get_frame_counter()
    T.check("get_frame_counter", fc ~= nil)

    -- GetFrameIndex
    local fi = rhi.get_frame_index()
    T.check("get_frame_index", fi ~= nil)

    -- GetMaxUniformBufferSize
    local mubs = rhi.get_max_uniform_buffer_size()
    T.check("get_max_uniform_buffer_size", mubs > 0)

    -- GetMaxStorageBufferSize
    local msbs = rhi.get_max_storage_buffer_size()
    T.check("get_max_storage_buffer_size", msbs > 0)

    -- GetMaxDrawIndirectCount
    local mdic = rhi.get_max_draw_indirect_count()
    T.check("get_max_draw_indirect_count", mdic > 0)

    -- GetMaxPushConstantsSize
    local mpcs = rhi.get_max_push_constants_size()
    T.check("get_max_push_constants_size", mpcs > 0)

    -- Align
    T.check("align (64, 256)", rhi.align(64, 256) == 256)
    T.check("align (257, 256)", rhi.align(257, 256) == 512)

    -- AlignUniform
    local au = rhi.align_uniform(100)
    T.check("align_uniform", au >= 100)

    -- AlignStorage
    local as = rhi.align_storage(100)
    T.check("align_storage", as >= 100)

    -- AlignStorageAs
    local asa = rhi.align_storage_as(100, 64)
    T.check("align_storage_as", asa >= 100)

    -- GetGpuName
    local name = rhi.get_gpu_name()
    T.check("get_gpu_name", name ~= nil and #name > 0)

    -- GetDescriptorPool
    local pool = rhi.get_descriptor_pool()
    T.check("get_descriptor_pool", pool ~= nil)

    -- GetMainQueue
    local queue = rhi.get_main_queue()
    T.check("get_main_queue", queue ~= nil)

    -- GetSurface
    local surface = rhi.get_surface()
    T.check("get_surface", surface ~= nil)

    -- GetSwapchain
    local swapchain = rhi.get_swapchain()
    T.check("get_swapchain", swapchain ~= nil)

    -- GetSwapchainImageCount
    local sic = rhi.get_swapchain_image_count()
    T.check("get_swapchain_image_count", sic >= 2)

    -- GetSystemAndProcessMemory
    local sysmem = rhi.get_system_memory()
    T.check("get_system_memory", sysmem ~= nil)
    T.check("get_system_memory sys_total", sysmem.sys_total > 0)
    T.check("get_system_memory proc_working_set", sysmem.proc_working_set > 0)

    -- GetGpuMemorySnapshot
    local gpumem = rhi.get_gpu_memory()
    T.check("get_gpu_memory", gpumem ~= nil)
    T.check("get_gpu_memory vram", gpumem.vram ~= nil)
    T.check("get_gpu_memory vram size", gpumem.vram.size > 0)

    -- GetWidth / GetHeight
    local w = rhi.get_width()
    local h = rhi.get_height()
    T.check("get_width", w > 0)
    T.check("get_height", h > 0)

    -- GetWidthf / GetHeightf
    local wf = rhi.get_width_f()
    local hf = rhi.get_height_f()
    T.check("get_width_f", wf > 0.0)
    T.check("get_height_f", hf > 0.0)

    T.summary("RHI API Tests")
end
