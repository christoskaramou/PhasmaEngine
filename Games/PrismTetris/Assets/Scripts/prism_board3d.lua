-- One 3D board: pooled crystal cubes for the settled cells, the falling piece and its ghost, the well
-- around them and a coloured light riding the piece. Everything is created and painted at load; play only
-- moves, shows, hides and re-colours.
local Board = {}
Board.__index = Board

local COLUMNS = 10
local FIRST_ROW = 3
local LAST_ROW = 22
local CELL_COUNT = 200
local CUBE = 0.9
local BASE_SHADE = 0.35
local SETTLED_GLOW = 0.8
local ACTIVE_GLOW = 0.7
local ACTIVE_SWING = 0.08
local GHOST_GLOW = 0.3
local PIECE_GLOW = 2
local PIECE_GLOW_RANGE = 5
local PIECE_GLOW_FRONT = 1.6
local SWEEP_TIME = 0.22
local SWEEP_GLOW = 1.4
local SWEEP_TINT = { 0.6, 0.9, 1.0 }
local FLASH_TIME = 0.25
local FLASH_LIGHT = 60
local FLASH_RADIUS = 10
local FADE_TIME = 1.0
local FADE_FLOOR = 0.12

local HUES = {
    I = { 0.25, 0.85, 1.0 },
    O = { 1.0, 0.78, 0.25 },
    T = { 0.68, 0.42, 1.0 },
    S = { 0.35, 0.95, 0.6 },
    Z = { 1.0, 0.35, 0.55 },
    J = { 0.3, 0.5, 1.0 },
    L = { 1.0, 0.55, 0.25 },
}
Board.HUES = HUES
Board.SETTLED_GLOW = SETTLED_GLOW
Board.FADE_FLOOR = FADE_FLOOR

local READY = {
    "JJ........",
    "JLL.......",
    "J.LOO.....",
    "SSLOO.T...",
    "ZSS.TTT...",
    "ZZIIII....",
}
local READY_FIRST_ROW = 17

local function scaled(hue, factor)
    return hue[1] * factor, hue[2] * factor, hue[3] * factor
end

local function append(batch, count, node, a, b, c)
    local base = count * 4
    batch[base + 1] = node
    batch[base + 2] = a
    batch[base + 3] = b
    batch[base + 4] = c
    return count + 1
end

local function paint(node, base, emissive, roughness)
    material.set(node, "base_color", vec4(base[1], base[2], base[3], 1))
    material.set(node, "roughness", roughness)
    material.set(node, "metallic", 0)
    material.set(node, "emissive", vec3(emissive[1], emissive[2], emissive[3]))
    return node
end

local function crystal(node, base, roughness)
    paint(node, base, { 0, 0, 0 }, roughness)
    material.set_texture(node, "base_color", "Textures/crystal.png")
    material.set_texture(node, "emissive", "Textures/crystal_glow.png")
    return node
end

local function bar(position, size, emissive, roughness)
    local node = primitives.cube(1)
    node:set_scale(vec3(size[1], size[2], size[3]))
    node:set_position(vec3(position[1], position[2], position[3]))
    return paint(node, { 0.02, 0.03, 0.05 }, emissive, roughness)
end

function Board.new(options)
    local self = setmetatable({
        rules = options.rules,
        stage = options.stage,
        origin = options.origin or { 0, 0, 0 },
        -- Only the lead board (yours) shakes the camera, flares bloom and shifts the aurora.
        lead = options.lead ~= false,
        shown = true,
        bars = {},
        kinds = {},
        cells = {},
        active = {},
        ghost = {},
        piece_cells = {},
        glows = {},
        base_colors = {},
        sweeps = {},
        flashes = {},
        active_kind = false,
        piece_shown = false,
        motion = true,
        time = 0,
        fade = 1,
        fading = false,
        level = 1,
        next_sweep = 1,
        next_flash = 1,
    }, Board)

    for kind, hue in pairs(HUES) do
        self.base_colors[kind] = vec4(hue[1] * BASE_SHADE, hue[2] * BASE_SHADE, hue[3] * BASE_SHADE, 1)
    end

    for i = 1, 4 do
        self.piece_cells[i] = { 0, 0 }
    end

    self:build_well()
    self:build_cells()
    self:build_piece()
    self:build_effects()
    return self
