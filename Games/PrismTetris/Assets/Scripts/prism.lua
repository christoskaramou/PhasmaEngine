local function module(path)
    local source = assert(fs.read(path), "Missing PRISM module: " .. path)
    return assert(load(source, "@" .. path, "t", _ENV))()
end

local Rules = module("Scripts/tetris_rules.lua")
local Stage = module("Scripts/prism_stage.lua")
local Board = module("Scripts/prism_board3d.lua")
local Hud = module("Scripts/prism_hud.lua")
local Jev = module("Scripts/prism_jev.lua")
local Driver = module("Scripts/gamekit/driver.lua")
Driver._json = module("Scripts/gamekit/json.lua")
local Agent = module("Scripts/tetris_agent.lua")

-- Races connect the driver to this game's MCP port, which the launcher writes into the runtime copy.
local function mcp_port()
    local config = fs.read("Agent/agent_config.json") or ""
    return tonumber(config:match('"mcp_port"%s*:%s*(%d+)')) or 8765
end

local seed = math.floor(engine.get_metrics().delta_ms * 1000000) + 731
local game = Rules.new(seed)
local rival = Rules.new(seed)
local stage = Stage.new()
local board = Board.new({
    rules = Rules,
    stage = stage,
    origin = { Stage.BOARD_X.you, 0, 0 },
})
local rival_board = Board.new({
    rules = Rules,
    stage = stage,
    origin = { Stage.BOARD_X.jev, 0, 0 },
    lead = false,
})
rival_board:set_shown(false)
local view = Hud.new(Rules)
local jev = Jev.new(mcp_port())
local saved_best = tonumber(fs.read("Save/prism_best.txt") or "0") or 0
-- `panel` is "key" while the Jev key panel replaces the menu; `jev` lets the HUD show the key status.
local state = {
    mode = "ready",
    best = math.max(0, math.floor(saved_best)),
    muted = false,
    motion = true,
    versus = false,
    jev = jev,
}
-- What the HUD shows of a race: Jev's game, the driver's stats and the outcome.
local race = {
    rival = rival,
    jev = jev,
}
local you = {
    game = game,
    board = board,
    name = "YOU",
}
local them = {
    game = rival,
    board = rival_board,
    name = "JEV",
}
local keys = {}
local repeat_direction = 0
local repeat_time = 0
local refresh = 0
local agent

local function sound(name)
    if audio and audio.play and not state.muted then
        audio.play(name .. ".wav")
    end
end

local function remember_best()
    if state.assisted then
        return
    end
    if game.score > state.best then
        state.best = game.score
        fs.write("Save/prism_best.txt", tostring(state.best))
    end
end

-- Only your seat makes sounds; in a race the top-out is judged once both boards have moved this frame.
local function feedback(seat, event)
    view:impact(event, state.versus and seat.name or nil)
    seat.board:impact(event, state.motion)
    if not event then
        return
    end

    remember_best()
    if seat ~= you then
        return
    end

    if event.kind == "game_over" then
        if not state.versus then
            state.mode = "over"
        end
        sound("over")
    elseif event.lines > 0 then
        sound(event.lines == 4 and "tetris" or "clear")
    else
        sound("lock")
    end
end

-- "versus" shows Jev's board, the race HUD and a camera framing both boards; "solo" frames your board alone.
local function show_race(on)
    state.versus = on
    view:set_layout(on and "versus" or "solo")
    stage:set_layout(on and "versus" or "solo")
    rival_board:set_shown(on)
end

local function new_game(requested_seed)
    remember_best()
    jev:stop()
    show_race(false)
    state.panel = nil
    seed = requested_seed or (seed + 1)
    game:restart(seed)
    state.mode = "playing"
    state.assisted = false
    state.agent = false
    repeat_direction = 0
    repeat_time = 0
    view.toast_time = 0
    view.dirty = true
    board:reset()
    sound("rotate")
end

-- `winner` is "you", "jev" or "draw"; a race that stops for another reason carries the driver's message.
local function end_race(winner, reason)
    race.winner = winner
    race.reason = reason
    state.mode = "over"
    view.dirty = true
    jev:stop(true)
    sound("over")
end

