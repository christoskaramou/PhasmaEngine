-- PhasmaEngine Editor Test Suite
-- 40+ tests covering: primitives, scene, material, camera, lights, settings, math, async, stress
-- Run via: fs.read("Scripts/tests/editor_tests.lua") then execute, or load directly

-- ============================================================
-- Helpers
-- ============================================================
function assert_near(a, b, eps, label)
    eps = eps or 0.001
    if math.abs(a - b) > eps then
        error(label .. ": expected ~" .. tostring(b) .. " got " .. tostring(a))
    end
end

function assert_true(v, label)
    if not v then error(label .. " expected true, got false") end
end

function assert_false(v, label)
    if v then error(label .. " expected false, got true") end
end

function assert_eq(a, b, label)
    if a ~= b then error(label .. ": expected " .. tostring(b) .. " got " .. tostring(a)) end
end

function assert_not_nil(v, label)
    if v == nil then error(label .. " is nil") end
end

local pass_count = 0
local fail_count = 0
local fail_names = {}

local function run_test(name, fn)
    local ok, err = pcall(fn)
    if ok then
        pass_count = pass_count + 1
        pe_log("PASS: " .. name)
    else
        fail_count = fail_count + 1
        table.insert(fail_names, name)
        pe_log("FAIL: " .. name .. ": " .. tostring(err))
    end
end

-- ============================================================
-- Category 1: Primitives
-- ============================================================

run_test("cube_creates_valid_node", function()
    scene.clear()
    local node = primitives.cube(1.0)
    assert_not_nil(node, "node")
    assert_true(node:is_valid(), "is_valid")
    scene.clear()
end)

run_test("all_primitive_types_valid", function()
    scene.clear()
    local nodes = {
        primitives.cube(1.0),
        primitives.sphere(0.5),
        primitives.uv_sphere(0.5, 16, 8),
        primitives.ico_sphere(0.5, 1),
        primitives.plane(5.0, 5.0),
        primitives.grid(5.0, 5.0, 4),
        primitives.cylinder(0.5, 2.0),
        primitives.cone(0.5, 2.0),
        primitives.pyramid(1.0, 1.0),
        primitives.circle(0.75, 24),
        primitives.torus(0.75, 0.18, 24, 8),
        primitives.quad(1.0, 1.0),
    }
    for i, n in ipairs(nodes) do
        assert_true(n:is_valid(), "node " .. i .. " is_valid")
    end
    scene.clear()
end)

run_test("blender_parity_primitives_save_load", function()
    scene.clear()
    local nodes = {
        primitives.uv_sphere(0.5, 16, 8),
        primitives.ico_sphere(0.5, 1),
        primitives.grid(4.0, 4.0, 3),
        primitives.pyramid(1.0, 1.25),
        primitives.circle(0.75, 24),
        primitives.torus(0.75, 0.18, 24, 8),
    }
    local names = {"ParityUVSphere", "ParityIcoSphere", "ParityGrid", "ParityPyramid", "ParityCircle", "ParityTorus"}
    for i, n in ipairs(nodes) do
        n:set_name(names[i])
    end

    scene.save("temp_blender_parity_primitives_test.pescene")
    scene.load("temp_blender_parity_primitives_test.pescene")

    for _, name in ipairs(names) do
        local loaded = scene.find_model(name)
        assert_not_nil(loaded, name .. " loaded")
        assert_true(loaded:is_valid(), name .. " is_valid")
    end
    scene.clear()
end)

run_test("primitive_default_position", function()
    scene.clear()
    local node = primitives.cube()
    local p = node:get_position()
    assert_near(p.x, 0.0, 0.01, "pos.x")
    assert_near(p.y, 0.0, 0.01, "pos.y")
    assert_near(p.z, 0.0, 0.01, "pos.z")
    scene.clear()
end)

run_test("primitive_set_name", function()
    scene.clear()
    local node = primitives.sphere()
    node:set_name("MySphere")
    assert_eq(node:get_name(), "MySphere", "get_name")
    scene.clear()
end)

run_test("primitive_set_position", function()
    scene.clear()
    local node = primitives.cube()
    node:set_position(vec3(3.0, -1.5, 7.0))
    local p = node:get_position()
    assert_near(p.x,  3.0,  0.01, "pos.x")
    assert_near(p.y, -1.5,  0.01, "pos.y")
    assert_near(p.z,  7.0,  0.01, "pos.z")
    scene.clear()
end)

