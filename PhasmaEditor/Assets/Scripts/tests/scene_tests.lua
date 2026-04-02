-- Scene binding test suite

function run_scene_tests()
    pe_log("=== Scene Tests ===")
    T.reset()

    -- get_model_count
    local count = scene.get_model_count()
    T.check("get_model_count returns number", type(count) == "number")

    -- get_active_camera
    local cam = scene.get_active_camera()
    T.check("get_active_camera", cam ~= nil)

    -- get_cameras
    local cameras = scene.get_cameras()
    T.check("get_cameras returns table", type(cameras) == "table")
    T.check("get_cameras has active camera", #cameras >= 1)

    -- add_camera
    local initial_count = #cameras
    local new_cam = scene.add_camera()
    T.check("add_camera returns camera", new_cam ~= nil)

    if new_cam then
        new_cam:set_name("test_camera")
        T.check("new camera name", new_cam:get_name() == "test_camera")

        local cameras2 = scene.get_cameras()
        T.check("add_camera increases count", #cameras2 == initial_count + 1)

        -- set_active_camera
        scene.set_active_camera(new_cam)
        local active = scene.get_active_camera()
        T.check("set_active_camera", active:get_name() == "test_camera")

        -- restore original active camera and remove test camera
        scene.set_active_camera(cam)
        scene.remove_camera(new_cam)

        local cameras3 = scene.get_cameras()
        T.check("remove_camera restores count", #cameras3 == initial_count)
    end

    -- model count changes with add
    local cube = primitives.cube(1.0)
    if cube then
        local count2 = scene.get_model_count()
        T.check("model count increased", count2 == count + 1)
        cube:remove()
    end

    -- legacy global functions still work
    T.check("save_scene exists", type(save_scene) == "function")

    T.summary("Scene Tests")
end