-- One seed for both boards, then Jev joins through the driver; it never changes the solo best.
local function start_race()
    remember_best()
    seed = seed + 1
    game:restart(seed)
    rival:restart(seed)
    show_race(true)
    state.panel = nil
    state.mode = "playing"
    state.assisted = true
    state.agent = true
    race.winner = nil
    race.reason = nil
    repeat_direction = 0
    repeat_time = 0
    view.toast_time = 0
    view.dirty = true
    board:reset()
    rival_board:reset()
    sound("rotate")

    if not jev:start() then
        end_race(nil, jev.error)
    end
end

-- The first board to top out loses; both in the same frame is a draw. A driver that stops ends the race.
local function judge_race()
    if not state.versus or state.mode == "over" then
        return
    end

    if game.game_over or rival.game_over then
        local winner = "you"
        if game.game_over and rival.game_over then
            winner = "draw"
        elseif game.game_over then
            winner = "jev"
        end
        end_race(winner)
        return
    end

    if not jev.running then
        end_race(nil, jev.error or "Jev stopped playing.")
    end
end

local piece_actions = {
    left = function(target) return target:move(-1) end,
    right = function(target) return target:move(1) end,
    cw = function(target) return target:rotate(1) end,
    ccw = function(target) return target:rotate(-1) end,
    hold = function(target) return target:hold() end,
    down = function(target) return target:soft_drop() end,
}

-- One input on a seat's game (yours unless given): the shared controller for keys and the agent alike.
local function apply_action(command, seat)
    seat = seat or you
    local target = seat.game
    if state.mode ~= "playing" or target.game_over then
        return false
    end
    if command == "drop" then
        feedback(seat, target:hard_drop())
        return true
    end

    local action = piece_actions[command]
    if not action or not action(target) then
        return false
    end
    view.dirty = true
    if seat == you and (command == "left" or command == "right") then
        sound("move")
    elseif seat == you and command ~= "down" then
        sound("rotate")
    end
    if target.game_over and not state.versus then
        state.mode = "over"
        sound("over")
    end
    return true
end

-- MENU and P open the menu during play and close it again.
local function toggle_pause()
    if state.mode == "playing" then
        state.mode = "paused"
    elseif state.mode == "paused" then
        state.mode = "playing"
        state.panel = nil
    end
    view.dirty = true
end

local function begin()
    if state.mode == "paused" then
        toggle_pause()
    elseif state.mode ~= "playing" then
        new_game()
    end
end

local function edge(name, current)
    local started = current and not keys[name]
    keys[name] = current
    return started
end

local function pressed(name)
    return edge(name, input.is_key_down_raw(name))
end

local function clicked(id)
    local widget = runtime_ui.get_state("prism", id)
    return widget and widget.clicked == true
end

-- The key panel replaces the menu. A driver still finishing a race is stopped so the key commands can run.
local function open_key_panel()
    local racing = state.versus and state.mode == "paused"
    if state.mode == "playing" or racing then
        return
    end

    jev:stop()
    state.panel = "key"
    jev.saved = nil
    jev.answer = nil
    jev:key("status")
    view.dirty = true
end

local function key_panel(enter, paste)
    if paste or clicked("paste") then
        jev:key("save")
    end
    if clicked("remove") then
        jev:key("remove")
    end
    if enter or clicked("back") then
        state.panel = nil
        view.dirty = true
    end
end

local function toggles()
    local enter = pressed("Return")
    local pause = pressed("P")
    local restart = pressed("R")
    local versus = pressed("V")
    local mute = pressed("M")
    local motion = pressed("F1")
    local ctrl = input.is_key_down_raw("Left Ctrl") or input.is_key_down_raw("Right Ctrl")

    -- In the key panel Enter goes back and Ctrl+V pastes; V, R and P do nothing there.
    if state.panel == "key" then
        key_panel(enter, versus and ctrl)
        enter = false
        versus = false
        restart = false
        pause = false
    end

    if enter or clicked("start") then
        begin()
    end
    if pause or clicked("pause") then
        toggle_pause()
    end
    if restart then
        if state.versus then
            start_race()
        else
            new_game()
        end
    end
    if clicked("solo") then
        new_game()
    end

    local race_key = versus and not ctrl
    local race_asked = race_key or clicked("versus")
    if race_asked and state.mode ~= "playing" then
        start_race()
    end
    if clicked("key") then
        open_key_panel()
    end
    if mute or clicked("sound") then
        state.muted = not state.muted
        view.dirty = true
    end
    if motion or clicked("motion") then
        state.motion = not state.motion
        board:clear_effects()
        rival_board:clear_effects()
        view.dirty = true
    end
