-- fps_hud.lua — on-screen FPS counter via the runtime UI (Android player HUD).
-- Attached to a scene node (Component_Script). engine.get_metrics().fps is 1/dt.

local shown = false

local function update()
    local m = engine.get_metrics()
    local fps = m and m.fps or 0
    runtime_ui.set_number("hud", "fps", "FPS", math.floor(fps + 0.5))
    if not shown then
        runtime_ui.show("hud")
        shown = true
    end
end

hooks {
    update = update,
    update_editor = update,
}
