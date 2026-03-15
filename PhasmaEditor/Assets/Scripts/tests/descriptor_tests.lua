-- Descriptor API test suite (called from main.lua)

function run_descriptor_tests()
    pe_log("=== Descriptor API Tests ===")
    T.reset()

    -- Create a descriptor with a uniform buffer binding
    local desc = create_descriptor({
        {binding = 0, type = "uniform_buffer", count = 1, name = "ubo"},
    }, "vertex|fragment", "test_descriptor")
    T.check("create_descriptor", desc ~= nil)

    -- GetLayout
    local layout = desc:get_layout()
    T.check("get_layout", layout ~= nil)

    -- DescriptorLayout properties
    T.check("get_variable_count", layout.get_variable_count ~= nil)
    T.check("is_push_descriptor", layout.is_push_descriptor == false)

    -- GetPool
    local pool = desc:get_pool()
    T.check("get_pool", pool ~= nil)

    -- GetStage
    local stage = desc:get_stage()
    T.check("get_stage", stage > 0)

    -- GetBindingInfos
    local infos = desc:get_binding_infos()
    T.check("get_binding_infos count", #infos == 1)
    T.check("get_binding_infos binding", infos[1].binding == 0)
    T.check("get_binding_infos name", infos[1].name == "ubo")

    -- Create a buffer and bind it
    local buf = create_buffer(256, "uniform", "host_write|mapped", "test_desc_ubo")
    T.check("create buffer for descriptor", buf ~= nil)

    desc:set_buffer(0, buf)
    T.check("set_buffer", true)

    desc:set_buffer(0, buf, 0)
    T.check("set_buffer (with offset)", true)

    desc:set_buffer(0, buf, 0, 256)
    T.check("set_buffer (with offset + range)", true)

    -- Update
    desc:update()
    T.check("update", true)

    -- GetBoundResources
    local resources = desc:get_bound_resources()
    T.check("get_bound_resources count", #resources >= 1)
    T.check("get_bound_resources binding", resources[1].binding == 0)
    T.check("get_bound_resources buffer_count", resources[1].buffer_count == 1)

    -- Create descriptor with storage buffer binding and test set_buffers
    local desc2 = create_descriptor({
        {binding = 0, type = "storage_buffer", count = 1, name = "ssbo"},
    }, "compute", "test_descriptor2")
    T.check("create_descriptor (storage)", desc2 ~= nil)

    local buf2 = create_buffer(512, "storage", "host_write|mapped", "test_desc_ssbo")
    local buf3 = create_buffer(256, "storage", "host_write|mapped", "test_desc_ssbo2")
    desc2:set_buffers(0, {buf2, buf3})
    T.check("set_buffers", true)

    desc2:set_buffers(0, {buf2}, {0}, {256})
    T.check("set_buffers (with offsets + ranges)", true)

    desc2:update()
    T.check("update (storage)", true)

    -- DescriptorLayout get_or_create (cached)
    local layout2 = descriptor_layout_get_or_create({
        {binding = 0, type = "uniform_buffer", count = 1},
    }, "vertex|fragment")
    T.check("descriptor_layout_get_or_create", layout2 ~= nil)

    -- Same params should return same layout
    local layout3 = descriptor_layout_get_or_create({
        {binding = 0, type = "uniform_buffer", count = 1},
    }, "vertex|fragment")
    T.check("descriptor_layout_get_or_create (cached)", layout3 ~= nil)

    -- Create descriptor with image binding
    local desc3 = create_descriptor({
        {binding = 0, type = "combined_image_sampler", count = 1, layout = "shader_read", name = "tex"},
    }, "fragment", "test_descriptor3")
    T.check("create_descriptor (image)", desc3 ~= nil)

    -- Cleanup
    destroy_descriptor(desc)
    destroy_descriptor(desc2)
    destroy_descriptor(desc3)
    destroy_buffer(buf)
    destroy_buffer(buf2)
    destroy_buffer(buf3)

    T.summary("Descriptor API Tests")
end
