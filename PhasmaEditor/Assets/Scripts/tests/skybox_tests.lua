-- Skybox binding test suite

function run_skybox_tests()
    pe_log("=== Skybox Tests ===")
    T.reset()

    -- load with non-existent file (should warn, not crash)
    skybox.load("nonexistent.hdr")
    T.check("load missing file does not crash", true)

    T.summary("Skybox Tests")
end
