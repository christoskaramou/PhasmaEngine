-- Shader API test suite (called from main.lua)

function run_shader_tests()
    pe_log("=== Shader API Tests ===")
    T.reset()

    -- create_shader
    local vs = create_shader("Shaders/Common/Quad.hlsl", "vertex", "mainVS")
    T.check("create_shader (vertex)", vs ~= nil)

    local fs = create_shader("Shaders/LuaTest/Grayscale.hlsl", "fragment", "mainPS")
    T.check("create_shader (fragment)", fs ~= nil)

    -- get_entry_name
    T.check("get_entry_name (vs)", vs.get_entry_name == "mainVS")
    T.check("get_entry_name (fs)", fs.get_entry_name == "mainPS")

    -- get_shader_stage
    T.check("get_shader_stage (vs)", vs:get_shader_stage() == "vertex")
    T.check("get_shader_stage (fs)", fs:get_shader_stage() == "fragment")

    -- get_size (SPIR-V uint32 count, should be > 0)
    T.check("get_size (vs)", vs.get_size > 0)
    T.check("get_size (fs)", fs.get_size > 0)

    -- get_bytes_count (should be size * 4)
    T.check("get_bytes_count (vs)", vs.get_bytes_count == vs.get_size * 4)

    -- get_path_id (returned as string)
    T.check("get_path_id (vs)", type(vs.get_path_id) == "string" and #vs.get_path_id > 0)

    -- get_local_defines
    local defines = vs:get_local_defines()
    T.check("get_local_defines", defines ~= nil)

    -- create_shader with invalid stage
    local bad = create_shader("Shaders/Common/Quad.hlsl", "invalid_stage", "mainVS")
    T.check("create_shader (invalid stage)", bad == nil)

    -- Cleanup
    destroy_shader(vs)
    destroy_shader(fs)

    T.summary("Shader API Tests")
end
