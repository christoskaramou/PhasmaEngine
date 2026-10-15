local root = arg[0]:gsub("\\", "/"):match("^(.*)/tests/[^/]+$") or "Games/PrismTetris"
dofile(root .. "/tests/entry_test.lua")

local function choose(observation)
    local best
    for _, action in ipairs(observation.actions) do
        if not best or action.score > best.score then
            best = action
        end
    end
    return assert(best, "Expected reachable placements")
end

local function find(observation, pattern)
    for _, action in ipairs(observation.actions) do
        if action.description:find(pattern) then
            return action
        end
    end
end

local function request(observation, action)
    local result = prism.agent:act({
        session_id = observation.session_id,
        state_id = observation.state_id,
        action_id = action.id,
    })
    assert(result.ok ~= false, result.error and result.error.message)
    return result
end

-- Ticks 60 Hz frames until the running placement finishes; returns how many it took.
local function settle()
    local frames = 0
    while prism.agent:observe().busy do
        script.tick()
        frames = frames + 1
        assert(frames < 600, "a placement never finished")
    end
    return frames
end

local function act(observation, action)
    local result = request(observation, action)
    assert(result.busy, "a placement plays out over time, not at once")
    settle()
    return prism.agent:observe()
end

local function hold_key(name, frames)
    local physical = input.is_key_down_raw
    input.is_key_down_raw = function(key)
        return key == name or physical(key)
    end
    for _ = 1, frames do
        script.tick()
    end
    input.is_key_down_raw = physical
end

