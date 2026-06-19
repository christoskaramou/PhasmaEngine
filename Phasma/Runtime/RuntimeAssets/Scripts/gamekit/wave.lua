-- gamekit/wave.lua — spawn members from a pool over time, detect "cleared".
--
-- new{ pool, points, count, interval, on_spawn } drips `count` members out of the
-- pool, one every `interval` seconds, each placed at a random point from `points`
-- (a list of {x,y,z}). The game releases members back to the pool when they die;
-- once all are spawned and the pool has no active members, on_clear(fn) fires.
-- :update(dt) is driven by the loop's "main" phase.

local Wave = {}
Wave.__index = Wave

function Wave.new(cfg)
    cfg = cfg or {}
    return setmetatable({
        pool = cfg.pool,
        points = cfg.points or {},
        count = cfg.count or 8,
        interval = cfg.interval or 1.0,
        on_spawn = cfg.on_spawn,
        _spawned = 0,
        _timer = 0.0,
        _running = false,
        _cleared_cb = nil,
        _did_clear = false,
    }, Wave)
end

function Wave:start()
    self._running = true
    self._spawned = 0
    self._timer = 0.0
    self._did_clear = false
    return self
end

function Wave:on_clear(fn)
    self._cleared_cb = fn
    return self
end

function Wave:_spawn_one()
    if not self.pool then return end
    local node = self.pool:acquire()
    if not node then return end -- pool exhausted; try again next interval
    local p = { x = 0.0, y = 0.0, z = 0.0 }
    if #self.points > 0 then p = self.points[math.random(1, #self.points)] end
    if node.set_position then node:set_position(vec3(p.x or 0.0, p.y or 0.0, p.z or 0.0)) end
    self._spawned = self._spawned + 1
    if self.on_spawn then self.on_spawn(node) end
end

function Wave:update(dt)
    if not self._running then return end
    if self._spawned < self.count then
        self._timer = self._timer + (dt or 0.0)
        while self._timer >= self.interval and self._spawned < self.count do
            self._timer = self._timer - self.interval
            self:_spawn_one()
        end
    elseif not self._did_clear and self.pool and self.pool:active_count() == 0 then
        self._did_clear = true
        self._running = false
        if self._cleared_cb then self._cleared_cb(self) end
    end
end

function Wave:spawned() return self._spawned end
function Wave:running() return self._running end

return Wave
