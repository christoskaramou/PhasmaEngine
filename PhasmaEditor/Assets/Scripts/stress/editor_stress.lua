-- Heavy PhasmaEditor stress scenario driven by tools/editor_stress.py.
-- Keep the expensive work batched: execute_lua has a short main-thread wait.

editor_stress = editor_stress or {}

local M = editor_stress

local primitive_fns = {
    function() return primitives.cube(1.0) end,
    function() return primitives.sphere(0.55) end,
    function() return primitives.cylinder(0.45, 1.4) end,
    function() return primitives.cone(0.5, 1.5) end,
    function() return primitives.quad(1.2, 1.2) end,
    function() return primitives.plane(1.6, 1.6) end,
}

local render_types = { "opaque", "alpha_cut", "alpha_blend", "transmission" }

local function bool_opt(opts, name, fallback)
    local value = opts[name]
    if value == nil then return fallback end
    return value == true
end

local function num_opt(opts, name, fallback)
    local value = opts[name]
    if type(value) == "number" then return value end
    return fallback
end

local function str_opt(opts, name, fallback)
    local value = opts[name]
    if type(value) == "string" then return value end
    return fallback
end

local function clamp01(x)
    if x < 0.0 then return 0.0 end
    if x > 1.0 then return 1.0 end
    return x
end

local function color_for(i)
    local r = clamp01(((i * 37) % 255) / 255.0)
    local g = clamp01(((i * 67) % 255) / 255.0)
    local b = clamp01(((i * 97) % 255) / 255.0)
    return vec4(r, g, b, 1.0)
end

local function safe_setting(name, value)
    if settings and settings.set then
        settings.set(name, value)
    end
end

local node_script_source = [=[
local props = exposed {
    spin = 38.0,
    bob = 0.22,
    drift = 0.10
}

local base = nil
local phase = 0.0

hooks {
    init = function()
        base = transform:get_position()
        phase = base.x * 0.173 + base.z * 0.097
    end,

    update_editor = function()
        if not base then base = transform:get_position() end
        local t = os.clock() + phase
        local p = vec3(
            base.x + math.sin(t * 0.70) * props.drift,
            base.y + math.sin(t * 1.80) * props.bob,
            base.z + math.cos(t * 0.65) * props.drift)
        transform:set_position(p)
        transform:set_rotation(vec3(t * props.spin, t * props.spin * 0.71, t * props.spin * 1.23))
    end,
}
]=]

local function write_node_script()
    local relative = "Scripts/stress/editor_stress_node.lua"
    if fs and fs.read and fs.write then
        local existing = fs.read(relative)
        if existing ~= node_script_source then
            fs.write(relative, node_script_source, false)
        end
    end
    return assets_path .. relative
end

local function node_script_path(opts)
    if num_opt(opts, "scripted_nodes", 0) <= 0 then
        return ""
    end
    return write_node_script()
end

local function strip_scene_prefix(path)
    local p = path:gsub("\\", "/")
    if p:sub(1, 7) == "Assets/" then
        p = p:sub(8)
    end
    if p:sub(1, 7) == "Scenes/" then
        p = p:sub(8)
    end
    return p
end

local function load_scene_compat(scene_path)
    local candidates = {}
    local function add(candidate)
        if candidate and #candidate > 0 then
            for _, existing in ipairs(candidates) do
                if existing == candidate then return end
            end
            table.insert(candidates, candidate)
        end
    end

    if not scene_path or #scene_path == 0 then
        scene.clear()
        pe_log("[editor_stress] generated-only scene")
        return ""
    end

    add(scene_path)
    add(strip_scene_prefix(scene_path))

    scene.clear()
    for _, candidate in ipairs(candidates) do
        pe_log("[editor_stress] scene.load(" .. candidate .. ")")
        scene.load(candidate)
        local count = scene.get_model_count()
        if count > 0 then
            pe_log(string.format("[editor_stress] scene ready path=%s models=%d", candidate, count))
            return candidate
        end
    end

    pe_log("[editor_stress] warning: scene load produced no models; continuing with generated content")
    return ""