run_test("primitive_set_scale", function()
    scene.clear()
    local node = primitives.cube()
    node:set_scale(vec3(2.0, 3.0, 0.5))
    local s = node:get_scale()
    assert_near(s.x, 2.0, 0.01, "scale.x")
    assert_near(s.y, 3.0, 0.01, "scale.y")
    assert_near(s.z, 0.5, 0.01, "scale.z")
    scene.clear()
end)

run_test("primitive_set_transform", function()
    scene.clear()
    local node = primitives.cube()
    node:set_transform(vec3(1,2,3), vec3(0,45,0), vec3(2,2,2))
    local p = node:get_position()
    local s = node:get_scale()
    assert_near(p.x, 1.0, 0.01, "pos.x")
    assert_near(p.y, 2.0, 0.01, "pos.y")
    assert_near(p.z, 3.0, 0.01, "pos.z")
    assert_near(s.x, 2.0, 0.02, "scale.x")
    assert_near(s.y, 2.0, 0.02, "scale.y")
    assert_near(s.z, 2.0, 0.02, "scale.z")
    scene.clear()
end)

run_test("scene_save_load_preserves_primitive_dimensions_and_scale", function()
    scene.clear()
    local node = primitives.cube(2.0)
    node:set_name("ScalePersistenceCube")
    node:set_scale(vec3(1.5, 1.5, 1.5))
    scene.save("temp_scale_persistence_test.pescene")
    scene.load("temp_scale_persistence_test.pescene")

    local loaded = scene.find_model("ScalePersistenceCube")
    assert_not_nil(loaded, "loaded node")
    local bb = loaded:get_bounding_box()
    local size = bb.size
    assert_near(size.x, 3.0, 0.05, "bb size.x")
    assert_near(size.y, 3.0, 0.05, "bb size.y")
    assert_near(size.z, 3.0, 0.05, "bb size.z")
    scene.clear()
end)

run_test("primitive_bounding_box", function()
    scene.clear()
    local node = primitives.cube(2.0)
    local bb = node:get_bounding_box()
    assert_not_nil(bb, "bounding_box")
    assert_not_nil(bb.min, "bb.min")
    assert_not_nil(bb.max, "bb.max")
    assert_true(bb.max.x > bb.min.x, "max.x > min.x")
    assert_true(bb.max.y > bb.min.y, "max.y > min.y")
    assert_true(bb.max.z > bb.min.z, "max.z > min.z")
    scene.clear()
end)

-- ============================================================
-- Category 2: Scene
-- ============================================================

