local root = (arg[0]:gsub("\\", "/"):match("^(.*)/tests/[^/]+$") or "Games/PrismTetris")
local assets = root .. "/Assets/"
local engine3d = dofile(root .. "/tests/engine3d_stubs.lua")
local widgets = {}
local clicks = {}
local held = {}
local callback
local writes = {}
local sounds = {}
local calls = 0
local width = 1600
local height = 1000
local visible = false
button_titles = {}

local function fingerprint(value)
    if type(value) ~= "table" then
        return type(value) .. ":" .. tostring(value)
    end
    local keys = {}
    for k in pairs(value) do
        keys[#keys + 1] = k
    end
    table.sort(keys, function(a, b) return tostring(a) < tostring(b) end)
    local parts = {}
    for _, k in ipairs(keys) do
        parts[#parts + 1] = fingerprint(k) .. "=" .. fingerprint(value[k])
    end
    return "{" .. table.concat(parts, ",") .. "}"
end
json_encode = fingerprint

fs = {
    read = function(path)
        if path:match("^Save/") then
            return writes[path]
        end
        local file = assert(io.open(assets .. path, "r"))
        local data = file:read("*a")
        file:close()
        return data
    end,
    write = function(path, data)
        writes[path] = data
        return true
    end,
}
engine = { get_metrics = function() return { delta_ms = 16.667 } end }
script = {
    on_update = function(_, fn) callback = fn end,
    tick = function() callback() end,
}
input = {
    is_key_down_raw = function(key) return held[key] == true end,
    is_left_mouse_down = function() return held["mouse left"] == true end,
    is_right_mouse_down = function() return held["mouse right"] == true end,
}
audio = {
    play = function(path) sounds[#sounds + 1] = path end,
    set_sfx_volume = function() end,
}
-- The race driver as the game sees it: one child process whose output arrives line by line.
driver = {
    started = 0,
    stopped = 0,
    killed = 0,
    lines = {},
    running = false,
}
proc = {
    -- One child at a time, like the engine: starting another kills the one still running.
    start = function(path, args)
        if driver.running then
            driver.killed = driver.killed + 1
        end
        driver.started = driver.started + 1
        driver.path = path
        driver.args = args
        if driver.fail then
            return false, driver.fail
        end
        driver.running = true
        return true
    end,
    read_line = function() return table.remove(driver.lines, 1) end,
    is_running = function() return driver.running end,
    stop = function()
        driver.running = false
        driver.stopped = driver.stopped + 1
    end,
}
runtime_ui = {
    clear = function() widgets = {} end,
    set_screen_overlay = function() end,
    show = function() visible = true end,
    get_surface_size = function() return { width = width, height = height } end,
    set_quad = function(_, id, options)
        calls = calls + 1
        options.visible = true
        widgets[id] = options
        if options.style == "button" then
            button_titles[options.title] = true
        end
    end,
    set_many_visible = function(_, ids, visible)
        calls = calls + 1
        for _, id in ipairs(ids) do
            if widgets[id] then
                widgets[id].visible = visible
            end
        end
    end,
    consume_click = function() return false end,
    get_state = function(_, id)
        local clicked = clicks[id]
        clicks[id] = nil
        return { clicked = clicked == true }
    end,
}

local function key(name)
    held[name] = true
    callback()
    held[name] = false
    callback()
end

dofile(assets .. "Scripts/prism.lua")
engine3d.frozen = true
assert(visible and prism.state.mode == "ready" and widgets.hud.path == "Textures/hud.png")
assert(widgets.start.visible and prism.board.cells[191].visible, "ready screen must show preview blocks")
key("Return")
assert(prism.state.mode == "playing" and not widgets.start.visible)
assert(sounds[1] == "rotate.wav", "audio.play resolves names within the project's Audio directory")
assert(not prism.board.cells[191].visible, "start must clear decorative ready-screen blocks")

local original_x = prism.game.active.x
key("Left")
assert(prism.game.active.x == original_x - 1)
key("X")
assert(prism.game.active.rotation == 1)
key("mouse right")
assert(prism.game.active.rotation == 2, "right click rotates clockwise (user request 2026-10-15)")
key("mouse left")
assert(prism.game.active.rotation == 1, "left click rotates counterclockwise")
key("C")
local first_hold = prism.game.held
key("C")
assert(prism.game.held == first_hold and not prism.game.can_hold)
key("Space")
assert(prism.game.score > 0 and prism.game.can_hold)
assert(tonumber(writes["Save/prism_best.txt"]) == prism.game.score)

key("P")
assert(prism.state.mode == "paused" and widgets.start.visible)
original_x = prism.game.active.x
key("Left")
assert(prism.game.active.x == original_x, "paused game must ignore movement")
clicks.start = true
callback()
assert(prism.state.mode == "playing")
clicks.pause = true
callback()
assert(prism.state.mode == "paused")
clicks.pause = true
callback()
assert(prism.state.mode == "playing")
clicks.sound = true
callback()
assert(prism.state.muted)
clicks.sound = true
callback()
assert(not prism.state.muted)
clicks.motion = true
callback()
assert(not prism.state.motion)
clicks.motion = true
callback()
assert(prism.state.motion)
clicks.solo = true
callback()
assert(prism.game.score == 0 and prism.state.mode == "playing", "SOLO starts a fresh solo game")
assert(widgets.pause.title == "MENU", "the top button is MENU (user request 2026-10-15)")
key("M")
local sound_count = #sounds
key("Left")
assert(prism.state.muted and #sounds == sound_count)
key("F1")
assert(not prism.state.motion)
key("R")
assert(prism.game.score == 0 and prism.game.lines == 0)

local prism_event = {
    kind = "clear",
    lines = 4,
    rows = { 19, 20, 21, 22 },
    points = 800,
    drop = 0,
}
local burst_mark = #engine3d.bursts
prism.view:impact(prism_event, false)
prism.board:impact(prism_event, false)
callback()
assert(widgets.toast.visible and widgets.toast.body:find("TETRIS"))
assert(prism.board.flashes[1].life == 0 and prism.board.flashes[2].life == 0, "FX off suppresses flash lights")
assert(#engine3d.bursts == burst_mark, "FX off emits no particle bursts")
for _ = 1, 100 do
    callback()
end
assert(not widgets.toast.visible)

width = 800
height = 500
callback()
assert(widgets.hud.width == 800 and widgets.hud.height == 500)
assert(widgets.score.x == 165 and widgets.score.font_scale == 2.35 / 2)
key("P")
local previous_calls = calls
for _ = 1, 20 do
    callback()
end
assert(calls == previous_calls, "idle paused UI must reuse retained widgets")

-- Race HUD, driven directly: the race overlay, a card and previews per board, Jev's stats, the results card.
local Rules = dofile(assets .. "Scripts/tetris_rules.lua")
local view = prism.view
local rival_game = Rules.new(9)
rival_game:hard_drop()
local race = {
    rival = rival_game,
    jev = { think_ms = 261, calls = 41, cost = 0.0051 },
}
local function shown(id)
    return widgets[id] ~= nil and widgets[id].visible
end
local function show(mode, race_info)
    prism.state.mode = mode
    view.dirty = true
    view:render(prism.game, prism.state, 0.016, race_info)
end

view:set_layout("versus")
show("playing", race)
assert(widgets.hud.path == "Textures/hud_versus.png", "a race swaps in the race overlay")
assert(widgets.you_score.body == string.format("%06d", prism.game.score), "your card shows your score")
assert(widgets.jev_score.body == string.format("%06d", rival_game.score), "Jev's card shows Jev's score")
assert(widgets.jev_level.body == "01" and widgets.jev_lines.body == "000", "Jev's card shows its level and lines")
assert(widgets.jev_next1_1.path == "Textures/block_" .. rival_game.next[1] .. ".png", "Jev's next column")
assert(widgets.you_next1_1.path == "Textures/block_" .. prism.game.next[1] .. ".png", "your next column")
local stats = widgets.jev_stats.body
assert(stats:find("261 MS") and stats:find("41 CALLS") and stats:find("$0.0051", 1, true), "Jev's stats line")
assert(not widgets.score.visible and not widgets.best.visible, "solo widgets hide during a race")
assert(not widgets.start.visible, "no menu while racing")

race.winner = "jev"
show("over", race)
assert(widgets.modal_title.body == "Jev wins.", "the results card names the winner")
local body = widgets.modal_body.body
assert(body:find(string.format("%06d", rival_game.score)) and body:find("LINES"), "both runs on the results card")
assert(widgets.modal_detail.body:find("41 CALLS") and widgets.modal_detail.body:find("$0.0051", 1, true))
assert(widgets.versus.visible and widgets.versus.title:find("REMATCH"), "rematch from the results card")
assert(widgets.solo.visible and widgets.solo.title == "SOLO", "solo from the results card")
assert(widgets.key.visible and not widgets.start.visible, "the results card offers the key, no PLAY")
race.winner = "draw"
show("over", race)
assert(widgets.modal_title.body == "Draw.", "a same-frame top-out is a draw")
race.winner = nil
race.reason = "No saved Jev key"
show("over", race)
assert(widgets.modal_title.body == "Jev stopped." and widgets.modal_detail.body:find("No saved Jev key"))
show("paused", race)
assert(widgets.solo.visible and widgets.versus.visible, "the race menu offers SOLO and a new race")
assert(not widgets.key.visible, "no key changes while Jev's driver races")

view:set_layout("solo")
show("ready")
assert(widgets.hud.path == "Textures/hud.png" and widgets.score.visible, "solo overlay and cards come back")
assert(not widgets.jev_score.visible and not widgets.jev_stats.visible, "race widgets hide in solo")
assert(widgets.start.visible and widgets.versus.visible and widgets.versus.title:find("VS JEV"), "ready offers VS JEV")
assert(widgets.key.visible and widgets.key.title == "JEV KEY", "the menu offers the Jev key")
assert(not shown("paste") and not shown("modal_detail"))
show("over")
assert(widgets.versus.visible and widgets.versus.title:find("VS JEV"), "a solo game over offers VS JEV")
show("paused")
assert(widgets.start.title:find("RESUME") and widgets.solo.visible and widgets.versus.visible and widgets.key.visible,
    "the solo menu offers RESUME, SOLO, VS JEV and JEV KEY")

-- The Jev key panel: the driver checks, pastes from the clipboard and removes the saved key; V never races here.
clicks.key = true
callback()
assert(prism.state.panel == "key" and driver.args[1] == "--key-status", "JEV KEY checks the saved key")
assert(widgets.modal_kicker.body == "JEV KEY" and widgets.modal_title.body == "Checking...")
assert(widgets.paste.visible and widgets.remove.visible and widgets.back.visible and not widgets.start.visible)
local function answer(line)
    driver.lines = { line }
    driver.running = false
    callback()
end
answer("KEY saved")
assert(widgets.modal_title.body == "Key saved.", "the panel shows a saved key")
clicks.paste = true
callback()
assert(driver.args[1] == "--save-key-from-clipboard", "PASTE KEY saves the clipboard")
answer("KEY invalid")
assert(widgets.modal_body.body == "That is not a Jev key." and widgets.modal_title.body == "Key saved.")
held["Left Ctrl"] = true
key("V")
held["Left Ctrl"] = false
assert(driver.args[1] == "--save-key-from-clipboard" and not prism.state.versus, "Ctrl+V pastes and never races")
answer("KEY clipboard-empty")
assert(widgets.modal_body.body == "Copy your Jev key first.", "an empty clipboard is named")
key("V")
assert(not prism.state.versus and prism.state.panel == "key", "plain V in the key panel never races")
clicks.remove = true
callback()
assert(driver.args[1] == "--remove-key", "REMOVE removes the saved key")
answer("KEY removed")
assert(widgets.modal_title.body == "No key saved." and widgets.modal_body.body == "Key removed.", "removed")
clicks.back = true
callback()
assert(prism.state.panel == nil and shown("key") and not shown("paste"), "BACK returns to the menu")
local key_starts = driver.started

-- The race ------------------------------------------------------------------------------------------
local function frames(count)
    for _ = 1, count do
        callback()
    end
end

-- Blocks the spawn rows above a lowered piece, so its next lock tops the board out.
local function top_out(target)
    for _ = 1, 4 do
        target:soft_drop()
    end
    for y = 1, 2 do
        for x = 4, 7 do
            target.board[y][x] = "Z"
        end
    end
end

local best_before = prism.state.best
prism.state.muted = false
key("V")
assert(prism.state.versus and prism.state.mode == "playing", "V starts a race")
assert(driver.started == key_starts + 1 and driver.path == "Bots/PhasmaGameDriver.exe", "the race starts the driver")
local args = table.concat(driver.args, " ")
local expected = "^%-%-url http://127%.0%.0%.1:%d+/mcp %-%-policy jev %-%-key%-source vault %-%-steps 10000$"
assert(args:match(expected), "driver arguments: " .. args)
assert(prism.game.active.kind == prism.rival.active.kind, "both boards start on the same piece")
assert(table.concat(prism.game.next) == table.concat(prism.rival.next), "both boards see the same queue")
assert(prism.rival_board.shown and widgets.hud.path == "Textures/hud_versus.png", "Jev's board and the race HUD show")
assert(prism.stage.target[1] == 0 and prism.state.agent, "the camera frames both boards; Jev's seat is agent-run")

local your_x = prism.game.active.x
local jev_x = prism.rival.active.x
key("Left")
assert(prism.game.active.x == your_x - 1 and prism.rival.active.x == jev_x, "your keys move only your board")
assert(prism.state.agent, "your keys never end Jev's agent mode in a race")

local observation = prism.agent:observe()
assert(#observation.actions > 0 and observation.observation.active == prism.rival.active, "the adapter plays Jev")
local far
for _, action in ipairs(observation.actions) do
    if action.description:find("^Place %a at column 1,") then
        far = action
    end
end
local yours = prism.game.active.x .. ":" .. prism.game.active.rotation
local sound_count = #sounds
prism.agent:act({ session_id = observation.session_id, state_id = observation.state_id, action_id = far.id })
frames(12)
assert(prism.rival.active.x < jev_x, "Jev's placement moves Jev's piece")
assert(prism.game.active.x .. ":" .. prism.game.active.rotation == yours, "Jev's inputs never touch your board")
assert(#sounds == sound_count, "only your seat plays sounds")

driver.lines = {
    "Trace: C:/race.jsonl",
    "jev-1.13.0 step 1 action=placement_3 score=40 decision_ms=261 input_tokens=3051",
    'jev-1.13.0 re-decide 1 after "stale_state" input_tokens=2990',
}
callback()
local jev = prism.jev
assert(jev.calls == 2 and jev.think_ms == 261 and jev.input_tokens == 6041, "driver lines feed Jev's stats")
assert(math.abs(jev.cost - 6041 * 0.042 / 1000000) < 1e-12, "cost follows TypeSafe's input-token price")
assert(widgets.jev_stats.body:find("261 MS") and widgets.jev_stats.body:find("2 CALLS"), "the HUD shows Jev's stats")

key("P")
assert(prism.state.mode == "paused", "P pauses the race")
local held = prism.game.active.y .. ":" .. prism.rival.active.y .. ":" .. prism.rival.active.x
frames(90)
assert(prism.game.active.y .. ":" .. prism.rival.active.y .. ":" .. prism.rival.active.x == held, "pause stops both")
assert(#prism.agent:observe().actions == 0, "Jev gets no actions while paused")
key("P")
assert(prism.state.mode == "playing", "P resumes the race")

top_out(prism.game)
key("Space")
assert(prism.state.mode == "over" and widgets.modal_title.body == "Jev wins.", "you topped out first: Jev wins")
assert(prism.agent:observe().terminal, "the race end is terminal for the driver")
assert(prism.state.best == best_before, "a race never changes the solo best")
driver.lines = { "Finished: decisions=1 redecides=1 api_calls=2 input_tokens=6041 output_tokens=0 terminal=true" }
frames(200)
assert(jev.finished and not driver.running and driver.stopped == 1, "a driver that lingers is stopped within 3 s")

key("V")
assert(prism.state.mode == "playing" and driver.started == key_starts + 2 and jev.calls == 0, "V rematches")
top_out(prism.rival)
prism.rival:hard_drop()
callback()
assert(widgets.modal_title.body == "You win.", "Jev topped out first: you win")

key("V")
prism.game.game_over = true
prism.rival.game_over = true
callback()
assert(widgets.modal_title.body == "Draw.", "both topping out in one frame is a draw")

key("V")
driver.lines = { "Game driver failed: No saved Jev key: Web Credential jev/api-key was not found or could not be read" }
driver.running = false
callback()
assert(prism.state.mode == "over" and widgets.modal_title.body == "Jev stopped.", "a dead driver ends the race")
assert(widgets.modal_detail.body:find("No saved Jev key"), "with the driver's own message")

driver.fail = "not found: Bots/PhasmaGameDriver.exe"
key("V")
assert(prism.state.mode == "over" and widgets.modal_detail.body:find("not found"), "a missing driver ends it at once")
driver.fail = nil

key("V")
local starts = driver.started
key("R")
assert(prism.state.versus and prism.state.mode == "playing" and driver.started == starts + 1, "R starts a new race")

prism.game.game_over = true
callback()
key("Return")
assert(not prism.state.versus and prism.state.mode == "playing", "Enter on the results card plays solo")
assert(not prism.rival_board.shown and widgets.hud.path == "Textures/hud.png", "solo hides Jev's board again")
assert(not driver.running and prism.stage.target[1] == -7.5, "solo stops the driver and frames your board")

-- Button captions carry no key letters in front (user request 2026-10-15): "VS JEV", not "V   VS JEV".
for title in pairs(button_titles) do
    for _, prefix in ipairs({ "V", "R", "M", "P", "II", "ENTER" }) do
        assert(not title:find("^" .. prefix .. "%s"), "button caption starts with a key: " .. title)
    end
end
assert(button_titles["VS JEV"] and button_titles["SOUND ON"] and button_titles["MENU"], "plain captions drawn")
-- A key command is never killed or replaced: not by a race driver's leftover stop timer, a second paste or R.
key("P")
key("V")
assert(prism.state.versus and driver.running, "a race from the menu")
prism.game.game_over = true
callback()
assert(prism.state.mode == "over" and driver.running, "the race ended; its driver is still finishing")
frames(60)
clicks.key = true
callback()
assert(prism.state.panel == "key", "the key panel opens after the race")
answer("KEY saved")
clicks.paste = true
callback()
local key_runs = driver.started
local key_kills = driver.killed
local key_stops = driver.stopped
clicks.paste = true
callback()
key("R")
key("P")
frames(240)
assert(driver.running and driver.started == key_runs, "a running key save is never replaced")
assert(driver.killed == key_kills and driver.stopped == key_stops, "a running key save is never killed")
assert(prism.state.panel == "key" and prism.state.mode == "over", "R and P do nothing in the key panel")

-- Even a direct race start (tooling) refuses while the key is being saved.
prism.versus()
assert(driver.killed == key_kills and driver.running, "a race never starts over a running key save")
assert(prism.state.mode == "over" and prism.race.reason:find("still being saved"), "the race says why")
clicks.key = true
callback()
answer("KEY saved")

-- A race driver that exits by itself leaves no stop timer behind to kill a later key command.
clicks.back = true
callback()
key("V")
prism.game.game_over = true
callback()
assert(prism.state.mode == "over" and driver.running, "a second race ended; its driver is finishing")
driver.lines = { "Finished: decisions=1 redecides=0 api_calls=1 input_tokens=10 output_tokens=0 terminal=true" }
driver.running = false
callback()
clicks.key = true
callback()
answer("KEY saved")
clicks.paste = true
callback()
local late_stops = driver.stopped
frames(240)
assert(driver.running and driver.stopped == late_stops, "a key save after a finished race runs to its end")
answer("KEY saved")

-- A key command that ends without answering reads as an error, not as "Checking..." forever.
clicks.remove = true
callback()
driver.lines = {}
driver.running = false
callback()
assert(widgets.modal_body.body:find("could not"), "a silent key command reports an error")
clicks.back = true
callback()
assert(engine3d.created_while_frozen == 0, "play creates no 3D nodes or new material properties after load")
print("PASS PRISM entry: controls, pause, hold, score persistence, FX, resize and idle UI reuse")
