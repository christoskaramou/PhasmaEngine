local Rules = {}
local State = {}
State.__index = State

local kinds = {"I", "O", "T", "S", "Z", "J", "L"}
local shapes = {
    I = {{0, 1}, {1, 1}, {2, 1}, {3, 1}},
    O = {{1, 0}, {2, 0}, {1, 1}, {2, 1}},
    T = {{1, 0}, {0, 1}, {1, 1}, {2, 1}},
    S = {{1, 0}, {2, 0}, {0, 1}, {1, 1}},
    Z = {{0, 0}, {1, 0}, {1, 1}, {2, 1}},
    J = {{0, 0}, {0, 1}, {1, 1}, {2, 1}},
    L = {{2, 0}, {0, 1}, {1, 1}, {2, 1}}
}

local rotations = {}
for kind, cells in pairs(shapes) do
    rotations[kind] = {[0] = cells}
    for rotation = 1, 3 do
        local turned = {}
        for i, cell in ipairs(rotations[kind][rotation - 1]) do
            if kind == "O" then
                turned[i] = {cell[1], cell[2]}
            else
                local extent = kind == "I" and 3 or 2
                turned[i] = {extent - cell[2], cell[1]}
            end
        end
        rotations[kind][rotation] = turned
    end
end

-- SRS offsets use screen coordinates, so positive y points down.
local kicks = {
    ["0:1"] = {{0, 0}, {-1, 0}, {-1, -1}, {0, 2}, {-1, 2}},
    ["1:0"] = {{0, 0}, {1, 0}, {1, 1}, {0, -2}, {1, -2}},
    ["1:2"] = {{0, 0}, {1, 0}, {1, 1}, {0, -2}, {1, -2}},
    ["2:1"] = {{0, 0}, {-1, 0}, {-1, -1}, {0, 2}, {-1, 2}},
    ["2:3"] = {{0, 0}, {1, 0}, {1, -1}, {0, 2}, {1, 2}},
    ["3:2"] = {{0, 0}, {-1, 0}, {-1, 1}, {0, -2}, {-1, -2}},
    ["3:0"] = {{0, 0}, {-1, 0}, {-1, 1}, {0, -2}, {-1, -2}},
    ["0:3"] = {{0, 0}, {1, 0}, {1, -1}, {0, 2}, {1, 2}}
}

local i_kicks = {
    ["0:1"] = {{0, 0}, {-2, 0}, {1, 0}, {-2, 1}, {1, -2}},
    ["1:0"] = {{0, 0}, {2, 0}, {-1, 0}, {2, -1}, {-1, 2}},
    ["1:2"] = {{0, 0}, {-1, 0}, {2, 0}, {-1, -2}, {2, 1}},
    ["2:1"] = {{0, 0}, {1, 0}, {-2, 0}, {1, 2}, {-2, -1}},
    ["2:3"] = {{0, 0}, {2, 0}, {-1, 0}, {2, -1}, {-1, 2}},
    ["3:2"] = {{0, 0}, {-2, 0}, {1, 0}, {-2, 1}, {1, -2}},
    ["3:0"] = {{0, 0}, {1, 0}, {-2, 0}, {1, 2}, {-2, -1}},
    ["0:3"] = {{0, 0}, {-1, 0}, {2, 0}, {-1, -2}, {2, 1}}
}

function Rules.cells(kind, rotation)
    return rotations[kind][rotation % 4]
end

local function empty_row(width)
    local row = {}
    for x = 1, width do
        row[x] = false
    end
    return row
end

local function random_index(state, count)
    state.random_seed = state.random_seed * 48271 % 2147483647
    return state.random_seed % count + 1
end

local function draw_kind(state)
    if #state.bag == 0 then
        for i, kind in ipairs(kinds) do
            state.bag[i] = kind
        end
        for i = #state.bag, 2, -1 do
            local j = random_index(state, i)
            state.bag[i], state.bag[j] = state.bag[j], state.bag[i]
        end
    end
    return table.remove(state.bag)
end

local function fits(state, kind, rotation, origin_x, origin_y)
    for _, cell in ipairs(Rules.cells(kind, rotation)) do
        local x = origin_x + cell[1]
        local y = origin_y + cell[2]
        if x < 1 or x > state.width or y < 1 or y > state.height then
            return false
        end
        if state.board[y][x] then
            return false
        end
    end
    return true
end

local function grounded(state)
    local piece = state.active
    return not fits(state, piece.kind, piece.rotation, piece.x, piece.y + 1)
end