end

function Board:world(x, y)
    local origin = self.origin
    return origin[1] + x - 5.5, origin[2] + LAST_ROW - y + 0.5, origin[3]
end

function Board:build_well()
    local x = self.origin[1]
    local y = self.origin[2]
    local z = self.origin[3]
    local grid = { 0.05, 0.11, 0.15 }
    local frame = { 0.35, 0.55, 0.7 }

    local bars = self.bars

    -- Dark back glass with a faint cell grid, then a thin glowing frame around the well.
    bars[#bars + 1] = bar({ x, y + 10, z - 0.6 }, { 10.3, 20.3, 0.15 }, { 0.012, 0.024, 0.06 }, 0.35)
    for column = 1, COLUMNS - 1 do
        bars[#bars + 1] = bar({ x + column - 5, y + 10, z - 0.52 }, { 0.025, 20, 0.02 }, grid, 0.3)
    end
    for row = 1, 19 do
        bars[#bars + 1] = bar({ x, y + row, z - 0.52 }, { 10, 0.025, 0.02 }, grid, 0.3)
    end

    bars[#bars + 1] = bar({ x - 5.2, y + 10, z }, { 0.14, 20.5, 1.0 }, frame, 0.25)
    bars[#bars + 1] = bar({ x + 5.2, y + 10, z }, { 0.14, 20.5, 1.0 }, frame, 0.25)
    bars[#bars + 1] = bar({ x, y - 0.12, z }, { 10.54, 0.14, 1.0 }, frame, 0.25)
end

-- A hidden board (Jev's, outside a race) shows no node at all; shown again, its next sync repaints its game.
function Board:set_shown(shown)
    if shown == self.shown then
        return
    end

    self.shown = shown
    for _, node in ipairs(self.bars) do
        node:set_visible(shown)
    end

    if shown then
        return
    end

    for i = 1, CELL_COUNT do
        self.cells[i]:set_visible(false)
        self.kinds[i] = false
    end

    self.piece_shown = true
    self:hide_piece()
    self:clear_effects()
end

function Board:build_cells()
    for i = 1, CELL_COUNT do
        local column = (i - 1) % COLUMNS + 1
        local row = FIRST_ROW + math.floor((i - 1) / COLUMNS)
        local node = crystal(primitives.cube(CUBE), { 0, 0, 0 }, 0.18)
        node:set_position(vec3(self:world(column, row)))
        node:set_visible(false)

        self.cells[i] = node
        self.kinds[i] = false
    end
end

function Board:build_piece()
    for i = 1, 4 do
        self.active[i] = crystal(primitives.cube(CUBE), { 0, 0, 0 }, 0.4)
        self.active[i]:set_visible(false)

        self.ghost[i] = crystal(primitives.cube(CUBE * 0.94), { 0.01, 0.015, 0.03 }, 0.05)
        self.ghost[i]:set_visible(false)

        self.glows[i] = self.stage:area_light()
    end
end

function Board:build_effects()
    for i = 1, 4 do
        local node = paint(primitives.cube(1), { 0, 0, 0 }, { 0, 0, 0 }, 0.5)
        node:set_scale(vec3(10.2, 0.92, 1.02))
        node:set_visible(false)
        self.sweeps[i] = {
            node = node,
            life = 0,
        }
    end

    for i = 1, 2 do
        self.flashes[i] = {
            light = self.stage:point_light(),
            life = 0,
            position = { 0, 0, 0 },
        }
    end
end

function Board:sync(game, mode)
    if not self.shown then
        return
    end

    self:sync_cells(game, mode)
    self:sync_piece(game, mode)
    self:sync_level(game)
end

function Board:sync_level(game)
    if game.level == self.level then
        return
    end

    local leveled_up = game.level > self.level
    self.level = game.level

    if leveled_up then
        self:level_burst()
    end

    if leveled_up and self.lead then
        self.stage:shift(game.level)
    end
end

function Board:target_kind(game, mode, x, y)
    if mode == "ready" then
        local line = READY[y - READY_FIRST_ROW + 1]
        local kind = line and line:sub(x, x)
        return kind and kind ~= "." and kind or false
    end

    return game.board[y][x] or false
end

function Board:sync_cells(game, mode)
    local glow = {}
    local count = 0
    for i = 1, CELL_COUNT do
        local x = (i - 1) % COLUMNS + 1
        local y = FIRST_ROW + math.floor((i - 1) / COLUMNS)
        local kind = self:target_kind(game, mode, x, y)
        if self.kinds[i] ~= kind then
            self.kinds[i] = kind
            count = self:repaint(i, kind, glow, count)
        end
    end

    if count > 0 then
        material.set_emissives(glow, count)
    end
end

function Board:repaint(i, kind, glow, count)
    local node = self.cells[i]
    if not kind then
        node:set_visible(false)
        return count
    end

    material.set(node, "base_color", self.base_colors[kind])
    node:set_visible(true)

    local r, g, b = scaled(HUES[kind], SETTLED_GLOW * self.fade)
    return append(glow, count, node, r, g, b)
end

function Board:sync_piece(game, mode)
    local piece = game.active
    local playing = mode ~= "ready" and not game.game_over and piece ~= nil
    if not playing then
        self:hide_piece()
        return
    end

    if piece.kind ~= self.active_kind then
        self:recolor_piece(piece.kind)
    end

    local landing_y = game:ghost_y()
    local show_ghost = landing_y ~= piece.y
    local positions = {}
    local count = 0
    for i, cell in ipairs(self.rules.cells(piece.kind, piece.rotation)) do
        local x = piece.x + cell[1]
        local y = piece.y + cell[2]
        local ground = landing_y + cell[2]
        count = self:place(self.active[i], x, y, true, positions, count)
        count = self:place(self.ghost[i], x, ground, show_ghost, positions, count)

        self.piece_cells[i][1] = x
        self.piece_cells[i][2] = y
    end

    if count > 0 then
        scene.set_positions(positions, count)
    end
    self.piece_shown = true
    self:follow(piece)
end

function Board:place(node, x, y, show, positions, count)
    local inside = show and y >= FIRST_ROW and y <= LAST_ROW
    node:set_visible(inside)
    if not inside then
        return count
    end

    local wx, wy, wz = self:world(x, y)
    return append(positions, count, node, wx, wy, wz)
end

function Board:recolor_piece(kind)
    self.active_kind = kind
    local glow = {}
    local count = 0
    local r, g, b = scaled(HUES[kind], GHOST_GLOW)
    for i = 1, 4 do
        material.set(self.active[i], "base_color", self.base_colors[kind])
        count = append(glow, count, self.ghost[i], r, g, b)
    end

    material.set_emissives(glow, count)
end

-- The piece glows onto the well in its own shape: a cell-sized area light in front of each cube, facing the
-- board, dark while its cell is still in the hidden spawn rows. (Right behind the cubes it lit only glass the
-- cubes themselves hide.)
function Board:follow(piece)
    local hue = HUES[piece.kind]
    for i = 1, 4 do
        local x = self.piece_cells[i][1]
        local y = self.piece_cells[i][2]
        local inside = y >= FIRST_ROW and y <= LAST_ROW
        local wx, wy, wz = self:world(x, y)
        local position = {
            wx,
            wy,
            wz + PIECE_GLOW_FRONT * self.stage.side,
        }

        local intensity = inside and PIECE_GLOW or 0
        self.stage:set_area_light(self.glows[i], position, hue, intensity, PIECE_GLOW_RANGE, 1, 1)
    end
end

function Board:hide_piece()
    if not self.piece_shown then
        return
    end

    for i = 1, 4 do
        self.active[i]:set_visible(false)
        self.ghost[i]:set_visible(false)
        self.stage:set_area_light(self.glows[i], { 0, 0, 0 }, { 0, 0, 0 }, 0, 1, 1, 1)
    end

    self.piece_shown = false
end

function Board:update(dt, motion)
    if not self.shown then
        return
    end

    self.time = self.time + dt
    self.motion = motion

    self:pulse_piece()
    self:update_sweeps(dt)
    self:update_flashes(dt)
    self:update_fade(dt)
end

function Board:update_sweeps(dt)
    local glow = {}
    local count = 0
    for _, sweep in ipairs(self.sweeps) do
        if sweep.life > 0 then
            sweep.life = math.max(0, sweep.life - dt)
            local r, g, b = scaled(SWEEP_TINT, SWEEP_GLOW * sweep.life / SWEEP_TIME)
            local reach = math.max(0.1, math.min(1, (SWEEP_TIME - sweep.life) / (SWEEP_TIME * 0.4)))
            sweep.node:set_scale(vec3(10.2 * reach, 0.3, 1.02))
            if sweep.life == 0 then
                sweep.node:set_visible(false)
            end

            count = append(glow, count, sweep.node, r, g, b)
        end
    end

    if count > 0 then
        material.set_emissives(glow, count)
    end
end

function Board:update_flashes(dt)
    for _, flash in ipairs(self.flashes) do
        if flash.life > 0 then
            flash.life = math.max(0, flash.life - dt)
            local intensity = FLASH_LIGHT * flash.life / FLASH_TIME
            self.stage:set_light(flash.light, flash.position, { 0.8, 0.95, 1.0 }, intensity, FLASH_RADIUS)
        end
    end
end

function Board:update_fade(dt)
    if not self.fading then
        return
    end

    self.fade = math.max(FADE_FLOOR, self.fade - dt / FADE_TIME)
    self.fading = self.fade > FADE_FLOOR
    self:repaint_all()
end

function Board:repaint_all()
    local glow = {}
    local count = 0
    for i = 1, CELL_COUNT do
        local kind = self.kinds[i]
        if kind then
            local r, g, b = scaled(HUES[kind], SETTLED_GLOW * self.fade)
            count = append(glow, count, self.cells[i], r, g, b)
        end
    end

    if count > 0 then
        material.set_emissives(glow, count)
    end
end

-- Effects fire before this frame's sync: `kinds` still holds the settled cells from before the lock, and the
-- event names the locked piece and its cells (moves made in the drop frame happened after the last sync).
-- With FX off a lock shows nothing extra: no sparks, shards, sweeps, flash lights, shake or bloom flare.
function Board:impact(event, motion)
    if not event or not self.shown then
        return
    end

    self.motion = motion
    if event.kind == "game_over" then
        self.fading = true
    end

    if not motion then
        return
    end

    if event.drop and event.drop > 0 then
        self:sparks(event)
        self:jolt(0.08)
    end

    for _, row in ipairs(event.rows or {}) do
        if row >= FIRST_ROW then
            self:clear_row(event, row)
        end
    end

    if event.lines == 4 then
        self:jolt(0.25)
        if self.lead then
            self.stage:flare()
        end
    end
end

function Board:jolt(strength)
    if self.lead then
        self.stage:jolt(strength)
    end
end

function Board:clear_row(event, row)
    local wx, wy, wz = self:world(5.5, row)
    local front = wz + 0.5 * self.stage.side

    self:start_sweep(wx, wy, wz)
    self:start_flash({ wx, wy, wz + 2 * self.stage.side })

    for half = 0, 1 do
        local kind = self:row_kind(event, row, half * 5 + 3)
        local offset = half == 0 and -2.5 or 2.5
        self:shards(vec3(wx + offset, wy, front), HUES[kind])
    end
end

-- A cleared cell is either a settled one from before the lock or part of the piece that just locked.
function Board:row_kind(event, row, column)
    local kind = self.kinds[(row - FIRST_ROW) * COLUMNS + column]
    return kind or event.piece or self.active_kind or "I"
end

function Board:shards(position, hue)
    local r, g, b = scaled(hue, 1.6)
    particles.emit_burst({
        position = position,
        velocity = vec3(0, 3.0, 2.0 * self.stage.side),
        color_start = vec4(r, g, b, 1),
        color_end = vec4(0.3, 0.4, 0.6, 0),
        gravity = vec3(0, -9, 0),
        count = 36,
        size_min = 0.22,
        size_max = 0.03,
        life_min = 0.55,
        life_max = 1.0,
        spawn_radius = 2.2,
        noise_strength = 9,
        drag = 0.6,
        texture_index = self.stage.textures.shard,
        orientation = "velocity",
    })
end

function Board:sparks(event)
    local cells = event.cells
    local kind = event.piece
    if not cells or not HUES[kind] then
        return
    end

    local sum_x = 0
    local bottom = 0
    for _, cell in ipairs(cells) do
        sum_x = sum_x + cell[1]
        bottom = math.max(bottom, cell[2])
    end

    local wx, wy, wz = self:world(sum_x / #cells, bottom)
    local r, g, b = scaled(HUES[kind], 1.8)
    particles.emit_burst({
        position = vec3(wx, wy - 0.45, wz + 0.5 * self.stage.side),
        velocity = vec3(0, 1.6, 1.0 * self.stage.side),
        color_start = vec4(r, g, b, 1),
        color_end = vec4(0, 0, 0, 0),
        gravity = vec3(0, -12, 0),
        count = 22,
        size_min = 0.12,
        size_max = 0.01,
        life_min = 0.25,
        life_max = 0.45,
        spawn_radius = 1.0,
        noise_strength = 6,
        drag = 0.4,
        texture_index = self.stage.textures.spark,
        orientation = "velocity",
    })
end

function Board:level_burst()
    if not self.motion then
        return
    end

    local wx, wy, wz = self:world(5.5, 12)
    particles.emit_burst({
        position = vec3(wx, wy, wz + 0.6 * self.stage.side),
        velocity = vec3(0, 0, 0),
        color_start = vec4(1.2, 1.8, 2.0, 1),
        color_end = vec4(0.4, 0.2, 0.8, 0),
        gravity = vec3(0, 0, 0),
        count = 160,
        size_min = 0.28,
        size_max = 0.02,
        life_min = 0.8,
        life_max = 1.4,
        spawn_radius = 0.8,
        noise_strength = 14,
        drag = 1.6,
        texture_index = self.stage.textures.glow,
    })
end

function Board:start_sweep(x, y, z)
    local sweep = self.sweeps[self.next_sweep]
    self.next_sweep = self.next_sweep % #self.sweeps + 1

    sweep.life = SWEEP_TIME
    sweep.node:set_position(vec3(x, y, z))
    sweep.node:set_scale(vec3(1.02, 0.3, 1.02))
    sweep.node:set_visible(true)
end

function Board:start_flash(position)
    if not self.motion then
        return
    end

    local flash = self.flashes[self.next_flash]
    self.next_flash = self.next_flash % #self.flashes + 1
    flash.life = FLASH_TIME
    flash.position = position
end

function Board:clear_effects()
    for _, sweep in ipairs(self.sweeps) do
        sweep.life = 0
        sweep.node:set_visible(false)
    end

    for _, flash in ipairs(self.flashes) do
        flash.life = 0
        self.stage:set_light(flash.light, flash.position, { 0, 0, 0 }, 0, FLASH_RADIUS)
    end
end

function Board:reset()
    self.fade = 1
    self.fading = false
    self.level = 1
    self:clear_effects()
    self:repaint_all()
end

function Board:pulse_piece()
    if not self.piece_shown or not self.active_kind then
        return
    end

    local swing = self.motion and ACTIVE_SWING * math.sin(self.time * 12.566) or 0
    local r, g, b = scaled(HUES[self.active_kind], ACTIVE_GLOW + swing)
    local glow = {}
    local count = 0
    for i = 1, 4 do
        count = append(glow, count, self.active[i], r, g, b)
    end

    material.set_emissives(glow, count)
end

return Board
