-- Shader file binding test suite

function run_shader_file_tests()
    pe_log("=== Shader File Tests ===")
    T.reset()

    -- list shaders
    local files = shaders.list()
    T.check("list returns table", type(files) == "table")
    T.check("list has shaders", #files > 0)

    if #files > 0 then
        T.check("first entry is string", type(files[1]) == "string")
        T.check("first entry is .hlsl", files[1]:match("%.hlsl$") ~= nil)
    end

    -- read shader
    if #files > 0 then
        local source = shaders.read(files[1])
        T.check("read returns string", type(source) == "string")
        T.check("read non-empty", source ~= nil and #source > 0)
    end

    -- read nonexistent
    local bad = shaders.read("nonexistent_shader_12345.hlsl")
    T.check("read nonexistent returns nil", bad == nil)

    T.summary("Shader File Tests")
end
