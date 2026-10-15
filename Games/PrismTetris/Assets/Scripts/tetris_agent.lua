local Agent = {}
local COMMANDS = { "left", "right", "cw", "ccw", "down" }
local INPUT_INTERVAL = 0.07

local function copy_array(source)
    local result = {}
    for i, value in ipairs(source) do
        result[i] = value
    end
    return result
end

local function copy_state(game, deep)
    local result = {}
    for key, value in pairs(game) do
        result[key] = value
    end
    result.active = {
        kind = game.active.kind,
        rotation = game.active.rotation,
        x = game.active.x,
        y = game.active.y,
    }
    if deep then
        result.board = {}
        for y, row in ipairs(game.board) do
            result.board[y] = copy_array(row)
        end
        result.next = copy_array(game.next)
        result.bag = copy_array(game.bag)
    end
    return setmetatable(result, getmetatable(game))
end

local function move(state, command)
    if command == "left" then
        return state:move(-1)
    elseif command == "right" then
        return state:move(1)
    elseif command == "cw" then
        return state:rotate(1)
    elseif command == "ccw" then
        return state:rotate(-1)
    end
    return state:soft_drop()
end

-- Numeric, so the search allocates no strings: origins stay within x -2..10 and y -1..22.
local function position_key(piece)
    return ((piece.y + 2) * 16 + piece.x + 4) * 4 + piece.rotation
end