end

local function set_node_material(node, i)
    if not node or not node:is_valid() then return end
    local rt = render_types[((i - 1) % #render_types) + 1]
    material.set_render_type(node, rt)
    material.set(node, "base_color", color_for(i))
    material.set(node, "roughness", 0.08 + ((i * 11) % 90) / 100.0)
    material.set(node, "metallic", ((i * 5) % 100) / 100.0)

    if rt == "alpha_cut" then
        material.set(node, "alpha_cutoff", 0.30 + ((i * 13) % 50) / 100.0)
    elseif rt == "alpha_blend" then
        material.set(node, "base_color", vec4(color_for(i).x, color_for(i).y, color_for(i).z, 0.42))
    elseif rt == "transmission" then
        material.set(node, "transmission", 0.75)
        material.set(node, "ior", 1.25 + ((i * 7) % 45) / 100.0)
        material.set(node, "thickness_factor", 0.35 + ((i * 3) % 75) / 100.0)
        material.set(node, "attenuation_distance", 2.0 + ((i * 17) % 120) / 10.0)
    end

    if (i % 9) == 0 then
        material.set(node, "emissive", vec3(0.2 + color_for(i).x, 0.1 + color_for(i).y, 0.1 + color_for(i).z))
    end
end

function M.begin(opts)
    opts = opts or {}
    M.state = {
        opts = opts,
        nodes = {},
        scripted_nodes = {},
        created_primitives = 0,
        point_lights = 0,
        spot_lights = 0,
        area_lights = 0,
        directional_lights = 0,
        emitters = 0,
        cameras = 0,
        root = nil,
        node_script = node_script_path(opts),
    }

    rhi.change_present_mode("immediate")

    if bool_opt(opts, "load_scene", false) then
        M.state.loaded_scene = load_scene_compat(str_opt(opts, "scene", ""))
    else
        scene.clear()
    end

    -- Loading or clearing a scene can reset script globals; keep the driver table reachable for later MCP batches.
    editor_stress = M

    safe_setting("shadows", true)
    safe_setting("ssao", true)
    safe_setting("fxaa", true)
    safe_setting("taa", true)
    safe_setting("ssr", true)
    safe_setting("dof", true)
    safe_setting("bloom", true)
    safe_setting("motion_blur", true)
    safe_setting("tonemapping", true)
    safe_setting("IBL", true)
    safe_setting("cas_sharpening", true)
    safe_setting("draw_grid", true)
    safe_setting("draw_aabbs", bool_opt(opts, "draw_aabbs", true))
    safe_setting("aabbs_depth_aware", true)
    safe_setting("frustum_culling", true)
    safe_setting("freeze_frustum_culling", false)
    safe_setting("randomize_lights", false)
    safe_setting("render_scale", num_opt(opts, "render_scale", 1.0))
    safe_setting("lights_intensity", num_opt(opts, "lights_intensity", 2.5))
    safe_setting("shadow_map_size", num_opt(opts, "shadow_map_size", 2048))
    safe_setting("num_cascades", num_opt(opts, "num_cascades", 4))
    safe_setting("shadow_distance", num_opt(opts, "shadow_distance", 140.0))
    safe_setting("shadow_filter_radius", num_opt(opts, "shadow_filter_radius", 2.0))

    if settings and settings.is_ray_tracing_supported and settings.is_ray_tracing_supported() then
        settings.set_render_mode(str_opt(opts, "render_mode", "hybrid"))
    end

    if particles and particles.clear then
        particles.clear()
    end

    M.state.root = scene.add_empty_node("PE_STRESS_ROOT")
    if M.state.root and M.state.root:is_valid() then
        M.state.root:set_position(vec3(0.0, 0.0, 0.0))
    end

    M.sample("begin")
    return "begin"
end

function M.spawn_primitives(batch_count)
    local st = M.state
    local opts = st.opts or {}
    local total = num_opt(opts, "primitive_count", 0)
    local scripted_total = num_opt(opts, "scripted_nodes", 0)
    local start_index = st.created_primitives + 1
    local end_index = math.min(total, st.created_primitives + batch_count)
    local grid = math.max(1, math.ceil(math.sqrt(total)))
    local spacing = num_opt(opts, "primitive_spacing", 2.15)
    local height_step = num_opt(opts, "height_step", 0.34)

    for i = start_index, end_index do
        local fn = primitive_fns[((i - 1) % #primitive_fns) + 1]
        local node = fn()
        if node and node:is_valid() then
            local col = (i - 1) % grid
            local row = math.floor((i - 1) / grid)
            local x = (col - grid * 0.5) * spacing
            local z = (row - grid * 0.5) * spacing
            local y = ((i * 17) % 23) * height_step
            local s = 0.55 + ((i * 19) % 90) / 100.0
            node:set_name(string.format("PE_STRESS_%05d", i))
            node:set_transform(vec3(x, y, z), vec3((i * 13) % 360, (i * 29) % 360, (i * 7) % 360), vec3(s, s, s))
            if st.root and st.root:is_valid() then
                node:set_parent(st.root)
            end
            set_node_material(node, i)
            table.insert(st.nodes, node)
            if i <= scripted_total and st.node_script ~= "" then
                node:set_script(st.node_script)
                table.insert(st.scripted_nodes, node)
            end
        end
        st.created_primitives = i
    end

    pe_log(string.format("[editor_stress] primitives %d/%d scripted=%d", st.created_primitives, total, #st.scripted_nodes))
    return st.created_primitives
end

function M.spawn_point_lights(batch_count)
    local st = M.state
    local total = num_opt(st.opts or {}, "point_lights", 0)
    local start_count = lights.get_counts().point
    local already = st.point_lights
    local target = math.min(total, st.point_lights + batch_count)
    for i = st.point_lights + 1, target do
        lights.add_point()
        local idx = start_count + (i - already) - 1
        local angle = i * 0.37
        local radius = 14.0 + (i % 41)
        local pos = vec3(math.cos(angle) * radius, 2.0 + (i % 19) * 0.45, math.sin(angle) * radius)
        local color = vec3(0.25 + ((i * 17) % 75) / 100.0, 0.25 + ((i * 31) % 75) / 100.0, 0.25 + ((i * 43) % 75) / 100.0)
        lights.set_point_light(idx, pos, color, 1.5 + (i % 9) * 0.5, 8.0 + (i % 17))
        lights.set_property("point", idx, "name", string.format("PE_STRESS_POINT_%04d", i))
        st.point_lights = i
    end
    pe_log(string.format("[editor_stress] point_lights %d/%d", st.point_lights, total))
    return st.point_lights
end

function M.spawn_spot_lights(batch_count)
    local st = M.state
    local total = num_opt(st.opts or {}, "spot_lights", 0)
    local start_count = lights.get_counts().spot
    local already = st.spot_lights
    local target = math.min(total, st.spot_lights + batch_count)
    for i = st.spot_lights + 1, target do
        lights.add_spot()
        local idx = start_count + (i - already) - 1
        local angle = i * 0.51
        local radius = 18.0 + (i % 47)
        lights.set_spot_light(
            idx,
            vec3(math.cos(angle) * radius, 5.0 + (i % 13), math.sin(angle) * radius),
            vec3(0.35 + ((i * 11) % 65) / 100.0, 0.35 + ((i * 23) % 65) / 100.0, 0.35 + ((i * 37) % 65) / 100.0),
            3.0 + (i % 11),
            18.0 + (i % 23),
            22.0 + (i % 28),
            0.45 + ((i * 7) % 45) / 100.0)
        lights.set_property("spot", idx, "name", string.format("PE_STRESS_SPOT_%04d", i))
        st.spot_lights = i
    end
    pe_log(string.format("[editor_stress] spot_lights %d/%d", st.spot_lights, total))
    return st.spot_lights
end

function M.spawn_area_lights(batch_count)
    local st = M.state
    local total = num_opt(st.opts or {}, "area_lights", 0)
    local start_count = lights.get_counts().area
    local already = st.area_lights
    local target = math.min(total, st.area_lights + batch_count)
    for i = st.area_lights + 1, target do
        lights.add_area()
        local idx = start_count + (i - already) - 1
        local angle = i * 0.73
        local radius = 12.0 + (i % 33)
        lights.set_area_light(
            idx,
            vec3(math.cos(angle) * radius, 6.0 + (i % 9), math.sin(angle) * radius),
            vec3(0.45 + ((i * 13) % 55) / 100.0, 0.45 + ((i * 29) % 55) / 100.0, 0.45 + ((i * 41) % 55) / 100.0),
            2.0 + (i % 8),
            18.0 + (i % 31),
            1.0 + (i % 7),
            1.0 + ((i * 3) % 9))
        lights.set_property("area", idx, "name", string.format("PE_STRESS_AREA_%04d", i))
        st.area_lights = i
    end
    pe_log(string.format("[editor_stress] area_lights %d/%d", st.area_lights, total))
    return st.area_lights
end

function M.spawn_directional_lights(batch_count)
    local st = M.state
    local total = num_opt(st.opts or {}, "directional_lights", 0)
    local start_count = lights.get_counts().directional
    local already = st.directional_lights
    local target = math.min(total, st.directional_lights + batch_count)
    for i = st.directional_lights + 1, target do
        lights.add_directional()
        local idx = start_count + (i - already) - 1
        lights.set_directional_light(idx, vec3(i * 3.0, 12.0, i * -2.0), vec3(0.7, 0.82, 1.0), 0.35 + (i % 5) * 0.2)
        lights.set_property("directional", idx, "name", string.format("PE_STRESS_DIR_%03d", i))
        st.directional_lights = i
    end
    pe_log(string.format("[editor_stress] directional_lights %d/%d", st.directional_lights, total))
    return st.directional_lights
end

function M.spawn_particles(batch_count)
    local st = M.state
    local total = num_opt(st.opts or {}, "emitters", 0)
    local per_emitter = num_opt(st.opts or {}, "particles_per_emitter", 1024)
    local target = math.min(total, st.emitters + batch_count)
    for i = st.emitters + 1, target do
        local angle = i * 0.61
        local radius = 18.0 + (i % 27)
        local idx = particles.add_emitter({
            position = vec3(math.cos(angle) * radius, 1.5 + (i % 8), math.sin(angle) * radius),
            velocity = vec3(math.sin(angle) * 2.0, 5.0 + (i % 5), math.cos(angle) * 2.0),
            gravity = vec3(0.0, -4.0 - (i % 6), 0.0),
            color_start = vec4(color_for(i).x, color_for(i).y, color_for(i).z, 1.0),
            color_end = vec4(color_for(i + 17).x, color_for(i + 17).y, color_for(i + 17).z, 0.0),
            count = per_emitter,
            spawn_rate = 400.0 + (i % 13) * 80.0,
            spawn_radius = 2.0 + (i % 7) * 0.4,
            noise_strength = 1.0 + (i % 9) * 0.25,
            drag = 0.03 + (i % 8) * 0.03,
            size_min = 0.04,
            size_max = 0.32 + (i % 5) * 0.08,
            life_min = 0.7,
            life_max = 3.4 + (i % 7) * 0.5,
            anim_rows = 5,
            anim_cols = 5,
            anim_speed = 8.0 + (i % 11),
            interpolate = true,
            orientation = (i % 2 == 0) and "velocity" or "billboard",
        })
        if idx >= 0 then
            particles.set_emitter(idx, "name", string.format("PE_STRESS_EMITTER_%03d", i))
        end
        st.emitters = i
    end
    pe_log(string.format("[editor_stress] emitters %d/%d particles=%d", st.emitters, total, particles.get_particle_count()))
    return st.emitters
end

function M.spawn_cameras()
    local st = M.state
    local total = num_opt(st.opts or {}, "cameras", 0)
    for i = st.cameras + 1, total do
        local cam = scene.add_camera()
        if cam then
            local angle = i * 0.55
            local radius = 42.0 + (i % 7) * 8.0
            cam:set_name(string.format("PE_STRESS_CAMERA_%02d", i))
            cam:set_position(vec3(math.cos(angle) * radius, 16.0 + (i % 5) * 3.0, math.sin(angle) * radius))
            cam:look_at(vec3(0.0, 4.0, 0.0))
            cam:set_far(4000.0)
            if bool_opt(st.opts or {}, "activate_stress_camera", false) and i == total then
                scene.set_active_camera(cam)
            end
        end
        st.cameras = i
    end
    pe_log(string.format("[editor_stress] cameras %d/%d", st.cameras, total))
    return st.cameras
end

function M.step(phase)
    local st = M.state
    local opts = st.opts or {}
    local nodes = st.nodes or {}
    local stride = math.max(1, num_opt(opts, "mutation_stride", 1))
    local t = phase * 0.71

    for i = 1, #nodes, stride do
        local node = nodes[i]
        if node and node:is_valid() then
            local p = node:get_position()
            local y = p.y + math.sin(t + i * 0.019) * 0.18
            node:set_position(vec3(p.x, y, p.z))
            node:set_rotation(vec3((phase * 19 + i * 3) % 360, (phase * 37 + i * 5) % 360, (phase * 11 + i * 7) % 360))
            if (i + phase) % 17 == 0 then
                set_node_material(node, i + phase * 101)
            end
        end
    end

    local point = lights.get_point_lights()
    for _, l in ipairs(point) do
        if (l.index % 4) == (phase % 4) then
            local angle = t + l.index * 0.21
            lights.set_point_light(l.index, vec3(math.cos(angle) * 28.0, 2.0 + (l.index % 17) * 0.35, math.sin(angle) * 28.0), l.color, l.intensity, l.radius)
        end
    end

    for i = 0, particles.get_count() - 1 do
        if (i % 3) == (phase % 3) then
            particles.set_emitter(i, {
                spawn_rate = 500.0 + ((phase + i) % 23) * 60.0,
                noise_strength = 1.0 + ((phase + i) % 9) * 0.25,
            })
        end
    end

    if phase % 2 == 0 then
        rhi.change_present_mode("immediate")
    end
    if phase % 3 == 0 then
        safe_setting("draw_aabbs", (phase % 6) ~= 0)
        safe_setting("bloom_strength", 0.05 + (phase % 7) * 0.10)
        safe_setting("motion_blur_strength", 0.02 + (phase % 5) * 0.03)
    end

    M.sample("phase_" .. tostring(phase))
    return phase
end

function M.sample(label)
    label = label or "sample"
    local m = engine.get_metrics()
    local mem = rhi.get_gpu_memory()
    local sys = rhi.get_system_memory()
    local counts = lights.get_counts()
    local vram_mb = -1
    local host_mb = -1
    local proc_mb = -1
    if mem and mem.vram and mem.vram.used then
        vram_mb = math.floor(mem.vram.used / (1024 * 1024))
    end
    if mem and mem.host and mem.host.used then
        host_mb = math.floor(mem.host.used / (1024 * 1024))
    end
    if sys and sys.proc_private_bytes then
        proc_mb = math.floor(sys.proc_private_bytes / (1024 * 1024))
    end

    pe_log(string.format(
        "[EDITOR_STRESS_SAMPLE] label=%s fps=%.2f frame_ms=%.3f present=%s models=%d nodes=%d scripted=%d point=%d spot=%d area=%d dir=%d emitters=%d particles=%d vram_mb=%d host_mb=%d proc_mb=%d",
        label,
        m.fps,
        m.delta_ms,
        rhi.get_present_mode(),
        scene.get_model_count(),
        #(M.state.nodes or {}),
        #(M.state.scripted_nodes or {}),
        counts.point,
        counts.spot,
        counts.area,
        counts.directional,
        particles.get_count(),
        particles.get_particle_count(),
        vram_mb,
        host_mb,
        proc_mb))
end

function M.cleanup()
    if particles and particles.clear then
        particles.clear()
    end
    scene.clear()
    pe_log("[editor_stress] cleanup complete")
end

pe_log("[editor_stress] scenario loaded")
