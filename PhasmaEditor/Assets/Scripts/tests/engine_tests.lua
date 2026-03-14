-- Engine binding test suite

function run_engine_tests()
    pe_log("=== Engine Tests ===")
    T.reset()

    -- get_metrics
    local m = engine.get_metrics()
    T.check("get_metrics returns table", m ~= nil)
    T.check("get_metrics has fps", type(m.fps) == "number")
    T.check("get_metrics has delta_ms", type(m.delta_ms) == "number")

    -- compile_shaders exists
    T.check("compile_shaders exists", type(engine.compile_shaders) == "function")

    T.summary("Engine Tests")
end