local started = os.clock()
local saved_best = prism.state.best
assert(prism.agent:describe().state_tracking == "game_epoch", "drivers learn the game is real-time")
local observation = prism.agent:reset({ seed = 42 })
assert(observation.mode == "playing" and prism.state.agent)
assert(#observation.observation.board == 20 and #observation.observation.next == 5)
assert(#observation.actions >= 9 and #observation.actions <= 80)

-- Real gravity: the piece keeps falling while the agent decides, and falling does not invalidate the decision.
local first = choose(observation)
local old_session = observation.session_id
local old_state = observation.state_id
local initial_y = prism.game.active.y
for _ = 1, 72 do
    script.tick()
end
local waiting = prism.agent:observe()
assert(prism.game.active.y > initial_y, "gravity keeps running while the agent decides")
assert(waiting.state_id == old_state and #waiting.actions == #observation.actions, "falling keeps the decision")

observation = act(waiting, first)
local stale = prism.agent:act({
    session_id = old_session,
    state_id = old_state,
    action_id = first.id,
})
assert(stale.ok == false and stale.error.code == "stale_state")

-- Inputs arrive at a human pace: at least 0.07 s (four 60 Hz frames) apart.
local far = assert(find(observation, "^Place %a at column 1,"), "a far-left placement is offered")
request(observation, far)
local last_x = prism.game.active.x
local last_change
for frame = 1, 240 do
    script.tick()
    if prism.game.active.x ~= last_x then
        assert(not last_change or frame - last_change >= 4, "inputs are spaced by 0.07 s")
        last_x = prism.game.active.x
        last_change = frame
    end

    if not prism.agent:observe().busy then
        break
    end
end
assert(last_change and not prism.agent:observe().busy, "the far-left placement finished")
observation = prism.agent:observe()

-- Pausing freezes the placement and gravity; resuming finishes it.
request(observation, assert(find(observation, "^Place %a at column 1,")))
script.tick()
prism.state.mode = "paused"
local paused_x = prism.game.active.x
local paused_y = prism.game.active.y
for _ = 1, 60 do
    script.tick()
end
assert(prism.game.active.x == paused_x and prism.game.active.y == paused_y, "a paused game does not move")
assert(prism.agent:observe().busy, "the paused placement is still pending")
prism.state.mode = "playing"
settle()
observation = prism.agent:observe()

-- A hold placement holds first, then lands where it was planned.
local hold = assert(find(observation, "^Hold then place"), "hold placements are offered")
observation = act(observation, hold)
assert(prism.game.held and prism.game.can_hold, "the hold placement held a piece and then locked")

-- A landing that becomes unreachable mid-route ends the placement: inputs stop, gravity takes over.
observation = prism.agent:reset({ seed = 42 })
request(observation, assert(find(observation, "^Place %a at column 1,")))
script.tick()
local piece = prism.game.active
for y = prism.game.height - 3, prism.game.height do
    for x = 1, 3 do
        prism.game.board[y][x] = "J"
    end
end
settle()
local stuck_x = piece.x
local stuck_y = piece.y
for _ = 1, 72 do
    script.tick()
end
assert(prism.game.active == piece and piece.x == stuck_x, "an abandoned placement stops its inputs")
assert(piece.y > stuck_y, "an abandoned placement keeps falling under gravity")

-- A piece that locks before its placement finishes ends it; the next piece is not steered to the old landing.
observation = prism.agent:reset({ seed = 42 })
request(observation, assert(find(observation, "^Place %a at column 1,")))
script.tick()
prism.game.next[1] = prism.game.active.kind
prism.game:hard_drop()
assert(not prism.agent:observe().busy, "a lock ends the running placement")
local spawned = prism.game.active
for _ = 1, 30 do
    script.tick()
end
assert(spawned.x == 4, "the next piece is not steered to the old landing")

-- A human key during a placement takes over at once.
observation = prism.agent:reset({ seed = 42 })
request(observation, assert(find(observation, "^Place %a at column 1,")))
hold_key("Right", 1)
assert(not prism.state.agent and not prism.agent:observe().busy, "a human key ends the agent's placement")
local human_x = prism.game.active.x
for _ = 1, 30 do
    script.tick()
end
assert(prism.game.active.x == human_x, "the agent's inputs stopped after the takeover")

-- The human keeps the game: the driver gets no placements, on this piece or the next, until it resets.
local taken = prism.agent:observe()
assert(#taken.actions == 0, "a human takeover leaves the driver nothing to play")
local refused = prism.agent:act({
    session_id = taken.session_id,
    state_id = taken.state_id,
    action_id = "placement_1",
})
assert(refused.ok == false and refused.error.code == "illegal_action", "the driver cannot take the piece back")
assert(not prism.state.agent, "the human still owns the game")
assert(prism.apply("drop"))
assert(#prism.agent:observe().actions == 0, "nor the next piece")

-- The local baseline still plays a seeded 50-piece game under real gravity.
observation = prism.agent:reset({ seed = 42 })
for _ = 1, 50 do
    observation = act(observation, choose(observation))
    assert(not observation.terminal, "Local baseline should survive this seeded 50-piece run")
end
assert(observation.score > 0 and prism.game.lines > 0)
local score = observation.score
local lines = prism.game.lines
local persisted_best = fs.read("Save/prism_best.txt")
local original_x = prism.game.active.x
hold_key("Left", 1)
assert(prism.game.active.x == original_x - 1, "Physical input must move through the shared controller")
assert(not prism.state.agent and prism.state.assisted, "Human override must preserve assisted-run provenance")
local resumed_y = prism.game.active.y
for _ = 1, 100 do
    script.tick()
end
assert(prism.game.active.y > resumed_y, "Human override must resume normal gravity")
assert(
    prism.state.best == saved_best and fs.read("Save/prism_best.txt") == persisted_best,
    "Human override must not save an assisted run as a human high score"
)
stale = prism.agent:act({
    session_id = observation.session_id,
    state_id = observation.state_id,
    action_id = observation.actions[1].id,
})
assert(stale.ok == false and stale.error.code == "stale_state", "Human input must invalidate the old decision")

prism.restart(42)
assert(
    not prism.state.assisted and prism.state.best == saved_best,
    "Manual restart must remember the old run before clearing assisted provenance"
)
assert(#prism.agent:observe().actions == 0, "New Game starts a manual run the driver cannot join")
for _ = 1, 3 do
    assert(prism.apply("drop"))
end
assert(
    prism.game.score > saved_best and prism.state.best == prism.game.score,
    "A fresh human run must still update its high score"
)
assert(tonumber(fs.read("Save/prism_best.txt")) == prism.state.best)
saved_best = prism.state.best

local again = prism.agent:reset({ seed = 42 })
assert(again.session_id ~= old_session and choose(again).description == first.description)
assert(again.score == 0 and prism.game.lines == 0)

-- Every advertised placement must play out through the same controller as keyboard input.
for _, candidate in ipairs(again.actions) do
    local fresh = prism.agent:reset({ seed = 42 })
    local selected
    for _, action in ipairs(fresh.actions) do
        if action.description == candidate.description then
            selected = action
            break
        end
    end

    assert(selected)
    local placed = act(fresh, selected)
    assert(placed.score > 0 and not placed.terminal, "placement " .. candidate.description .. " landed")
end
assert(prism.state.best == saved_best, "Automated scores must not replace the human high score")

-- In a race the adapter plays Jev's board: your keys never stop its placement, and your top-out ends it.
prism.versus()
local race = prism.agent:observe()
assert(not race.terminal and race.observation.active == prism.rival.active, "a race hands Jev its own board")
local jev_piece = prism.rival.active
request(race, assert(find(race, "^Place %a at column 1,")))
hold_key("Right", 1)
assert(prism.state.agent and prism.agent:observe().busy, "your key in a race leaves Jev's placement running")
settle()
assert(prism.rival.active ~= jev_piece, "Jev's placement landed on Jev's board")
prism.game.game_over = true
script.tick()
assert(prism.state.mode == "over" and prism.agent:observe().terminal, "your top-out is terminal for Jev")
prism.restart(42)
assert(not prism.state.versus and prism.state.best == saved_best, "back to solo; the race saved no best score")
print(string.format(
    "PASS PRISM agent: real gravity, timed inputs, pause, hold, unreachable targets, takeover, stale requests "
        .. "and provenance; local policy 50 pieces, %d lines, %d score (%.3fs)",
    lines,
    score,
    os.clock() - started
))
