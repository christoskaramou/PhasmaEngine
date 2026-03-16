-- editor_shortcuts.lua
-- Keyboard shortcuts (moved from C++ Window::ProcessEvents)
-- Escape: exit confirmation (when not in play mode)
-- Ctrl+T: trigger GPU capture
-- Ctrl+G: toggle GUI
-- W/E/R: gizmo translate/rotate/scale (when object selected)

local escape_was_down = false
local t_was_down = false
local g_was_down = false
local w_was_down = false
local e_was_down = false
local r_was_down = false

local function update_editor()
    local rmb = input.is_right_mouse_down()

    local escape_down = input.is_key_down("Escape")
    local ctrl_down = input.is_key_down("Left Ctrl")
    local t_down = input.is_key_down("T")
    local g_down = input.is_key_down("G")
    local w_down = input.is_key_down("W")
    local e_down = input.is_key_down("E")
    local r_down = input.is_key_down("R")

    -- Escape: exit confirmation (only when not in play mode)
    if escape_down and not escape_was_down then
        if not engine.is_play_mode() then
            if not engine.is_popup_open() then
                engine.trigger_exit_confirmation()
            end
        end
    end

    -- Shortcuts below only when right-click is NOT held
    if not rmb then
        -- Ctrl+T: trigger GPU capture
        if ctrl_down and t_down and not t_was_down then
            debug.trigger_capture()
        end

        -- Ctrl+G: toggle GUI
        if ctrl_down and g_down and not g_was_down then
            engine.toggle_gui()
        end

        -- W/E/R: gizmo mode (only when object selected and not typing in UI)
        if not engine.want_capture_keyboard() then
            local sel = selection.get()
            if sel.has_selection then
                if w_down and not w_was_down then
                    selection.set_gizmo("translate")
                end
                if e_down and not e_was_down then
                    selection.set_gizmo("rotate")
                end
                if r_down and not r_was_down then
                    selection.set_gizmo("scale")
                end
            end
        end
    end

    escape_was_down = escape_down
    t_was_down = t_down
    g_was_down = g_down
    w_was_down = w_down
    e_was_down = e_down
    r_was_down = r_down
end

hooks {
    update_editor = update_editor
}