-- Decision epoch without the falling row, so gravity alone never invalidates a decision in flight.
local function decision_key(game, state, busy)
    local values = {
        state.mode,
        tostring(state.agent),
        tostring(busy),
        game.score,
        game.lines,
        game.level,
        game.held or "-",
        tostring(game.can_hold),
        game.active.kind,
        game.active.x,
        game.active.rotation,
    }

    for _, row in ipairs(game.board) do
        for x = 1, game.width do
            values[#values + 1] = row[x] or "."
        end
    end

    for _, kind in ipairs(game.next) do
        values[#values + 1] = kind
    end

    return table.concat(values, "|")
end

local function surface_features(game)
    local heights = {}
    local holes = 0
    local aggregate = 0
    local tallest = 0
    for x = 1, game.width do
        local occupied = false
        heights[x] = 0
        for y = 1, game.height do
            if game.board[y][x] then
                if not occupied then
                    heights[x] = game.height - y + 1
                end
                occupied = true
            elseif occupied then
                holes = holes + 1
            end
        end
        aggregate = aggregate + heights[x]
        tallest = math.max(tallest, heights[x])
    end
    local bumpiness = 0
    for x = 2, game.width do
        bumpiness = bumpiness + math.abs(heights[x] - heights[x - 1])
    end
    return holes, aggregate, tallest, bumpiness
end

local function route(node, hold)
    local reverse = {}
    while node.previous do
        reverse[#reverse + 1] = node.command
        node = node.previous
    end
    local commands = hold and { "hold" } or {}
    for i = #reverse, 1, -1 do
        commands[#commands + 1] = reverse[i]
    end
    commands[#commands + 1] = "drop"
    return commands
end

local function landing_key(rules, probe, piece, hold)
    probe.active = piece
    local landing = probe:ghost_y()
    local coordinates = {}
    for _, cell in ipairs(rules.cells(piece.kind, piece.rotation)) do
        coordinates[#coordinates + 1] = (landing + cell[2]) * 10 + piece.x + cell[1]
    end
    table.sort(coordinates)
    return (hold and "hold:" or "play:") .. table.concat(coordinates, ":")
end

local function placement(state, node, hold)
    local simulation = copy_state(state, true)
    simulation.active = {
        kind = node.piece.kind,
        rotation = node.piece.rotation,
        x = node.piece.x,
        y = node.piece.y,
    }
    local event = simulation:hard_drop()
    local holes, height, tallest, bump = surface_features(simulation)
    local score = event.lines * 12 - holes * 9 - height * 0.48 - bump * 0.28
    if simulation.game_over then
        score = score - 10000
    end
    local description = string.format(
        "%s %s at column %d, rotation %d: clear %d lines, %d holes, height %d.",
        hold and "Hold then place" or "Place",
        node.piece.kind,
        node.piece.x,
        node.piece.rotation,
        event.lines,
        holes,
        tallest
    )
    return {
        description = description,
        score = score,
    }
end

local function expand(probe, node, command)
    probe.active = {
        kind = node.piece.kind,
        rotation = node.piece.rotation,
        x = node.piece.x,
        y = node.piece.y,
    }
    if not move(probe, command) then
        return nil
    end
    return {
        piece = probe.active,
        previous = node,
        command = command,
    }
end

-- Breadth-first over every position the active piece can reach; visit(key, node) returns true to stop there.
-- One scratch copy of the game takes every trial move and landing check instead of a copy per step.
local function search(rules, state, hold, visit)
    if state.game_over then
        return nil
    end

    local probe = copy_state(state, false)
    local first = { piece = state.active }
    local queue = { first }
    local visited = { [position_key(first.piece)] = true }
    local head = 1
    while head <= #queue do
        local node = queue[head]
        head = head + 1
        if visit(landing_key(rules, probe, node.piece, hold), node) then
            return node
        end

        for _, command in ipairs(COMMANDS) do
            local next_node = expand(probe, node, command)
            if next_node then
                local next_key = position_key(next_node.piece)
                if not visited[next_key] then
                    visited[next_key] = true
                    queue[#queue + 1] = next_node
                end
            end
        end
    end
end

local function reachable(rules, state, hold, results)
    search(rules, state, hold, function(key, node)
        if not results[key] then
            results[key] = placement(state, node, hold)
        end
    end)
end

-- First input of the shortest route from where the piece is now to the target landing; nil when unreachable.
local function next_command(rules, game, target)
    local hold = target:sub(1, 5) == "hold:"
    local state = game
    if hold then
        state = copy_state(game, true)
        if not state:hold() then
            return nil
        end
    end

    local found = search(rules, state, hold, function(key)
        return key == target
    end)
    return found and route(found, hold)[1]
end

local function placements(rules, game)
    local candidates = {}
    reachable(rules, game, false, candidates)
    if game.can_hold then
        local held = copy_state(game, true)
        if held:hold() then
            reachable(rules, held, true, candidates)
        end
    end
    local keys = {}
    for key in pairs(candidates) do
        keys[#keys + 1] = key
    end
    table.sort(keys)
    local actions = {}
    local targets = {}
    for index, key in ipairs(keys) do
        local id = "placement_" .. index
        local candidate = candidates[key]
        actions[index] = {
            id = id,
            description = candidate.description,
            score = candidate.score,
        }
        targets[id] = key
    end
    return actions, targets
end

-- options.game() returns the game the agent plays: yours in solo, Jev's in a race.
function Agent.attach(options)
    local state = options.state
    local cached_key
    local actions = {}
    local targets = {}
    local target
    local piece
    local wait = 0

    -- A human key, a lock, a new game or a change of seat ends the running placement.
    local function placing(game)
        if not state.agent or game.game_over or game.active ~= piece then
            target = nil
        end
        return target ~= nil
    end

    -- After a human key or New Game the human owns the run: no placements until the driver resets the game.
    local function observe()
        local game = options.game()
        local busy = placing(game)
        local key = decision_key(game, state, busy)
        if key ~= cached_key then
            local deciding = state.agent and not busy
            if state.mode == "playing" and not game.game_over and deciding then
                actions, targets = placements(options.rules, game)
            else
                actions, targets = {}, {}
            end
            cached_key = key
        end
        local board = {}
        for y = 3, game.height do
            local row = {}
            for x = 1, game.width do
                row[x] = game.board[y][x] or "."
            end
            board[#board + 1] = table.concat(row)
        end
        -- A finished race is terminal for Jev's board too, so the driver exits instead of waiting for a piece.
        return {
            mode = state.mode,
            terminal = game.game_over or state.mode == "over",
            score = game.score,
            busy = busy,
            observation = {
                board = board,
                active = game.active,
                held = game.held or "none",
                next = game.next,
                lines = game.lines,
                level = game.level,
                can_hold = game.can_hold,
            },
            actions = actions,
        }
    end

    -- One input every 0.07 s toward the chosen landing, re-planned from wherever gravity has moved the piece.
    local function tick(dt)
        local game = options.game()
        if not placing(game) or state.mode ~= "playing" then
            return
        end

        wait = wait - dt
        if wait > 0 then
            return
        end

        wait = wait + INPUT_INTERVAL
        local command = next_command(options.rules, game, target)
        if not command or command == "drop" then
            target = nil
        end

        if command and not options.apply(command) then
            target = nil
        elseif command == "hold" then
            target = "play:" .. target:sub(6)
            piece = game.active
        end
    end

    local controller = options.driver.attach({
        id = "prism-tetris",
        description = "Clear rows and survive. Board rows show 20 visible rows; active origins use 1-based coordinates "
            .. "including two hidden spawn rows. The piece keeps falling while you decide; a chosen placement plays "
            .. "out as timed inputs and is abandoned if the piece can no longer reach it. Choose one reachable "
            .. "placement. Numeric scores are a local baseline favouring cleared lines, few holes and a low, even "
            .. "stack. Only the visible five-piece queue is exposed.",
        observe = observe,
        -- The decision epoch that also keys the action list, so one state_id always names one set of actions.
        state_key = function()
            return cached_key
        end,
        reset = function(seed)
            options.reset(seed)
            target = nil
            state.assisted = true
            state.agent = true
            return observe()
        end,
        act = function(id)
            target = assert(targets[id], "Unknown placement")
            piece = options.game().active
            wait = 0
            state.assisted = true
            state.agent = true
        end,
    })
    controller.tick = tick
    return controller
end

return Agent
