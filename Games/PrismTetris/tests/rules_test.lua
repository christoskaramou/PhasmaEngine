local script = arg[0]:gsub("\\", "/")
local root = script:match("^(.*)/tests/[^/]+$") or "Games/PrismTetris"
local Rules = dofile(root .. "/Assets/Scripts/tetris_rules.lua")
local count = 0

local function check(name, test)
    test()
    count = count + 1
    print("PASS " .. name)
end

local function place(state, kind, rotation, x, y)
    state.active = {kind = kind, rotation = rotation, x = x, y = y}
    state.lock_timer = 0
    state.gravity_timer = 0
end

local function fill_except(state, first, last, gap)
    for y = first, last do
        for x = 1, state.width do
            state.board[y][x] = "J"
        end
        state.board[y][gap] = false
    end
end

local function cells_key(cells)
    local result = {}
    for _, cell in ipairs(cells) do
        result[#result + 1] = cell[1] .. ":" .. cell[2]
    end
    table.sort(result)
    return table.concat(result, ",")
end

check("seeded seven-bag and five-piece preview", function()
    local a = Rules.new(105)
    local b = Rules.new(105)
    local stream = {a.active.kind}
    for _, kind in ipairs(a.next) do
        stream[#stream + 1] = kind
    end
    for i = 1, 21 do
        if i > 1 then
            for y = 1, a.height do
                for x = 1, a.width do
                    a.board[y][x] = false
                    b.board[y][x] = false
                end
            end
            a:hard_drop()
            b:hard_drop()
        end
        assert(a.active.kind == b.active.kind)
        assert(#a.next == 5)
        stream[i] = a.active.kind
    end

    for first = 1, 21, 7 do
        local seen = {}
        for i = first, first + 6 do
            assert(not seen[stream[i]], "duplicate in seven-bag")
            seen[stream[i]] = true
        end
    end
end)

check("walls, floor and stack collision", function()
    local state = Rules.new(3)
    place(state, "O", 0, 0, 20)
    assert(not state:move(-1))
    assert(state:move(8))
    assert(not state:move(1))
    assert(state:soft_drop())
    assert(not state:soft_drop())
    assert(state.active.y == 21)
    assert(state.score == 1)

    place(state, "O", 0, 3, 10)
    state.board[10][6] = "T"
    assert(not state:move(1))
end)

check("four rotations restore every piece", function()
    for _, kind in ipairs({"I", "O", "T", "S", "Z", "J", "L"}) do
        local state = Rules.new(8)
        place(state, kind, 0, 4, 8)
        local original = cells_key(Rules.cells(kind, 0))
        for _ = 1, 4 do
            assert(state:rotate(1))
        end
        assert(state.active.rotation == 0)
        assert(state.active.x == 4 and state.active.y == 8)
        assert(cells_key(Rules.cells(kind, state.active.rotation)) == original)
        for _ = 1, 4 do
            assert(state:rotate(-1))
        end
        assert(state.active.rotation == 0)
    end
end)

check("SRS wall and floor kicks including I", function()
    local state = Rules.new(1)
    place(state, "T", 1, 0, 8)
    assert(state:rotate(-1))
    assert(state.active.x == 1 and state.active.rotation == 0)

    place(state, "T", 0, 4, 21)
    assert(state:rotate(1))
    assert(state.active.y == 20 and state.active.x == 3)

    place(state, "I", 1, -1, 8)
    assert(state:rotate(-1))
    assert(state.active.x == 1 and state.active.rotation == 0)

    place(state, "I", 0, 4, 21)
    assert(state:rotate(1))
    assert(state.active.y == 19 and state.active.x == 5)
end)

check("blocked rotations leave active piece intact", function()
    local state = Rules.new(2)
    place(state, "T", 0, 4, 8)
    for y = 5, 13 do
        for x = 1, 10 do
            state.board[y][x] = "L"
        end
    end
    for _, cell in ipairs(Rules.cells("T", 0)) do
        state.board[8 + cell[2]][4 + cell[1]] = false
    end
    assert(not state:rotate(1))
    assert(state.active.x == 4 and state.active.y == 8 and state.active.rotation == 0)
end)

check("hold once per piece then swap after lock", function()
    local state = Rules.new(27)
    local first = state.active.kind
    local upcoming = state.next[1]
    assert(state:hold())
    assert(state.held == first and state.active.kind == upcoming)
    assert(not state:hold())
    assert(state:hard_drop())
    local current = state.active.kind
    assert(state:hold())
    assert(state.active.kind == first and state.held == current)
    assert(state.active.rotation == 0 and state.active.x == 4 and state.active.y == 1)
end)

check("ghost and hard-drop scoring", function()
    local state = Rules.new(4)
    place(state, "O", 0, 4, 1)
    assert(state:ghost_y() == 21)
    local event = state:hard_drop()
    assert(event.kind == "lock" and event.drop == 20 and event.points == 40)
    assert(state.score == 40)
    assert(state.board[22][5] == "O" and state.board[21][6] == "O")
end)

check("single clear collapses rows and scores", function()
    local state = Rules.new(5)
    fill_except(state, 22, 22, 5)
    state.board[21][1] = "Z"
    place(state, "I", 1, 3, 19)
    local event = state:hard_drop()
    assert(event.kind == "clear" and event.lines == 1 and event.rows[1] == 22)
    assert(event.points == 100 and state.score == 100)
    assert(state.lines == 1 and state.combo == 0)
    assert(state.board[22][1] == "Z" and state.board[22][5] == "I")
    assert(not state.board[1][1])
end)

check("Tetris, combo, back-to-back and level progression", function()
    local state = Rules.new(6)
    fill_except(state, 19, 22, 5)
    place(state, "I", 1, 3, 19)
    local event = state:hard_drop()
    assert(event.lines == 4 and event.points == 800 and #event.rows == 4)
    assert(state.lines == 4 and state.back_to_back and state.combo == 0)

    fill_except(state, 19, 22, 5)
    place(state, "I", 1, 3, 19)
    event = state:hard_drop()
    assert(event.points == 1250 and state.lines == 8 and state.combo == 1)

    fill_except(state, 21, 22, 5)
    place(state, "I", 1, 3, 19)
    event = state:hard_drop()
    assert(event.points == 400 and state.level == 2 and state.lines == 10)
    assert(not state.back_to_back)
end)

check("gravity, soft gravity and half-second lock delay", function()
    local state = Rules.new(7)
    place(state, "O", 0, 4, 1)
    assert(not state:tick(0.5, false))
    assert(state.active.y == 1)
    assert(not state:tick(0.5, false))
    assert(state.active.y == 2)
    assert(not state:tick(0.07, true))
    assert(state.active.y == 4 and state.score == 2)

    place(state, "O", 0, 4, 21)
    assert(not state:tick(0.49, false))
    assert(state:move(1))
    assert(not state:tick(0.49, false))
    local event = state:tick(0.02, false)
    assert(event and event.kind == "lock")
end)

check("lock resets stop after fifteen grounded actions", function()
    local state = Rules.new(9)
    place(state, "O", 0, 4, 21)
    for i = 1, 15 do
        assert(not state:tick(0.4, false))
        assert(state:move(i % 2 == 1 and 1 or -1))
    end
    assert(state.lock_resets == 15)
    assert(not state:tick(0.4, false))
    assert(state:move(-1))
    assert(state:tick(0.11, false))
end)

check("spawn block-out, hold block-out and hidden lock-out", function()
    local state = Rules.new(10)
    for x = 1, 10 do
        state.board[1][x] = "S"
        state.board[2][x] = "Z"
    end
    assert(state:hold())
    assert(state.game_over)
    assert(not state:move(1) and not state:rotate(1) and not state:soft_drop())
    assert(not state:hard_drop() and not state:tick(1, false))

    state:restart(10)
    place(state, "O", 0, 4, 1)
    state.board[3][5] = "I"
    state.board[3][6] = "I"
    local event = state:hard_drop()
    assert(event.kind == "game_over" and state.game_over)

    state:restart(11)
    place(state, "O", 0, 0, 21)
    for _, cell in ipairs(Rules.cells(state.next[1], 0)) do
        state.board[1 + cell[2]][4 + cell[1]] = "L"
    end
    event = state:hard_drop()
    assert(event.kind == "game_over" and state.game_over)
end)

check("restart restores clean deterministic state", function()
    local state = Rules.new(91)
    local first = state.active.kind
    local next_key = table.concat(state.next)
    state:hard_drop()
    state:hold()
    state:restart()
    assert(state.active.kind == first and table.concat(state.next) == next_key)
    assert(state.score == 0 and state.lines == 0 and state.level == 1)
    assert(not state.held and state.can_hold and not state.game_over)
    for y = 1, 22 do
        for x = 1, 10 do
            assert(not state.board[y][x])
        end
    end
end)

check("guideline gravity speeds up per level", function()
    local state = Rules.new(5)
    state.level = 2
    local y = state.active.y
    state:tick(0.79)
    assert(state.active.y == y, "level 2 waits about 0.793 s per row")
    state:tick(0.005)
    assert(state.active.y == y + 1, "level 2 falls a row by 0.795 s")

    state = Rules.new(5)
    state.level = 10
    y = state.active.y
    state:tick(0.063)
    assert(state.active.y == y, "level 10 waits about 0.064 s per row")
    state:tick(0.002)
    assert(state.active.y == y + 1, "level 10 falls a row by 0.065 s")
end)

check("level 20 is 20G with the normal lock delay", function()
    local state = Rules.new(5)
    state.level = 20
    local landing = state:ghost_y()
    local event = state:tick(0.001)
    assert(not event and state.active.y == landing, "20G drops the piece to its landing row at once")

    event = state:tick(0.45)
    assert(not event, "the lock delay still applies at 20G")
    event = state:tick(0.06)
    assert(event and event.kind == "lock", "the piece locks after 0.5 s on the ground")
end)

print("Prism Tetris rules: " .. count .. " checks passed")
