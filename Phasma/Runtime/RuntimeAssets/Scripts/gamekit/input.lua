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
    return vec2(axis(spec.x), axis(spec.y))
end

-- Recompute pressed-edges. Called by the loop in the "pre" phase each frame.
function input_map.update(_dt)
    for action, spec in pairs(input_map._actions) do
        local now = false
        if spec.keys then
            for _, k in ipairs(spec.keys) do
                if raw_down(k) then now = true; break end
            end
        end
        input_map._pressed[action] = now and not input_map._down_last[action]
        input_map._down_last[action] = now
    end
end

return input_map
