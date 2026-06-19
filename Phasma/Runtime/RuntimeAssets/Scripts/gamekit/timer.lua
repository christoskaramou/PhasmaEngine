-- gamekit/timer.lua — scheduled callbacks on the gamekit clock.
--
-- after(s, fn) fires once after s seconds; every(s, fn) fires repeatedly. Time is
-- accumulated from the loop dt (timer.update runs in the "pre" phase), so timers
-- honour pause/play and never use wall-clock. Handles returned by after/every can
-- be cancelled.

local timer = {
    _items = {},
    _next = 1,
    _now = 0.0,
}

function timer.after(seconds, fn)
    local h = timer._next
    timer._next = timer._next + 1
    timer._items[h] = { at = timer._now + (seconds or 0.0), fn = fn, every = nil }
    return h
end

function timer.every(seconds, fn)
    local h = timer._next
    timer._next = timer._next + 1
    seconds = math.max(1e-4, seconds or 1.0)
    timer._items[h] = { at = timer._now + seconds, fn = fn, every = seconds }
    return h
end

function timer.cancel(handle)
    if handle ~= nil then timer._items[handle] = nil end
end

function timer.clear()
    timer._items = {}
end

function timer.update(dt)
    timer._now = timer._now + (dt or 0.0)
    -- snapshot due handles first; a callback may add/cancel timers
    local due = {}
    for h, item in pairs(timer._items) do
        if timer._now >= item.at then due[#due + 1] = h end
    end
    for _, h in ipairs(due) do
        local item = timer._items[h]
        if item then
            if item.every then
                item.at = item.at + item.every
                if item.fn then item.fn() end
            else
                timer._items[h] = nil
                if item.fn then item.fn() end
            end
        end
    end
end

return timer
