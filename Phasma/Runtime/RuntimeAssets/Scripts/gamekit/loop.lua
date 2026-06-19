-- gamekit/loop.lua — the single per-frame tick.
--
-- gamekit owns ONE script.on_update callback ("play" mode). Every system (timer,
-- input edge state, wave logic, player control, camera follow, tween) registers a
-- function here and the loop fans them out each frame in a stable phase order:
--   pre  -> timer, input          (state that later systems read)
--   main -> waves, actors, gameplay (added by the game's entry script)
--   post -> camera follow, tween   (react to the new positions)
-- dt is seconds, clamped so a load/alt-tab spike can't teleport everything.

local loop = {
    _buckets = { pre = {}, main = {}, post = {} },
    _dt = 0.0,
    _started = false,
    _id = "gamekit",
}

-- Register fn(dt) into a phase ("pre" | "main" | "post"; default "main").
-- Returns a handle table you can later pass to loop.remove().
function loop.add(fn, phase)
    if type(fn) ~= "function" then return nil end
    phase = phase or "main"
    local bucket = loop._buckets[phase] or loop._buckets.main
    local handle = { fn = fn, phase = phase }
    bucket[#bucket + 1] = handle
    return handle
end

function loop.remove(handle)
    if type(handle) ~= "table" then return end
    local bucket = loop._buckets[handle.phase]
    if not bucket then return end
    for i = #bucket, 1, -1 do
        if bucket[i] == handle then
            table.remove(bucket, i)
            return
        end
    end
end

-- Seconds since the previous tick (valid inside loop callbacks).
function loop.dt()
    return loop._dt
end

local PHASES = { "pre", "main", "post" }

-- The C++-registered callback. Computes dt, dispatches each bucket in order.
function loop.tick()
    local metrics = engine and engine.get_metrics and engine.get_metrics()
    local dt = (metrics and metrics.delta_ms or 16.6) / 1000.0
    if dt < 0.0 then dt = 0.0 end
    if dt > 0.1 then dt = 0.1 end -- clamp big frame gaps (scene load, alt-tab)
    loop._dt = dt

    for _, phase in ipairs(PHASES) do
        local bucket = loop._buckets[phase]
        -- snapshot length so systems added mid-tick run next frame, not this one
        local n = #bucket
        for i = 1, n do
            local handle = bucket[i]
            if handle and handle.fn then handle.fn(dt) end
        end
    end
end

-- Register the single update with the engine. Idempotent: re-registering the same
-- id just replaces the callback, so a Play -> Stop -> Play cycle is safe.
function loop.start(id)
    loop._id = id or loop._id
    loop._started = true
    if script and script.on_update then
        script.on_update(loop._id, loop.tick, "play")
    end
end

function loop.stop()
    loop._started = false
    if script and script.remove_update then
        script.remove_update(loop._id)
    end
end

return loop
