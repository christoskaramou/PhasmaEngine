-- Skybox binding test suite

function run_skybox_tests()
    pe_log("=== Skybox Tests ===")
    T.reset()

    -- is_day
    local was_day = skybox.is_day()
    T.check("is_day returns bool", type(was_day) == "boolean")

    -- set_time
    skybox.set_time("night")
    T.check("set_time night", skybox.is_day() == false)

    skybox.set_time("day")
    T.check("set_time day", skybox.is_day() == true)

    -- restore original
    if not was_day then
        skybox.set_time("night")
    end

    -- load with non-existent file (should warn, not crash)
    skybox.load("nonexistent.hdr")
    T.check("load missing file does not crash", true)

    T.summary("Skybox Tests")
end
