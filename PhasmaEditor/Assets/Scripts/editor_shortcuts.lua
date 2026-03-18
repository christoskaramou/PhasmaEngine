-- editor_shortcuts.lua
-- Keyboard shortcuts (moved from C++ Window::ProcessEvents)
-- Escape: context-sensitive (close file selector / deselect / close popup)
-- F: focus camera on selected object
-- Ctrl+T: trigger GPU capture
-- Ctrl+G: toggle GUI
-- W/E/R: gizmo translate/rotate/scale (when object selected)

local escape_was_down = false
local t_was_down = false
local g_was_down = false
local f_was_down = false
local w_was_down = false
local e_was_down = false
local r_was_down = false

local function handle_escape()
    -- 1. Popup open (modal) → ImGui closes it natively via Escape, nothing to do
    if engine.is_popup_open() then return end

    local focused = engine.get_focused_window()

    -- 2. File selector focused → close it
    if focused == "Select File" then
        engine.close_file_selector()
        return
    end

    -- 3. Agent input focused → defocus (clear ImGui keyboard focus)
    if focused == "Agent" then
        imgui.set_keyboard_focus_here(-1)
        return
    end

    -- 4. Viewport focused → deselect
    if focused == "Viewport" then
        selection.clear()
        return
    end
end

local function update_editor()
    local rmb = input.is_right_mouse_down()

    local escape_down = input.is_key_down("Escape")
    local ctrl_down = input.is_key_down("Left Ctrl")
    local t_down = input.is_key_down("T")
    local g_down = input.is_key_down("G")
    local f_down = input.is_key_down("F")
    local w_down = input.is_key_down("W")
    local e_down = input.is_key_down("E")
    local r_down = input.is_key_down("R")

    -- Escape: context-sensitive (on key press edge only)
    if escape_down and not escape_was_down then
        handle_escape()
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

        -- F: focus camera on selected object (only when not typing in UI)
        if f_down and not f_was_down and not engine.want_capture_keyboard() then
            selection.focus()
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
    f_was_down = f_down
    w_was_down = w_down
    e_was_down = e_down
    r_was_down = r_down
end

hooks {
    update_editor = update_editor
}