run_test("scene_clear_removes_entities", function()
    primitives.cube()
    primitives.sphere()
    scene.clear()
    local ents = scene.get_entities()
    assert_eq(#ents, 0, "entity count after clear")
end)

run_test("scene_get_entities_returns_nodes", function()
    scene.clear()
    primitives.cube()
    primitives.sphere()
    primitives.plane()
    local ents = scene.get_entities()
    assert_true(#ents >= 3, "at least 3 entities, got " .. #ents)
    for i, e in ipairs(ents) do
        assert_not_nil(e.node, "ents[" .. i .. "].node")
        assert_not_nil(e.label, "ents[" .. i .. "].label")
    end
    scene.clear()
end)

run_test("scene_find_model_by_name", function()
    scene.clear()
    local node = primitives.cube()
    node:set_name("FindMe")
    local found = scene.find_model("FindMe")
    assert_not_nil(found, "find_model result")
    assert_true(found:is_valid(), "found is_valid")
    assert_eq(found:get_name(), "FindMe", "found name")
    scene.clear()
end)

run_test("scene_find_model_missing_returns_nil", function()
    scene.clear()
    local result = scene.find_model("__does_not_exist_xyz__")
    assert_true(result == nil, "should be nil")
end)

run_test("scene_get_model_count", function()
    scene.clear()
    local before = scene.get_model_count()
    primitives.cube()
    primitives.cube()
    local after = scene.get_model_count()
    assert_true(after >= before + 2, "model count increased by at least 2")
    scene.clear()
end)

run_test("scene_node_invalidates_after_clear", function()
    scene.clear()
    local node = primitives.cube()
    assert_true(node:is_valid(), "valid before clear")
    scene.clear()
    assert_false(node:is_valid(), "invalid after clear")
end)

run_test("selection_select_and_clear", function()
    scene.clear()
    primitives.cube()
    selection.select_node(0)
    local s = selection.get()
    assert_true(s.has_selection, "has_selection after select_node")
    selection.clear()
    s = selection.get()
    assert_false(s.has_selection, "has_selection after clear")
    scene.clear()
end)

run_test("gizmo_mode_round_trip", function()
    for _, mode in ipairs({"translate", "rotate", "scale"}) do
        selection.set_gizmo(mode)
        assert_eq(selection.get_gizmo(), mode, "gizmo mode " .. mode)
    end
end)

-- ============================================================
-- Category 3: Material
-- ============================================================

run_test("material_get_returns_table", function()
    scene.clear()
    local node = primitives.cube()
    local mat = material.get(node)
    assert_not_nil(mat, "material table")
    assert_not_nil(mat.base_color, "base_color")
    assert_not_nil(mat.metallic,   "metallic")
    assert_not_nil(mat.roughness,  "roughness")
    scene.clear()
end)

run_test("material_set_float_metallic", function()
    scene.clear()
    local node = primitives.sphere()
    material.set(node, "metallic", 0.75)
    local mat = material.get(node)
    assert_near(mat.metallic, 0.75, 0.001, "metallic")
    scene.clear()
end)

run_test("material_set_roughness", function()
    scene.clear()
    local node = primitives.cube()
    material.set(node, "roughness", 0.2)
    local mat = material.get(node)
    assert_near(mat.roughness, 0.2, 0.001, "roughness")
    scene.clear()
end)

run_test("material_set_base_color_vec4", function()
    scene.clear()
    local node = primitives.cube()
    material.set(node, "base_color", vec4(1.0, 0.0, 0.0, 1.0))
    local mat = material.get(node)
    assert_near(mat.base_color.x, 1.0, 0.001, "base_color.r")
    assert_near(mat.base_color.y, 0.0, 0.001, "base_color.g")
    assert_near(mat.base_color.z, 0.0, 0.001, "base_color.b")
    scene.clear()
end)

run_test("material_render_type_round_trip", function()
    local types = {"opaque", "alpha_cut", "alpha_blend", "transmission"}
    for _, t in ipairs(types) do
        scene.clear()
        local node = primitives.cube()
        material.set_render_type(node, t)
        assert_eq(material.get_render_type(node), t, "render_type " .. t)
    end
    scene.clear()
end)

run_test("material_has_texture_returns_bool", function()
    scene.clear()
    local node = primitives.sphere()
    local slots = {"base_color","metallic_roughness","normal","occlusion","emissive"}
    for _, s in ipairs(slots) do
        local v = material.has_texture(node, s)
        assert_true(v == true or v == false, "has_texture " .. s .. " is bool")
    end
    scene.clear()
end)

run_test("material_unknown_slot_no_crash", function()
    scene.clear()
    local node = primitives.cube()
    local v = material.has_texture(node, "no_such_slot")
    assert_false(v, "should return false for unknown slot")
    scene.clear()
end)

-- ============================================================
-- Category 4: Camera
-- ============================================================

run_test("active_camera_not_nil", function()
    local cam = scene.get_active_camera()
    assert_not_nil(cam, "active camera")
end)

run_test("camera_position_round_trip", function()
    local cam = scene.get_active_camera()
    local orig = cam:get_position()
    cam:set_position(vec3(10, 5, -3))
    local p = cam:get_position()
    assert_near(p.x,  10.0, 0.01, "pos.x")
    assert_near(p.y,   5.0, 0.01, "pos.y")
    assert_near(p.z,  -3.0, 0.01, "pos.z")
    cam:set_position(orig)
end)

run_test("camera_fov_round_trip", function()
    local cam = scene.get_active_camera()
    local orig = cam:get_fov()
    cam:set_fov(75.0)
    assert_near(cam:get_fov(), 75.0, 0.5, "fov degrees")
    cam:set_fov(orig)
end)

run_test("camera_near_far_round_trip", function()
    local cam = scene.get_active_camera()
    local origNear = cam:get_near()
    local origFar  = cam:get_far()
    cam:set_near(0.05)
    cam:set_far(2000.0)
    assert_near(cam:get_near(), 0.05,   0.001, "near")
    assert_near(cam:get_far(),  2000.0, 1.0,   "far")
    cam:set_near(origNear)
    cam:set_far(origFar)
end)

run_test("camera_look_at", function()
    local cam = scene.get_active_camera()
    -- look_at from a known position toward origin
    cam:set_position(vec3(0, 0, 5))
    cam:look_at(vec3(0, 0, 0))
    local front = cam:get_front()
    -- front should point roughly toward -Z (allow some tolerance for frame-lag)
    assert_true(front.z < 0.1, "front.z should be near-negative after look_at toward origin, got " .. front.z)
end)

run_test("camera_add_remove", function()
    local before = #scene.get_cameras()
    local cam = scene.add_camera()
    assert_not_nil(cam, "new camera")
    local after = #scene.get_cameras()
    assert_eq(after, before + 1, "camera count after add")
    scene.remove_camera(cam)
    assert_eq(#scene.get_cameras(), before, "camera count after remove")
end)

run_test("camera_point_in_frustum", function()
    local cam = scene.get_active_camera()
    local pos = cam:get_position()
    local front = cam:get_front()
    local target = pos + front * 5.0
    assert_true(cam:point_in_frustum(target, 0.0), "point in front is in frustum")
end)

-- ============================================================
-- Category 5: Lights
-- ============================================================

run_test("add_remove_point_light", function()
    scene.clear()
    local c0 = lights.get_counts()
    lights.add_point()
    local c1 = lights.get_counts()
    assert_eq(c1.point, c0.point + 1, "count after add")
    lights.remove_point(c1.point - 1)
    local c2 = lights.get_counts()
    assert_eq(c2.point, c0.point, "count after remove")
end)

run_test("add_all_light_types", function()
    scene.clear()
    local c0 = lights.get_counts()
    lights.add_point()
    lights.add_directional()
    lights.add_spot()
    lights.add_area()
    local c1 = lights.get_counts()
    assert_eq(c1.point,       c0.point + 1,       "point count")
    assert_eq(c1.directional, c0.directional + 1, "directional count")
    assert_eq(c1.spot,        c0.spot + 1,        "spot count")
    assert_eq(c1.area,        c0.area + 1,        "area count")
    lights.remove_point(c1.point - 1)
    lights.remove_directional(c1.directional - 1)
    lights.remove_spot(c1.spot - 1)
    lights.remove_area(c1.area - 1)
end)

run_test("set_point_light_properties", function()
    scene.clear()
    lights.add_point()
    local c = lights.get_counts()
    local idx = c.point - 1
    lights.set_point_light(idx, vec3(1,2,3), vec3(1,0.5,0), 5.0, 10.0)
    local pls = lights.get_point_lights()
    local pl = nil
    for _, l in ipairs(pls) do
        if l.index == idx then pl = l; break end
    end
    assert_not_nil(pl, "point light entry")
    assert_near(pl.position.x, 1.0, 0.01, "pl.pos.x")
    assert_near(pl.position.y, 2.0, 0.01, "pl.pos.y")
    assert_near(pl.position.z, 3.0, 0.01, "pl.pos.z")
    assert_near(pl.intensity,  5.0, 0.01, "pl.intensity")
    assert_near(pl.radius,    10.0, 0.01, "pl.radius")
    lights.remove_point(idx)
end)

run_test("lights_find_by_name", function()
    scene.clear()
    lights.add_point()
    local c = lights.get_counts()
    local idx = c.point - 1
    lights.set_property("point", idx, "name", "TestLight")
    local results = lights.find("TestLight")
    assert_true(#results >= 1, "find returned at least one result")
    local found = false
    for _, r in ipairs(results) do
        if r.name == "TestLight" then found = true end
    end
    assert_true(found, "TestLight found in results")
    lights.remove_point(idx)
end)

run_test("spot_light_angle_falloff", function()
    scene.clear()
    lights.add_spot()
    local c = lights.get_counts()
    local idx = c.spot - 1
    lights.set_spot_light(idx, vec3(0,5,0), vec3(1,1,1), 3.0, 15.0, 30.0, 0.8)
    local sls = lights.get_spot_lights()
    local sl = nil
    for _, l in ipairs(sls) do
        if l.index == idx then sl = l; break end
    end
    assert_not_nil(sl, "spot light entry")
    assert_near(sl.angle,   30.0, 0.1, "angle")
    assert_near(sl.falloff,  0.8, 0.01, "falloff")
    lights.remove_spot(idx)
end)

-- ============================================================
-- Category 6: Settings
-- ============================================================

run_test("bool_setting_round_trip", function()
    local keys = {"shadows", "ssao", "fxaa", "taa", "bloom", "motion_blur", "tonemapping"}
    for _, k in ipairs(keys) do
        local orig = settings.get(k)
        settings.set(k, not orig)
        assert_eq(settings.get(k), not orig, k .. " toggled")
        settings.set(k, orig)
    end
end)

run_test("float_setting_round_trip", function()
    local pairs_list = {
        {"bloom_strength",      0.3},
        {"IBL_intensity",       1.5},
        {"lights_intensity",    2.0},
    }
    for _, kv in ipairs(pairs_list) do
        local k, v = kv[1], kv[2]
        local orig = settings.get(k)
        settings.set(k, v)
        assert_near(settings.get(k), v, 0.001, k)
        settings.set(k, orig)
    end
end)

run_test("render_mode_round_trip", function()
    local orig = settings.get_render_mode()
    local modes = {"raster", "hybrid"}
    for _, m in ipairs(modes) do
        settings.set_render_mode(m)
        assert_eq(settings.get_render_mode(), m, "render_mode " .. m)
    end
    settings.set_render_mode(orig)
end)

run_test("unknown_setting_returns_nil", function()
    local v = settings.get("__no_such_setting__")
    assert_true(v == nil, "unknown setting should be nil")
end)

-- ============================================================
-- Category 7: Math
-- ============================================================

run_test("vec3_arithmetic", function()
    local a = vec3(1, 2, 3)
    local b = vec3(4, 5, 6)
    local s = a + b
    assert_near(s.x, 5, 0.001, "add.x")
    assert_near(s.y, 7, 0.001, "add.y")
    assert_near(s.z, 9, 0.001, "add.z")
    local d = b - a
    assert_near(d.x, 3, 0.001, "sub.x")
    local m = a * 2.0
    assert_near(m.z, 6, 0.001, "mul.z")
end)

run_test("normalize_length", function()
    local v = vec3(3, 4, 0)
    local n = normalize(v)
    assert_near(length(n), 1.0, 0.001, "normalized length")
    assert_near(n.x, 0.6, 0.001, "n.x")
    assert_near(n.y, 0.8, 0.001, "n.y")
end)

run_test("dot_and_cross", function()
    local x = vec3(1,0,0)
    local y = vec3(0,1,0)
    assert_near(dot(x, y), 0.0, 0.001, "dot(x,y)")
    assert_near(dot(x, x), 1.0, 0.001, "dot(x,x)")
    local z = cross(x, y)
    assert_near(z.x, 0.0, 0.001, "cross.x")
    assert_near(z.y, 0.0, 0.001, "cross.y")
    assert_near(z.z, 1.0, 0.001, "cross.z")
end)

run_test("lerp_scalar_and_vec3", function()
    assert_near(lerp(0.0, 10.0, 0.5), 5.0, 0.001, "scalar lerp 0.5")
    assert_near(lerp(0.0, 10.0, 0.0), 0.0, 0.001, "scalar lerp 0.0")
    assert_near(lerp(0.0, 10.0, 1.0), 10.0, 0.001, "scalar lerp 1.0")
    local va = vec3(0,0,0)
    local vb = vec3(2,4,6)
    local vc = lerp(va, vb, 0.5)
    assert_near(vc.x, 1.0, 0.001, "vec3 lerp.x")
    assert_near(vc.y, 2.0, 0.001, "vec3 lerp.y")
    assert_near(vc.z, 3.0, 0.001, "vec3 lerp.z")
end)

run_test("mat4_identity_multiply", function()
    local m = mat4()
    local v = vec4(1, 2, 3, 1)
    local r = m * v
    assert_near(r.x, 1.0, 0.001, "r.x")
    assert_near(r.y, 2.0, 0.001, "r.y")
    assert_near(r.z, 3.0, 0.001, "r.z")
    assert_near(r.w, 1.0, 0.001, "r.w")
end)

run_test("quat_euler_round_trip", function()
    local euler_in = vec3(30, 45, 0)
    local q = quat(euler_in)
    local euler_out = q:to_euler()
    assert_near(euler_out.x, 30.0, 0.5, "pitch")
    assert_near(euler_out.y, 45.0, 0.5, "yaw")
end)

run_test("clamp_and_saturate", function()
    assert_near(clamp(-5.0, 0.0, 1.0), 0.0, 0.001, "clamp low")
    assert_near(clamp(5.0,  0.0, 1.0), 1.0, 0.001, "clamp high")
    assert_near(clamp(0.5,  0.0, 1.0), 0.5, 0.001, "clamp mid")
    assert_near(saturate(-1.0), 0.0, 0.001, "saturate low")
    assert_near(saturate(2.0),  1.0, 0.001, "saturate high")
    assert_near(saturate(0.3),  0.3, 0.001, "saturate mid")
end)

-- ============================================================
-- Category 8: Stress / Integration
-- ============================================================

run_test("spawn_many_primitives", function()
    scene.clear()
    local count = 20
    local nodes = {}
    for i = 1, count do
        local n = primitives.cube()
        n:set_position(vec3(i * 2.0, 0, 0))
        table.insert(nodes, n)
    end
    assert_eq(#nodes, count, "node count")
    for i, n in ipairs(nodes) do
        assert_true(n:is_valid(), "node " .. i .. " valid")
    end
    scene.clear()
end)

run_test("add_remove_many_lights", function()
    scene.clear()
    local before = lights.get_counts().point
    for i = 1, 10 do
        lights.add_point()
    end
    assert_eq(lights.get_counts().point, before + 10, "after adding 10")
    for i = 1, 10 do
        local idx = lights.get_counts().point - 1
        lights.remove_point(idx)
    end
    assert_eq(lights.get_counts().point, before, "after removing 10")
end)

run_test("particles_add_remove_cycle", function()
    particles.clear()
    for i = 1, 5 do
        local idx = particles.add_emitter({ count = 50, position = vec3(i, 0, 0) })
        assert_true(idx >= 0, "emitter index valid iteration " .. i)
    end
    assert_eq(particles.get_count(), 5, "5 emitters present")
    particles.clear()
    assert_eq(particles.get_count(), 0, "0 emitters after clear")
end)

run_test("engine_metrics_sane", function()
    local m = engine.get_metrics()
    assert_not_nil(m.fps,      "fps present")
    assert_not_nil(m.delta_ms, "delta_ms present")
    assert_true(m.fps >= 0.0,      "fps non-negative")
    assert_true(m.delta_ms >= 0.0, "delta_ms non-negative")
end)

run_test("rhi_gpu_info_sane", function()
    local gpu = rhi.get_gpu_name()
    assert_true(type(gpu) == "string" and #gpu > 0, "gpu name non-empty string")
    local w = rhi.get_width()
    local h = rhi.get_height()
    assert_true(w > 0, "width > 0")
    assert_true(h > 0, "height > 0")
    local fc = rhi.get_frame_counter()
    assert_true(fc >= 0, "frame_counter >= 0")
    local mem = rhi.get_gpu_memory()
    assert_not_nil(mem.vram, "vram present")
    assert_true(mem.vram.budget > 0, "vram budget > 0")
end)

run_test("fs_write_read_roundtrip", function()
    local path = "Tests/lua_rw_test.txt"
    local content = "hello_phasma_test_12345"
    local wrote = fs.write(path, content)
    assert_true(wrote, "fs.write succeeded")
    local read = fs.read(path)
    assert_not_nil(read, "fs.read returned content")
    assert_eq(read, content, "content round-trips")
end)

run_test("shaders_list_non_empty", function()
    local list = shaders.list()
    assert_true(#list > 0, "shader list non-empty, got " .. #list)
    for i, s in ipairs(list) do
        assert_true(s:sub(-5) == ".hlsl", "entry " .. i .. " ends in .hlsl: " .. s)
        if i >= 5 then break end
    end
end)

run_test("play_mode_toggle", function()
    local orig = engine.is_play_mode()
    engine.set_play_mode(true)
    assert_true(engine.is_play_mode(), "play mode on")
    engine.set_play_mode(false)
    assert_false(engine.is_play_mode(), "play mode off")
    engine.set_play_mode(orig)
end)

-- ============================================================
-- Summary
-- ============================================================
pe_log("========================================")
pe_log("TEST RESULTS: " .. pass_count .. " passed, " .. fail_count .. " failed")
if fail_count > 0 then
    pe_log("FAILED TESTS:")
    for _, n in ipairs(fail_names) do
        pe_log("  - " .. n)
    end
end
pe_log("========================================")