local function spawn(state, kind)
    if not kind then
        kind = table.remove(state.next, 1)
        state.next[#state.next + 1] = draw_kind(state)
    end

    state.active = {kind = kind, rotation = 0, x = 4, y = 1}
    state.gravity_timer = 0
    state.lock_timer = 0
    state.lock_resets = 0
    state.game_over = not fits(state, kind, 0, 4, 1)
end

function State:restart(seed)
    local initial_seed = seed or self.initial_seed or 731
    self.initial_seed = math.max(1, math.floor(math.abs(initial_seed)) % 2147483647)
    self.random_seed = self.initial_seed
    self.width = 10
    self.height = 22
    self.hidden = 2
    self.board = {}
    self.bag = {}
    self.next = {}
    self.held = nil
    self.can_hold = true
    self.score = 0
    self.lines = 0
    self.level = 1
    self.combo = -1
    self.back_to_back = false
    self.game_over = false

    for y = 1, self.height do
        self.board[y] = empty_row(self.width)
    end
    for i = 1, 5 do
        self.next[i] = draw_kind(self)
    end
    spawn(self)
    return self
end

function Rules.new(seed)
    return setmetatable({}, State):restart(seed)
end

local function reset_lock(state, was_grounded)
    if was_grounded and state.lock_resets < 15 then
        state.lock_timer = 0
        state.lock_resets = state.lock_resets + 1
    end
end

function State:move(dx)
    if self.game_over then
        return false
    end

    local piece = self.active
    if not fits(self, piece.kind, piece.rotation, piece.x + dx, piece.y) then
        return false
    end

    local was_grounded = grounded(self)
    piece.x = piece.x + dx
    reset_lock(self, was_grounded)
    return true
end

function State:rotate(dir)
    if self.game_over then
        return false
    end

    local piece = self.active
    local rotation = (piece.rotation + (dir < 0 and -1 or 1)) % 4
    local offsets = piece.kind == "I" and i_kicks or kicks
    local was_grounded = grounded(self)
    for _, offset in ipairs(offsets[piece.rotation .. ":" .. rotation]) do
        local x = piece.x + offset[1]
        local y = piece.y + offset[2]
        if fits(self, piece.kind, rotation, x, y) then
            piece.x = x
            piece.y = y
            piece.rotation = rotation
            reset_lock(self, was_grounded)
            return true
        end
    end
    return false
end

function State:ghost_y()
    local piece = self.active
    local y = piece.y
    if self.game_over then
        return y
    end

    while fits(self, piece.kind, piece.rotation, piece.x, y + 1) do
        y = y + 1
    end
    return y
end

function State:soft_drop()
    if self.game_over or grounded(self) then
        return false
    end

    self.active.y = self.active.y + 1
    self.score = self.score + 1
    self.gravity_timer = 0
    return true
end

function State:hold()
    if self.game_over or not self.can_hold then
        return false
    end

    local previous = self.held
    self.held = self.active.kind
    spawn(self, previous)
    self.can_hold = false
    return true
end

local function collapse_rows(state)
    local rows = {}
    local remaining = {}
    for y = 1, state.height do
        local full = true
        for x = 1, state.width do
            if not state.board[y][x] then
                full = false
                break
            end
        end

        if full then
            rows[#rows + 1] = y
        else
            remaining[#remaining + 1] = state.board[y]
        end
    end

    for y = 1, #rows do
        state.board[y] = empty_row(state.width)
    end
    for y, row in ipairs(remaining) do
        state.board[y + #rows] = row
    end
    return rows
end

local function clear_points(state, count)
    if count == 0 then
        state.combo = -1
        return 0
    end

    local base = ({100, 300, 500, 800})[count] * state.level
    if count == 4 and state.back_to_back then
        base = base * 1.5
    end

    state.combo = state.combo + 1
    local points = base + 50 * state.combo * state.level
    state.back_to_back = count == 4
    state.lines = state.lines + count
    state.level = math.floor(state.lines / 10) + 1
    return points
end

local function lock_piece(state, drop)
    local piece = state.active
    local all_hidden = true
    local cells = {}
    for _, cell in ipairs(Rules.cells(piece.kind, piece.rotation)) do
        local x = piece.x + cell[1]
        local y = piece.y + cell[2]
        state.board[y][x] = piece.kind
        cells[#cells + 1] = { x, y }
        if y > state.hidden then
            all_hidden = false
        end
    end

    local rows = collapse_rows(state)
    local points = clear_points(state, #rows)
    state.score = state.score + points
    state.can_hold = true
    if all_hidden then
        state.game_over = true
    else
        spawn(state)
    end

    local kind = #rows > 0 and "clear" or "lock"
    if state.game_over then
        kind = "game_over"
    end
    return {
        kind = kind,
        lines = #rows,
        rows = rows,
        points = points + drop * 2,
        drop = drop,
        piece = piece.kind,
        cells = cells,
    }
end

function State:hard_drop()
    if self.game_over then
        return nil
    end

    local y = self:ghost_y()
    local distance = y - self.active.y
    self.active.y = y
    self.score = self.score + distance * 2
    return lock_piece(self, distance)
end

-- Guideline gravity in seconds per row; level 20 and above is 20G (0: the piece lands at once).
local function gravity_interval(state, soft)
    if state.level >= 20 then
        return 0
    end

    local interval = (0.8 - (state.level - 1) * 0.007) ^ (state.level - 1)
    return soft and math.min(interval, 0.035) or interval
end

function State:tick(dt, soft)
    if self.game_over or dt <= 0 then
        return nil
    end

    local interval = gravity_interval(self, soft)
    if interval <= 0 then
        self.active.y = self:ghost_y()
    end

    while dt > 0 do
        if grounded(self) then
            local elapsed = math.min(dt, math.max(0, 0.5 - self.lock_timer))
            self.lock_timer = self.lock_timer + elapsed
            dt = dt - elapsed
            if self.lock_timer >= 0.5 then
                return lock_piece(self, 0)
            end
        else
            local elapsed = math.min(dt, math.max(0, interval - self.gravity_timer))
            self.gravity_timer = self.gravity_timer + elapsed
            dt = dt - elapsed
            if self.gravity_timer >= interval then
                self.gravity_timer = 0
                self.active.y = self.active.y + 1
                if soft then
                    self.score = self.score + 1
                end
            end
        end
    end
    return nil
end

return Rules
