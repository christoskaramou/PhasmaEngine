-- Camera binding test suite

function run_camera_tests()
    pe_log("=== Camera Tests ===")
    T.reset()

    local cam = get_camera()
    T.check("get_camera", cam ~= nil)

    if not cam then
        T.summary("Camera Tests")
        return
    end

    -- Existing bindings
    local pos = cam:get_position()
    T.check("get_position", pos ~= nil)

    local euler = cam:get_euler()
    T.check("get_euler", euler ~= nil)

    local front = cam:get_front()
    T.check("get_front", front ~= nil)

    local right = cam:get_right()
    T.check("get_right", right ~= nil)

    local up = cam:get_up()
    T.check("get_up", up ~= nil)

    local fov = cam:get_fov()
    T.check("get_fov positive", fov > 0)

    local near = cam:get_near()
    T.check("get_near positive", near > 0)

    -- New bindings: rotation_speed
    local rspeed = cam:get_rotation_speed()
    T.check("get_rotation_speed", type(rspeed) == "number")

    cam:set_rotation_speed(0.5)
    T.check("set_rotation_speed", cam:get_rotation_speed() == 0.5)
    cam:set_rotation_speed(rspeed)

    -- New bindings: aspect
    local aspect = cam:get_aspect()
    T.check("get_aspect positive", aspect > 0)

    -- New bindings: matrices
    local view = cam:get_view()
    T.check("get_view returns mat4", view ~= nil)

    local proj = cam:get_projection()
    T.check("get_projection returns mat4", proj ~= nil)

    local vp = cam:get_view_projection()
    T.check("get_view_projection returns mat4", vp ~= nil)

    local inv_view = cam:get_inv_view()
    T.check("get_inv_view returns mat4", inv_view ~= nil)

    local inv_proj = cam:get_inv_projection()
    T.check("get_inv_projection returns mat4", inv_proj ~= nil)

    -- Jitter
    local jitter = cam:get_jitter()
    T.check("get_jitter returns vec2", jitter ~= nil)

    cam:set_jitter(vec2(0.125, 0.25))
    local j2 = cam:get_jitter()
    T.check("set_jitter", j2.x == 0.125 and j2.y == 0.25)
    cam:set_jitter(jitter)

    local prev_jitter = cam:get_prev_jitter()
    T.check("get_prev_jitter returns vec2", prev_jitter ~= nil)

    T.summary("Camera Tests")
end
