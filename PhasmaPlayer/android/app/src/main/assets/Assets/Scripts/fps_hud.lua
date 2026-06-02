-- fps_hud.lua — on-screen FPS counter + render-settings menu (Android player HUD).
-- Mirrors the editor "Global" widget's effect toggles via runtime_ui checkboxes bound to
-- settings.get/set. Attached to a scene node (Component_Script).
--
-- NOTE: every effect listed below must have its shader baked into the SPIR-V cache
-- (tools/bake_android_shaders.ps1 against an all-effects-on scene); the Android player has no
-- runtime shader compiler, so toggling on an unbaked effect would hard-fault. render_scale and
-- the float sliders/cascade-count are intentionally omitted (no slider primitive; render_scale
-- needs a Resize event, and cascade count / shadow-map size are compile-time shader defines).

local hud_shown = false
local menu_open = false

-- { display label, GlobalSettings bool key } — all runtime/pass-skip flags (no shader define).
local toggles = {
    { "Cast Shadows",   "shadows" },
    { "IBL",            "IBL" },
    { "SSR",            "ssr" },
    { "FXAA",           "fxaa" },
    { "TAA",            "taa" },
    { "CAS Sharpen",    "cas_sharpening" },
    { "Tonemapping",    "tonemapping" },
    { "Bloom",          "bloom" },
    { "Depth of Field", "dof" },
    { "Motion Blur",    "motion_blur" },
    { "Frustum Cull",   "frustum_culling" },
}

local function update()
    -- FPS counter
    local m = engine.get_metrics()
    local fps = m and m.fps or 0
    runtime_ui.set_number("hud", "fps", "FPS", math.floor(fps + 0.5))

    -- Settings toggle button lives on the HUD screen.
    runtime_ui.set_button("hud", "menu_btn", menu_open and "Settings -" or "Settings +")
    if runtime_ui.consume_click("hud", "menu_btn") then
        menu_open = not menu_open
        runtime_ui.set_visible("settings", menu_open)
    end

    if not hud_shown then
        runtime_ui.show("hud")
        hud_shown = true
    end

    -- Settings panel: one checkbox per effect, two-way bound to the live setting.
    if menu_open then
        for _, t in ipairs(toggles) do
            local label, key = t[1], t[2]
            local cur = settings.get(key)
            if cur == nil then cur = false end
            local ui_val = runtime_ui.get_bool("settings", key, cur)
            if ui_val ~= cur then
                settings.set(key, ui_val)
                cur = ui_val
            end
            runtime_ui.set_bool("settings", key, label, cur)
        end

        runtime_ui.show("settings")
    end
end

hooks {
    update = update,
    update_editor = update,
}
