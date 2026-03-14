-- Semaphore API test suite (called from main.lua)

function run_semaphore_tests()
    pe_log("=== Semaphore API Tests ===")
    T.reset()

    -- Create binary semaphore
    local bin = create_semaphore(false, "test_binary_sem")
    T.check("create_semaphore (binary)", bin ~= nil)

    -- IsTimeline
    T.check("is_timeline (binary)", bin.is_timeline == false)

    -- Create timeline semaphore
    local tl = create_semaphore(true, "test_timeline_sem")
    T.check("create_semaphore (timeline)", tl ~= nil)
    T.check("is_timeline (timeline)", tl.is_timeline == true)

    -- GetValue (timeline, initial 0)
    T.check("get_value (initial)", tl.get_value == 0)

    -- Signal
    tl:signal(1)
    T.check("signal", true)

    -- Wait
    tl:wait(1)
    T.check("wait", true)

    -- GetValue (after signal)
    T.check("get_value (after signal)", tl.get_value == 1)

    -- SetStageFlags
    tl:set_stage_flags("fragment")
    T.check("set_stage_flags", true)

    -- AddStageFlags
    tl:add_stage_flags("compute")
    T.check("add_stage_flags", true)

    -- GetStageFlags
    local flags = tl:get_stage_flags()
    T.check("get_stage_flags", flags ~= nil and flags > 0)

    -- Cleanup
    destroy_semaphore(bin)
    destroy_semaphore(tl)

    T.summary("Semaphore API Tests")
end
