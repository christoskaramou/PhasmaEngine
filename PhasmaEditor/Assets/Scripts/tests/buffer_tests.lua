-- Buffer API test suite (called from main.lua)

function run_buffer_tests()
    pe_log("=== Buffer API Tests ===")
    T.reset()

    -- Create buffers
    local buf_uniform = create_buffer(256, "uniform", "host_write|mapped", "test_uniform")
    local buf_storage = create_buffer(1024, "storage|transfer_dst", "dedicated", "test_storage")
    local buf_vertex = create_buffer(128, "vertex", "host_write|mapped", "test_vertex")
    pe_log("Created 3 buffers: uniform=" .. buf_uniform.size .. " storage=" .. buf_storage.size .. " vertex=" .. buf_vertex.size)

    -- Map and zero
    buf_uniform:map()
    buf_uniform:zero()
    T.check_data("zero", buf_uniform:get_data(4, "float", 0), {0.0, 0.0, 0.0, 0.0})

    -- set_data float
    buf_uniform:set_data({1.0, 2.0, 3.0, 4.0}, "float")
    T.check_data("set_data float", buf_uniform:get_data(4, "float", 0), {1.0, 2.0, 3.0, 4.0})

    -- set_data int at offset
    buf_uniform:set_data({10, 20, 30}, "int", 16)
    T.check_data("set_data int", buf_uniform:get_data(3, "int", 16), {10, 20, 30})

    -- set_data uint at offset
    buf_uniform:set_data({100, 200}, "uint", 28)
    T.check_data("set_data uint", buf_uniform:get_data(2, "uint", 28), {100, 200})

    -- set_data vec4
    buf_uniform:set_data({vec4(1.0, 0.0, 0.0, 1.0)}, "vec4", 48)
    T.check_data("set_data vec4", buf_uniform:get_data(4, "float", 48), {1.0, 0.0, 0.0, 1.0})

    -- set_data vec2
    buf_uniform:set_data({vec2(0.5, 0.5)}, "vec2", 64)
    T.check_data("set_data vec2", buf_uniform:get_data(2, "float", 64), {0.5, 0.5})

    -- set_data vec3
    buf_uniform:set_data({vec3(1.0, 2.0, 3.0)}, "vec3", 72)
    T.check_data("set_data vec3", buf_uniform:get_data(3, "float", 72), {1.0, 2.0, 3.0})

    -- set_struct mixed types
    buf_uniform:set_struct({
        {"float", 3.14, 2.71},
        {"int", 42},
        {"uint", 255},
        {"vec4", vec4(0.0, 1.0, 0.0, 1.0)},
    }, 96)
    T.check_data("set_struct floats", buf_uniform:get_data(2, "float", 96), {3.14, 2.71})
    T.check_data("set_struct int", buf_uniform:get_data(1, "int", 104), {42})
    T.check_data("set_struct uint", buf_uniform:get_data(1, "uint", 108), {255})
    T.check_data("set_struct vec4", buf_uniform:get_data(4, "float", 112), {0.0, 1.0, 0.0, 1.0})

    -- set_struct camera UBO (mat4 + vec4)
    buf_uniform:set_struct({
        {"mat4", mat4(1.0)},
        {"vec4", vec4(0.0, 5.0, -10.0, 1.0)},
    }, 128)
    local diag = {
        buf_uniform:get_data(1, "float", 128)[1],
        buf_uniform:get_data(1, "float", 148)[1],
        buf_uniform:get_data(1, "float", 168)[1],
        buf_uniform:get_data(1, "float", 188)[1],
    }
    T.check_data("set_struct mat4 diagonal", diag, {1.0, 1.0, 1.0, 1.0})
    T.check_data("set_struct camera vec4", buf_uniform:get_data(4, "float", 192), {0.0, 5.0, -10.0, 1.0})

    -- Flush
    buf_uniform:flush()
    T.check("flush", true)

    buf_uniform:flush(64, 0)
    T.check("flush (partial)", true)

    -- Vertex buffer triangle
    buf_vertex:map()
    buf_vertex:set_data({
        -0.5, -0.5, 0.0,
         0.5, -0.5, 0.0,
         0.0,  0.5, 0.0,
    }, "float")
    T.check_data("vertex triangle", buf_vertex:get_data(9, "float", 0),
        {-0.5, -0.5, 0.0, 0.5, -0.5, 0.0, 0.0, 0.5, 0.0})
    buf_vertex:flush()

    -- Track info
    local info = buf_uniform:get_track_info()
    pe_log("Track info - offset: " .. info.offset .. " size: " .. info.size)
    T.check("get_track_info", info.size > 0)

    -- Unmap/map cycle
    buf_uniform:unmap()
    buf_uniform:map()
    buf_uniform:unmap()
    T.check("map/unmap cycle", true)

    -- Cleanup
    destroy_buffer(buf_uniform)
    destroy_buffer(buf_storage)
    destroy_buffer(buf_vertex)

    T.summary("Buffer API Tests")
end
