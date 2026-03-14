-- AccelerationStructure API test suite (called from main.lua)

function run_acceleration_structure_tests()
    pe_log("=== AccelerationStructure API Tests ===")
    T.reset()

    -- Create
    local as = create_acceleration_structure("test_as")
    T.check("create_acceleration_structure", as ~= nil)

    -- GetDeviceAddress (0 before building)
    T.check("get_device_address (before build)", as.get_device_address == 0)

    -- Cleanup
    destroy_acceleration_structure(as)

    T.summary("AccelerationStructure API Tests")
end
