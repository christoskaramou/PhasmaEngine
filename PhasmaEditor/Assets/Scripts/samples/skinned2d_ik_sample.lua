-- Run with: local code = assert(fs.read("Scripts/samples/skinned2d_ik_sample.lua")); assert(load(code))()

skinned2d_ik_sample = skinned2d_ik_sample or {}

local M = skinned2d_ik_sample
local UPDATE_ID = "skinned2d_ik_sample"

local state = {
    strip = nil,
    target = nil,
    joint_influences = nil,
    width_scales = nil,
    time = 0.0,
    previous_render_mode = nil,
}

local function log(message)
    if pe_log then
        pe_log("[Skinned2D IK Sample] " .. message)
    end
end

local function apply_sample_render_settings()
    if not settings or not settings.get_render_mode or not settings.set_render_mode then
        return
    end

    state.previous_render_mode = settings.get_render_mode()
    if state.previous_render_mode ~= "raster" then
        settings.set_render_mode("raster")
        log("render mode set to raster")
    end
end

local function restore_render_settings()
    if settings and settings.set_render_mode and state.previous_render_mode then
        settings.set_render_mode(state.previous_render_mode)
    end
    state.previous_render_mode = nil
end

local function configure_camera()
    local cam = scene.get_active_camera()
    if not cam then
        cam = scene.add_camera()
        scene.set_active_camera(cam)
    end

    cam:set_projection_mode("orthographic")
    cam:set_orthographic_size(9.5)
    cam:set_near(0.01)
    cam:set_far(1000.0)
    cam:set_position(vec3(0.0, 0.0, 36.0))
    cam:look_at(vec3(0.0, 0.0, 0.0))

    if scene.add_directional_light then
        scene.add_directional_light()
    end
end

local function update()
    if not state.strip or not state.strip:is_valid() then
        return
    end

    local metrics = engine and engine.get_metrics and engine.get_metrics() or nil
    local dt = metrics and math.min(metrics.delta_ms / 1000.0, 0.05) or 0.016
    state.time = state.time + dt

    local target = vec2(
        2.60 + math.sin(state.time * 1.4) * 2.10,
        math.sin(state.time * 2.1) * 1.55)

    if state.target and state.target:is_valid() then
        state.target:set_position(vec3(target.x, target.y, 0.08))
    end

    animation.solve_strip_ik_2d(state.strip, target, 10, 60.0, 1.45, state.joint_influences, state.width_scales)
end

function M.stop()
    if script then
        script.remove_update(UPDATE_ID)
    end
    restore_render_settings()
    state.strip = nil
    state.target = nil
    state.joint_influences = nil
    state.width_scales = nil
    state.time = 0.0
end

function M.reset()
    if not primitives or not primitives.skinned_strip_2d then
        log("primitives.skinned_strip_2d is unavailable")
        return false
    end
    if not animation or not animation.solve_strip_ik_2d then
        log("2D strip IK binding is unavailable")
        return false
    end

    M.stop()
    scene.clear()
    apply_sample_render_settings()
    configure_camera()

    state.strip = primitives.skinned_strip_2d(7.0, 0.75, 64, 32)
    state.strip:set_name("Skinned2D IK Strip")
    state.strip:set_position(vec3(0.0, 0.0, 0.0))
    state.joint_influences = {}
    state.width_scales = {}
    local joint_count = animation.get_joint_count(state.strip)
    for i = 1, joint_count do
        local t = (i - 1) / math.max(joint_count - 1, 1)
        state.joint_influences[i] = 0.35 + math.sin(t * math.pi) * 1.15
        state.width_scales[i] = 1.2 - t * 0.75
    end

    state.target = primitives.circle(0.16, 32)
    state.target:set_name("Skinned2D IK Target")
    state.target:set_position(vec3(2.35, 0.0, 0.08))

    if material then
        material.set(state.strip, "base_color", vec4(0.12, 0.68, 0.86, 1.0))
        material.set(state.target, "base_color", vec4(1.0, 0.76, 0.18, 1.0))
    end

    script.on_update(UPDATE_ID, update, "always")
    update()
    log("ready")
    return true
end

M.reset()
