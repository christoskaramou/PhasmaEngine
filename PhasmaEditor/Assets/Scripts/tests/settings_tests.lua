-- Settings binding test suite

function run_settings_tests()
    pe_log("=== Settings Tests ===")
    T.reset()

    -- Bool settings get/set
    local original_ssao = settings.get("ssao")
    T.check("get ssao returns bool", type(original_ssao) == "boolean")

    settings.set("ssao", not original_ssao)
    T.check("set ssao toggles", settings.get("ssao") == (not original_ssao))
    settings.set("ssao", original_ssao)

    -- New bool settings
    T.check("get randomize_lights", type(settings.get("randomize_lights")) == "boolean")
    T.check("get use_Disney_PBR", type(settings.get("use_Disney_PBR")) == "boolean")
    T.check("get freeze_frustum_culling", type(settings.get("freeze_frustum_culling")) == "boolean")
    T.check("get aabbs_depth_aware", type(settings.get("aabbs_depth_aware")) == "boolean")
    T.check("get dynamic_rendering", type(settings.get("dynamic_rendering")) == "boolean")

    -- Float settings get/set
    local original_scale = settings.get("render_scale")
    T.check("get render_scale returns number", type(original_scale) == "number")

    settings.set("render_scale", 0.5)
    T.check("set render_scale", settings.get("render_scale") == 0.5)
    settings.set("render_scale", original_scale)

    -- Uint32 settings
    local shadow_size = settings.get("shadow_map_size")
    T.check("get shadow_map_size", type(shadow_size) == "number" and shadow_size > 0)

    local num_cascades = settings.get("num_cascades")
    T.check("get num_cascades", type(num_cascades) == "number" and num_cascades > 0)

    -- Render mode
    local mode = settings.get_render_mode()
    T.check("get_render_mode returns string", type(mode) == "string")
    T.check("get_render_mode valid", mode == "raster" or mode == "hybrid" or mode == "ray_tracing")

    local original_mode = mode
    settings.set_render_mode("raster")
    T.check("set_render_mode raster", settings.get_render_mode() == "raster")
    settings.set_render_mode(original_mode)

    -- is_ray_tracing_supported
    local rt = settings.is_ray_tracing_supported()
    T.check("is_ray_tracing_supported returns bool", type(rt) == "boolean")

    -- Int settings
    local mbs = settings.get("motion_blur_samples")
    T.check("get motion_blur_samples", type(mbs) == "number" and mbs > 0)

    -- Depth bias
    local db = settings.get_depth_bias()
    T.check("get_depth_bias returns table", type(db) == "table")
    T.check("depth_bias has 3 values", db[1] ~= nil and db[2] ~= nil and db[3] ~= nil)

    local orig_db = {db[1], db[2], db[3]}
    settings.set_depth_bias(0.0, 0.0, -4.0)
    local db2 = settings.get_depth_bias()
    T.check("set_depth_bias", db2[3] == -4.0)
    settings.set_depth_bias(orig_db[1], orig_db[2], orig_db[3])

    -- Invalid setting name returns nil
    local bad = settings.get("nonexistent_setting")
    T.check("invalid setting returns nil", bad == nil)

    T.summary("Settings Tests")
end
