-- gamekit/tween.lua — interpolate numeric fields of a table over time.
--
-- to(target, fields, dur, ease) eases each named numeric field of `target` from its
-- current value to the given end value over `dur` seconds. Returns a handle; set
-- handle.on_done = fn for a completion callback. tween.update runs in the loop's
-- "post" phase. Works on any table of numbers (HUD alpha, a counter, a {x,y,z}).

local tween = { _items = {} }

local EASES = {
    linear = function(t) return t end,
    in_quad = function(t) return t * t end,
    out_quad = function(t) return 1.0 - (1.0 - t) * (1.0 - t) end,
    in_out_quad = function(t)
        if t < 0.5 then return 2.0 * t * t end
        return 1.0 - ((-2.0 * t + 2.0) ^ 2) / 2.0
    end,
    out_back = function(t)
        local c1, c3 = 1.70158, 2.70158
        return 1.0 + c3 * (t - 1.0) ^ 3 + c1 * (t - 1.0) ^ 2
    end,
}

function tween.to(target, fields, dur, ease)
    local from = {}
    for k, _ in pairs(fields) do from[k] = target[k] or 0.0 end
    local item = {
        target = target,
        from = from,
        to = fields,
        dur = math.max(1e-4, dur or 0.0),
        t = 0.0,
        ease = EASES[ease or "linear"] or EASES.linear,
        on_done = nil,
    }
    tween._items[#tween._items + 1] = item
    return item
end

function tween.update(dt)
    for i = #tween._items, 1, -1 do
        local it = tween._items[i]
        it.t = it.t + (dt or 0.0)
        local p = math.min(1.0, it.t / it.dur)
        local e = it.ease(p)
        for k, target_v in pairs(it.to) do
            it.target[k] = it.from[k] + (target_v - it.from[k]) * e
        end
        if p >= 1.0 then
            table.remove(tween._items, i)
            if it.on_done then it.on_done() end
        end
    end
end

function tween.clear()
    tween._items = {}
end

return tween
