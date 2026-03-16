-- play_controls.lua
-- Escape exits play mode, Space toggles pause

local escape_was_down = false
local space_was_down = false

local function update_editor()
    if not engine.is_play_mode() then return end

    local escape_down = input.is_key_down("Escape")
    local space_down = input.is_key_down("Space")

    -- Escape: stop play mode (on key press edge)
    if escape_down and not escape_was_down then
        engine.set_play_mode(false)
    end

    -- Space: toggle pause (on key press edge)
    if space_down and not space_was_down then
        engine.set_paused(not engine.is_paused())
    end

    escape_was_down = escape_down
    space_was_down = space_down
end

hooks {
    update_editor = update_editor
}
