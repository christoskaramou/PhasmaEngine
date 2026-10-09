-- The engine's default render pipeline (Scene Settings > Pipeline). Each value is the scene setting of the same
-- name: the Pipeline panel shows and edits it, a loaded scene keeps its own saved values, and the literals here
-- apply when this script is picked for a scene. A built-in pass runs when its switch is on and the render path
-- needs it. A project pipeline script starts from these with pipeline.base() and adds its own passes from init()
-- with render_graph.add_fullscreen_pass / render_graph.add_pass. exposed_when nests a value under the switch it
-- needs; the Pipeline panel hides it while that switch is off.
exposed {
    -- Culling and geometry
    frustum_culling = true,
    occlusion_culling = false,
    occlusion_culling_bias = 0.002,
    skinned_instancing = false,
    cluster_geometry = false,
    cluster_error_pixels = 1.0,
    lod_enabled = true,
    lod_count = 4,
    lod_bias = 1.0,
    lod_distance_1 = 30.0,
    lod_distance_2 = 90.0,
    lod_distance_3 = 250.0,

    -- Shadows
    shadows = true,
    shadow_map_size = 2048,
    num_cascades = 4,
    shadow_distance = 250.0,
    shadow_cascade_lambda = 0.85,
    shadow_normal_bias = 1.5,
    shadow_fade_fraction = 0.15,
    shadow_filter_radius = 0.75,
    shadow_lod_bias = 1.0,
    shadow_debug_mode = 0,
    depth_bias_constant = 0.0,
    depth_bias_clamp = 0.0,
    depth_bias_slope = -6.2,

    -- Lighting
    use_Disney_PBR = true,
    physical_point_falloff = false,
    forward_plus = true,
    IBL = true,
    IBL_intensity = 1.0,
    global_illumination = false,
    gi_probe_spacing = 2.0,
    gi_intensity = 1.0,
    gi_hysteresis = 0.999,
    ssao = true,
    ssao_radius = 0.5,
    ssao_bias = 0.025,
    ssao_intensity = 0.5,
    ssao_power = 1.0,
    ssao_samples = 16,
    ssr = false,

    -- Post-process (the default profile; post-process zones override it)
    taa = true,
    cas_sharpening = true,
    cas_sharpness = 0.5,
    fxaa = false,
    tonemapping = false,
    bloom = false,
    bloom_strength = 1.0,
    bloom_range = 1.0,
    dof = false,
    dof_focus_scale = 15.0,
    dof_blur_range = 5.0,
    motion_blur = true,
    motion_blur_strength = 1.0,
    motion_blur_samples = 16,
    color_grading = false,
    color_grading_lift_r = 0.0,
    color_grading_lift_g = 0.0,
    color_grading_lift_b = 0.0,
    color_grading_gamma_r = 1.0,
    color_grading_gamma_g = 1.0,
    color_grading_gamma_b = 1.0,
    color_grading_gain_r = 1.0,
    color_grading_gain_g = 1.0,
    color_grading_gain_b = 1.0,
    color_grading_saturation = 1.0,
    color_grading_contrast = 1.0,
    color_grading_intensity = 1.0,
    color_grading_mask = "",

    -- G-buffer formats ("" = the engine's)
    normal_format = "",
    velocity_format = "",
}

local function under(switch, names)
    local when = {}
    for _, name in ipairs(names) do
        when[name] = switch
    end
    exposed_when(when)
end
under("occlusion_culling", {"occlusion_culling_bias"})
under("cluster_geometry", {"cluster_error_pixels"})
under("lod_enabled", {"lod_count", "lod_bias", "lod_distance_1", "lod_distance_2", "lod_distance_3"})
under("shadows", {"shadow_map_size", "num_cascades", "shadow_distance", "shadow_cascade_lambda", "shadow_normal_bias",
                  "shadow_fade_fraction", "shadow_filter_radius", "shadow_lod_bias", "shadow_debug_mode",
                  "depth_bias_constant", "depth_bias_clamp", "depth_bias_slope"})
under("IBL", {"IBL_intensity"})
under("global_illumination", {"gi_probe_spacing", "gi_intensity", "gi_hysteresis"})
under("ssao", {"ssao_radius", "ssao_bias", "ssao_intensity", "ssao_power", "ssao_samples"})
under("taa", {"cas_sharpening"}) -- sharpening runs only after TAA
under("cas_sharpening", {"cas_sharpness"})
under("bloom", {"bloom_strength", "bloom_range"})
under("dof", {"dof_focus_scale", "dof_blur_range"})
under("motion_blur", {"motion_blur_strength", "motion_blur_samples"})
under("color_grading", {"color_grading_lift_r", "color_grading_lift_g", "color_grading_lift_b", "color_grading_gamma_r",
                        "color_grading_gamma_g", "color_grading_gamma_b", "color_grading_gain_r", "color_grading_gain_g",
                        "color_grading_gain_b", "color_grading_saturation", "color_grading_contrast",
                        "color_grading_intensity", "color_grading_mask"})
