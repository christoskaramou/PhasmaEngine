-- gamekit/pick.lua — mouse-to-world on a ground plane (pure Lua).
--
-- The engine has no screen->world binding, so gamekit unprojects the cursor with
-- the camera's inverse view-projection and intersects the resulting ray with the
-- horizontal plane Y = plane_y. Returns a world vec3, or nil if the ray is
-- parallel to / points away from the plane. Used for click-to-move / aiming.

local pick = {}

local function window_size()
    local s = engine and engine.get_window_size and engine.get_window_size()
    if not s then return 1.0, 1.0 end
    local w = s.x or s.width or s[1] or 1.0
    local h = s.y or s.height or s[2] or 1.0
    return w, h
end

-- unproject an NDC point at depth z (0=near, 1=far) through inv view-projection
local function unproject(invVP, ndc_x, ndc_y, z)
    local p = invVP * vec4(ndc_x, ndc_y, z, 1.0)
    local w = p.w
    if w == 0.0 then w = 1.0 end
    return vec3(p.x / w, p.y / w, p.z / w)
end

function pick.mouse_ground(plane_y)
    plane_y = plane_y or 0.0
    local cam = get_camera and get_camera() or nil
    if not cam or not cam.get_inv_view_projection then return nil end
    if not (input and input.get_mouse_position) then return nil end

    local mouse = input.get_mouse_position()
    local w, h = window_size()
    local ndc_x = (mouse.x / w) * 2.0 - 1.0
    local ndc_y = 1.0 - (mouse.y / h) * 2.0

    local invVP = cam:get_inv_view_projection()
    local near = unproject(invVP, ndc_x, ndc_y, 0.0)
    local far = unproject(invVP, ndc_x, ndc_y, 1.0)

    local dir = vec3(far.x - near.x, far.y - near.y, far.z - near.z)
    if math.abs(dir.y) < 1e-6 then return nil end -- ray parallel to ground
    local t = (plane_y - near.y) / dir.y
    if t < 0.0 then return nil end -- plane is behind the camera
    return vec3(near.x + dir.x * t, plane_y, near.z + dir.z * t)
end

return pick