end

local function horizontal(dt)
    local left = input.is_key_down_raw("Left") or input.is_key_down_raw("A")
    local right = input.is_key_down_raw("Right") or input.is_key_down_raw("D")
    local direction = (right and 1 or 0) - (left and 1 or 0)
    if direction ~= repeat_direction then
        repeat_direction = direction
        repeat_time = 0.16
        if direction ~= 0 then
            apply_action(direction == -1 and "left" or "right")
        end
    elseif direction ~= 0 then
        repeat_time = repeat_time - dt
        while repeat_time <= 0 do
            apply_action(direction == -1 and "left" or "right")
            repeat_time = repeat_time + 0.045
        end
    end
end

local function gravity(seat, dt, soft)
    local target = seat.game
    local previous_y = target.active and target.active.y
    local event = target:tick(dt, soft)
    if event then
        feedback(seat, event)
    elseif target.active and target.active.y ~= previous_y then
        view.dirty = true
    end
end

local function control(dt)
    local up = pressed("Up")
    local x = pressed("X")
    local z = pressed("Z")
    local c = pressed("C")
    local shift = pressed("Left Shift")
    local space = pressed("Space")
    -- Left click turns the piece counterclockwise, right click clockwise (user request 2026-10-15); the engine
    -- reports no click while the cursor is on a HUD button.
    local left_click = edge("mouse left", input.is_left_mouse_down())
    local right_click = edge("mouse right", input.is_right_mouse_down())
    if state.mode ~= "playing" then
        repeat_direction = 0
        return
    end

    local moving = input.is_key_down_raw("Left") or input.is_key_down_raw("Right")
        or input.is_key_down_raw("A") or input.is_key_down_raw("D")
    local soft = input.is_key_down_raw("Down") or input.is_key_down_raw("S")
    local clockwise = up or x or right_click
    local counterclockwise = z or left_click
    local rotating = clockwise or counterclockwise
    local placing = c or shift or space
    local human = rotating or placing or moving or soft
    if human and not state.versus then
        state.agent = false
    end

    -- The agent plays through timed inputs under the same gravity. In solo a human key above takes the piece
    -- from it; in a race your keys drive your board and the agent drives Jev's.
    agent.tick(dt)
    horizontal(dt)
    if clockwise then
        apply_action("cw")
    end
    if counterclockwise then
        apply_action("ccw")
    end
    if c or shift then
        apply_action("hold")
    end

    local dropped = space and apply_action("drop")
    if not dropped then
        gravity(you, dt, soft)
    end
    if state.versus then
        gravity(them, dt, false)
    end
end

if audio and audio.set_sfx_volume then
    audio.set_sfx_volume(0.45)
end

-- Expose the live game for the isolated editor's automation console.
agent = Agent.attach({
    rules = Rules,
    game = function()
        return state.versus and rival or game
    end,
    state = state,
    apply = function(command)
        return apply_action(command, state.versus and them or you)
    end,
    reset = new_game,
    driver = Driver,
})
if hooks then
    hooks {
        init = function() agent:attach() end,
        destroy = function() agent:detach() end,
    }
end
_G.prism = {
    game = game,
    rival = rival,
    state = state,
    view = view,
    board = board,
    rival_board = rival_board,
    stage = stage,
    jev = jev,
    race = race,
    start = begin,
    restart = new_game,
    versus = start_race,
    apply = apply_action,
    agent = agent,
}

local function present(dt)
    view:render(game, state, dt, state.versus and race or nil)
    stage:resize(view.width or 0, view.height or 0)
    board:sync(game, state.mode)
    rival_board:sync(rival, state.mode)
    board:update(dt, state.motion)
    rival_board:update(dt, state.motion)
    stage:update(dt, state.motion)
end

script.on_update("prism_tetris", function()
    local metrics = engine.get_metrics()
    local dt = math.max(0, math.min(0.1, metrics.delta_ms / 1000))
    toggles()
    control(dt)

    -- Race output and key-panel answers both arrive through the driver process.
    if jev:update(dt) then
        view.dirty = true
    end
    judge_race()

    refresh = refresh + dt
    if refresh >= 0.2 then
        remember_best()
        refresh = 0
    end

    present(dt)
end, "play")

present(0)
print("[PRISM] Ready. Enter plays; V races Jev; arrows move; Space drops; C holds; P pauses.")
