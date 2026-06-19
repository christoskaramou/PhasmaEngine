-- gamekit/grid.lua — cell <-> world mapping for board / isometric games.
--
-- A grid of cols x rows cells of `cell` units, anchored at `origin` (a {x,y,z}).
-- cell_to_world returns the world centre of a cell; world_to_cell inverts it.
-- (Not exercised by the topdown smoke; ships for the iso/card fast-follow.)

local Grid = {}
Grid.__index = Grid

function Grid.new(cfg)
    cfg = cfg or {}
    return setmetatable({
        cols = cfg.cols or 8,
        rows = cfg.rows or 8,
        cell = cfg.cell or 1.0,
        origin = cfg.origin or { x = 0.0, y = 0.0, z = 0.0 },
    }, Grid)
end

function Grid:cell_to_world(cx, cy)
    local o = self.origin
    return vec3(
        (o.x or 0.0) + (cx + 0.5) * self.cell,
        (o.y or 0.0),
        (o.z or 0.0) + (cy + 0.5) * self.cell)
end

function Grid:world_to_cell(pos)
    local o = self.origin
    local cx = math.floor(((pos.x or 0.0) - (o.x or 0.0)) / self.cell)
    local cy = math.floor(((pos.z or 0.0) - (o.z or 0.0)) / self.cell)
    return cx, cy
end

function Grid:in_bounds(cx, cy)
    return cx >= 0 and cy >= 0 and cx < self.cols and cy < self.rows
end

function Grid:neighbors(cx, cy, diagonal)
    local dirs = diagonal
        and { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 }, { 1, 1 }, { 1, -1 }, { -1, 1 }, { -1, -1 } }
        or { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } }
    local out = {}
    for _, d in ipairs(dirs) do
        local nx, ny = cx + d[1], cy + d[2]
        if self:in_bounds(nx, ny) then out[#out + 1] = { x = nx, y = ny } end
    end
    return out
end

return Grid
