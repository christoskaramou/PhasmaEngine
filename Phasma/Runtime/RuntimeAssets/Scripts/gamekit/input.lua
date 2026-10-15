-- gamekit/input.lua — action maps over the raw key bindings.
--
-- The engine only exposes raw is_key_down(name). gamekit adds named actions so a
-- game says input.down("fire") / input.axis2("move") instead of hard-coding keys.
-- Edge detection (pressed) is computed once per frame in input.update(dt), which
-- the loop runs in the "pre" phase before gameplay reads it.
--
-- bind() spec forms:
--   { keys = { "Space", "J" } }                 -- button action (down/pressed)
--   { x = { neg = "A", pos = "D" },             -- 2D axis action (axis2)
--     y = { neg = "W", pos = "S" } }
--   { up = "W", down = "S", left = "A", right = "D" }  -- axis shorthand
-- For axis shorthand, up maps to y=-1 and down to y=+1, so a topdown "forward"
-- (W) yields a -Z move when the game feeds axis2 into world Z.

local input_map = {
    _actions = {},
    _down_last = {},
    _pressed = {},
    _injected = {},
}

local function raw_down(key)
    return key ~= nil and input and input.is_key_down and input.is_key_down(key)
end

function input_map.bind(action, spec)
    spec = spec or {}
    -- normalize axis shorthand into x/y
    if spec.up or spec.down or spec.left or spec.right then
        spec.y = spec.y or { neg = spec.up, pos = spec.down }
        spec.x = spec.x or { neg = spec.left, pos = spec.right }
    end
    input_map._actions[action] = spec
    return input_map
end

-- True while any key bound to the action is held.
function input_map.down(action)
    local injected = input_map._injected[action]
    if injected and injected.value == true then
        return true
    end

    local spec = input_map._actions[action]
    if not spec then return false end
    if spec.keys then
        for _, k in ipairs(spec.keys) do
            if raw_down(k) then return true end
        end
    end
    return false
end

-- True only on the frame the action transitions from up to down.
function input_map.pressed(action)
    return input_map._pressed[action] == true
end

local function axis(component)
    local v = 0.0
    if component then
        if raw_down(component.pos) then v = v + 1.0 end
        if raw_down(component.neg) then v = v - 1.0 end
    end
    return v
end

-- 2D axis from an action's x/y components, each in [-1, 1].
function input_map.axis2(action)
    local spec = input_map._actions[action]
    if not spec then return vec2(0.0, 0.0) end

    local injected = input_map._injected[action]
    local value = injected and injected.value
    local x = axis(spec.x) + (type(value) == "table" and value.x or 0)
    local y = axis(spec.y) + (type(value) == "table" and value.y or 0)

    return vec2(
        math.max(-1, math.min(1, x)),
        math.max(-1, math.min(1, y))
    )
end

-- Bounded named input shares the physical controller and releases automatically.
function input_map.inject(action, value, frames)
    assert(input_map._actions[action], "inject requires a bound action")
    assert(type(frames) == "number" and frames % 1 == 0 and frames >= 1 and frames <= 600,
        "injected input duration must be 1..600 frames")

    local spec = input_map._actions[action]

    if spec.keys then
        assert(type(value) == "boolean", "button injection requires a boolean")
    else
        assert(type(value) == "table", "axis injection requires x and y")

        for _, component in ipairs({ "x", "y" }) do
            local number = value[component]
            assert(type(number) == "number" and number >= -1 and number <= 1,
                "injected axis components must be in -1..1")
        end

        value = { x = value.x, y = value.y }
    end

    input_map._injected[action] = { value = value, frames = frames }
end

function input_map.injected(action)
    return input_map._injected[action] ~= nil
end

function input_map.clear_injected()
    local previous = input_map._injected
    input_map._injected = {}

    for action in pairs(previous) do
        input_map._down_last[action] = input_map.down(action)
        input_map._pressed[action] = false
    end
end

-- Recompute pressed-edges. Called by the loop in the "pre" phase each frame.
function input_map.update(_dt)
    for action, injected in pairs(input_map._injected) do
        if injected.frames <= 0 then
            input_map._injected[action] = nil
        else
            injected.frames = injected.frames - 1
        end
    end

    for action in pairs(input_map._actions) do
        local now = input_map.down(action)
        input_map._pressed[action] = now and not input_map._down_last[action]
        input_map._down_last[action] = now
    end
end

return input_map
