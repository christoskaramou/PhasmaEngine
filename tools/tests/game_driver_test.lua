local gamekit = "Phasma/Runtime/RuntimeAssets/Scripts/gamekit/"
local driver = assert(loadfile(gamekit .. "driver.lua"))()
driver._json = assert(loadfile(gamekit .. "json.lua"))()

local registered = {}
mcp = {
    register_tool = function(definition)
        registered[definition.name] = definition
    end,
    unregister_tool = function(name)
        registered[name] = nil
    end,
}

local x, moves, resets, busy, terminal = 0, 0, 0, false, false
local bad_actions, fail_action = false, false
local function observe()
    local actions = terminal and {} or { { id = "right", description = "Move right" } }
    if bad_actions then
        actions[2] = { id = "right", description = "Duplicate" }
    end
    return {
        mode = terminal and "over" or "play",
        terminal = terminal,
        score = x,
        busy = busy,
        observation = { x = x },
        actions = actions,
    }
end

local function spec()
    return {
        id = "test",
        description = "Deterministic driver test",
        observe = observe,
        act = function(action)
            assert(action == "right")
            moves = moves + 1
            x = x + 1
            if fail_action then
                error("action callback failed after changing state")
            end
        end,
        reset = function(seed)
            resets = resets + 1
            x = seed
            terminal = false
        end,
    }
end

local controller = driver.attach(spec())
for _, name in ipairs({ "game_describe", "game_observe", "game_act", "game_reset" }) do
    assert(registered[name], name .. " was not registered")
end
assert(controller:describe().protocol_version == 1)

local function action(state, id)
    return { session_id = state.session_id, state_id = state.state_id, action_id = id or "right" }
end

local function rejected(result, code)
    assert(result.ok == false and result.error.code == code, driver._json.encode(result))
end

local initial = controller:observe()
assert(initial.state_id == 1 and initial.score == 0)
assert(controller:observe().state_id == initial.state_id)
rejected(controller:act({}), "invalid_request")
rejected(controller:act(false), "invalid_request")
rejected(controller:act({ session_id = initial.session_id, state_id = 1.5, action_id = "right" }), "invalid_request")
rejected(controller:act({ session_id = initial.session_id, state_id = 1, action_id = string.rep("x", 129) }), "invalid_request")
rejected(controller:act(action(initial, "execute_lua")), "illegal_action")
assert(moves == 0)

local after = controller:act(action(initial))
assert(moves == 1 and after.score == 1 and after.state_id > initial.state_id)
rejected(controller:act(action(initial)), "stale_state")
assert(moves == 1)

x = 5
rejected(controller:act(action(after)), "stale_state")
busy = true
rejected(controller:act(action(controller:observe())), "busy")
busy = false
bad_actions = true
rejected(controller:observe(), "invalid_observation")
bad_actions = false

terminal = true
local wire = registered.game_observe.handler({})
assert(type(wire) == "string" and wire:find('"actions":%[%]'), wire)
assert(#controller:observe().actions == 0)
rejected(controller:act(action(controller:observe())), "illegal_action")
local old_session = controller:observe().session_id
rejected(controller:reset({ seed = -1 }), "invalid_request")
rejected(controller:reset({ seed = 2147483648 }), "invalid_request")
rejected(controller:reset({ seed = 3.1 }), "invalid_request")
rejected(controller:reset({ seed = 0, extra = true }), "invalid_request")
assert(resets == 0)
local reset = controller:reset({ seed = 7 })
assert(resets == 1 and reset.score == 7 and reset.session_id ~= old_session and reset.state_id == 1)
rejected(controller:act({ session_id = old_session, state_id = 1, action_id = "right" }), "stale_state")

fail_action = true
rejected(controller:act(action(reset)), "callback_failed")
rejected(controller:act(action(reset)), "stale_state")
assert(moves == 2)
fail_action = false

local replacement = driver.attach(spec())
controller:detach()
assert(registered.game_observe, "old cleanup removed the replacement's tools")
rejected(controller:act(action(controller:observe())), "detached")
replacement:detach()
assert(next(registered) == nil and _G.__gamekit_active_driver == nil)
replacement:attach()
assert(registered.game_act)
replacement:detach()

local logical_spec = spec()
local visual_position = 0
logical_spec.observe = function()
    local state = observe()
    state.observation.position = visual_position
    return state
end
logical_spec.state_key = function(state)
    return { hp = state.score, busy = state.busy }
end
local logical = driver.attach(logical_spec)
local epoch = logical:observe()
visual_position = 100
assert(logical:observe().state_id == epoch.state_id)
assert(logical:act(action(epoch)).score == 9)
x = 30
assert(logical:observe().state_id > epoch.state_id)
logical:detach()

local physical = {}
input = { is_key_down = function(key) return physical[key] == true end }
vec2 = function(a, b) return { x = a, y = b } end
local controls = assert(loadfile(gamekit .. "input.lua"))()
controls.bind("move", { up = "W", down = "S", left = "A", right = "D" })
controls.bind("fire", { keys = { "Space" } })
controls.inject("move", { x = 1, y = 0 }, 2)
for _ = 1, 2 do
    controls.update(1 / 60)
    assert(controls.axis2("move").x == 1 and controls.injected("move"))
end
controls.update(1 / 60)
assert(controls.axis2("move").x == 0 and not controls.injected("move"))
physical.A = true
assert(controls.axis2("move").x == -1)
controls.inject("move", { x = 1, y = 0 }, 2)
assert(controls.axis2("move").x == 0, "physical and agent movement must share the action map")
controls.clear_injected()
assert(controls.axis2("move").x == -1)
controls.inject("fire", true, 1)
controls.update(1 / 60)
assert(controls.down("fire") and controls.pressed("fire"))
controls.clear_injected()
controls.inject("fire", true, 1)
controls.update(1 / 60)
assert(controls.pressed("fire"), "clearing a macro must allow a fresh button edge")
controls.update(1 / 60)
assert(not controls.down("fire") and not controls.pressed("fire"))
assert(not pcall(controls.inject, "move", { x = 2, y = 0 }, 1))
assert(not pcall(controls.inject, "move", { x = 0, y = 0 }, 0))
assert(not pcall(controls.inject, "move", { x = 0, y = 0 }, 601))
assert(not pcall(controls.inject, "missing", true, 1))
print("GameKit driver/input regression checks passed")
