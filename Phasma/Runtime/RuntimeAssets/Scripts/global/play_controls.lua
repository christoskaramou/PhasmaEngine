-- play_controls.lua
-- Escape exits play mode, P toggles pause

local escape_was_down = false
local pause_was_down = false

local function update()
    local escape_down = input.is_key_down("Escape")
    local pause_down = input.is_key_down("P")

    -- Escape: stop play mode (on key press edge)
    if escape_down and not escape_was_down then
        engine.set_play_mode(false)
    end

    -- P: toggle pause (on key press edge)
    if pause_down and not pause_was_down then
        engine.set_paused(not engine.is_paused())
    end

    escape_was_down = escape_down
    pause_was_down = pause_down
end

hooks {
    update = update,
    update_editor = update
}
