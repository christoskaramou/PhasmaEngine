local root = arg[0]:gsub("\\", "/"):match("^(.*)/tests/[^/]+$") or "Games/PrismTetris"
local record = dofile(root .. "/tests/engine3d_stubs.lua")
local surface = {
    width = 1600,
    height = 1000,
}
runtime_ui = {
    get_surface_size = function()
        return surface
    end,
}

local function script(name)
    return dofile(root .. "/Assets/Scripts/" .. name)
end

local function near(a, b, tolerance)
    return math.abs(a - b) <= tolerance
end

local function find_node(predicate)
    for _, node in ipairs(record.nodes) do
        if predicate(node) then
            return node
        end
    end
end

-- Stage --------------------------------------------------------------------------------------------

local Stage = script("prism_stage.lua")
local stage = Stage.new()

assert(record.settings_nodes == 1, "post-processing needs exactly one scene-settings node")
for _, name in ipairs({ "hdr", "tonemapping", "bloom", "ssr", "taa" }) do
    assert(record.settings[name] == true, name .. " must be enabled")
end
assert(record.settings.bloom_strength == Stage.BLOOM)
assert(record.camera.projection == "perspective" and record.camera.fov == 45)
assert(#record.directional_lights == 1, "the stage owns one key light")
assert(#record.emitters == 0, "no long-lived emitters: effects use bursts only")

local aurora = find_node(function(node)
    return node.material.texture_emissive == "Textures/aurora.png"
end)
assert(aurora, "aurora backdrop uses the generated aurora texture as emission")

-- The camera frames the 20 board rows inside the HUD's 200-880 design-px cutout at any window shape.
-- Independent projection: look-at with world up +Y, horizontal FOV 45, from the stage's real eye and target.
local function cross(a, b)
    return {
        a[2] * b[3] - a[3] * b[2],
        a[3] * b[1] - a[1] * b[3],
        a[1] * b[2] - a[2] * b[1],
    }
end

local function dot(a, b)
    return a[1] * b[1] + a[2] * b[2] + a[3] * b[3]
end

local function normalize(a)
    local length = math.sqrt(dot(a, a))
    return { a[1] / length, a[2] / length, a[3] / length }
end

-- Design-space x and y of the board-plane point (world_x, world_y).
local function design_point(world_x, world_y, width, height)
    local eye = stage.eye
    local target = stage.target
    local forward = normalize({ target[1] - eye[1], target[2] - eye[2], target[3] - eye[3] })
    local right = normalize(cross(forward, { 0, 1, 0 }))
    local up = cross(right, forward)
    local relative = { world_x - eye[1], world_y - eye[2], -eye[3] }

    local tan_horizontal = math.tan(math.rad(45) / 2)
    local tan_vertical = math.tan(math.atan(tan_horizontal * height / width))
    local depth = dot(relative, forward)
    local ndc_x = dot(relative, right) / (depth * tan_horizontal)
    local ndc_y = dot(relative, up) / (depth * tan_vertical)
    local scale = math.min(width / 1600, height / 1000)
    local left = (width - 1600 * scale) / 2
    local top = (height - 1000 * scale) / 2
    return ((1 + ndc_x) * width / 2 - left) / scale, ((1 - ndc_y) * height / 2 - top) / scale
end

local SIZES = { { 1600, 1000 }, { 2560, 1080 }, { 1600, 1200 }, { 2560, 1369 } }
local you_x = Stage.BOARD_X.you
local jev_x = Stage.BOARD_X.jev
assert(you_x == -7.5 and jev_x == 7.5, "your board sits at x = -7.5, Jev's at +7.5")

-- Solo: the camera centres on your board, whose 20 rows fill the 200-880 cutout.
for _, size in ipairs(SIZES) do
    stage:resize(size[1], size[2])
    local center_x, board_top = design_point(you_x, 20, size[1], size[2])
    local _, board_bottom = design_point(you_x, 0, size[1], size[2])
    local fits = near(board_top, 200, 1) and near(board_bottom, 880, 1) and near(center_x, 800, 1)
    local message = "solo board spans %.1f-%.1f px around x %.1f at %dx%d"
    assert(fits, string.format(message, board_top, board_bottom, center_x, size[1], size[2]))
end

-- Versus: both boards' rows fill 250-850, you on the left, each frame inside its HUD cutout.
stage:set_layout("versus")
local CUTOUTS = Stage.CUTOUTS
for _, size in ipairs(SIZES) do
    local width = size[1]
    local height = size[2]
    stage:resize(width, height)
    for seat, board_x in pairs({ you = you_x, jev = jev_x }) do
        local cutout = CUTOUTS[seat]
        local _, board_top = design_point(board_x, 20, width, height)
        local _, board_bottom = design_point(board_x, 0, width, height)
        local rows = near(board_top, 250, 1.5) and near(board_bottom, 850, 1.5)
        assert(rows, string.format("%s rows span %.1f-%.1f px at %dx%d", seat, board_top, board_bottom, width, height))

        for _, y in ipairs({ 0, 20 }) do
            local left = design_point(board_x - 5.2, y, width, height)
            local right = design_point(board_x + 5.2, y, width, height)
            local inside = left >= cutout[1] and right <= cutout[2]
            local message = "%s frame spans %.1f-%.1f px at row %d, %dx%d"
            assert(inside, string.format(message, seat, left, right, y, width, height))
        end
    end
end
assert(CUTOUTS.you[2] < 800 and CUTOUTS.jev[1] > 800, "you play on the left, Jev on the right")
local aurora_x = aurora.position[1]
stage:set_layout("solo")
assert(aurora_x == 0 and aurora.position[1] == you_x, "the aurora stays centred behind the framed boards")
stage:resize(1600, 1000)

-- Effects on: a flare brightens bloom. Effects off: no sway or shake, bloom back to base at once.
stage:flare()
stage:update(0.016, true)
assert(record.settings.bloom_strength > Stage.BLOOM, "a flare raises bloom")

stage:jolt(0.25)
stage:flare()
stage:update(0.016, false)
local position = record.camera.position
assert(near(position.x, stage.eye[1], 1e-9) and near(position.y, stage.eye[2], 1e-9), "reduced FX: camera at rest")
assert(record.settings.bloom_strength == Stage.BLOOM, "reduced FX: bloom at base strength")

stage:shift(2)
assert(stage.tint_target[1] ~= 1 or stage.tint_target[3] ~= 1, "a level change retints the aurora")

local light = stage:point_light()
stage:set_light(light, { 1, 2, 3 }, { 0.5, 0.25, 1 }, 12, 4)
local entry = record.point_lights[light.index + 1]
assert(entry.intensity == 12 and entry.radius == 4)
assert(entry.node.position[1] == 1 and entry.node.position[3] == 3, "lights move through their node")

-- Board: cells and piece ---------------------------------------------------------------------------

local Rules = script("tetris_rules.lua")
local Board = script("prism_board3d.lua")
local board = Board.new({
    rules = Rules,
    stage = stage,
})
local rival = Board.new({
    rules = Rules,
    stage = stage,
    origin = { 7.5, 0, 0 },
    lead = false,
})
record.frozen = true

local function visible(nodes)
    local count = 0
    for _, node in ipairs(nodes) do
        if node.visible then
            count = count + 1
        end
    end
    return count
end

local function glow_lights()
    local list = {}
    for i, glow in ipairs(board.glows) do
        list[i] = record.area_lights[glow.index + 1]
    end
    return list
end

local function lit_glows()
    local count = 0
    for _, entry in ipairs(glow_lights()) do
        if entry.intensity > 0 then
            count = count + 1
        end
    end
    return count
end

-- Every block cube carries the shared crystal textures: bevel detail in colour, glowing rims in emission.
for _, node in ipairs({ board.cells[1], board.cells[200], board.active[1], board.ghost[4] }) do
    assert(node.material.texture_base_color == "Textures/crystal.png", "blocks use the crystal colour texture")
    assert(node.material.texture_emissive == "Textures/crystal_glow.png", "blocks glow through the crystal rims")
end

local game = Rules.new(42)
board:sync(game, "ready")
assert(board.cells[191].visible and visible(board.cells) == 27, "ready screen shows the 27 decorative blocks")
assert(#board.glows == 4 and lit_glows() == 0, "no piece glow on the ready screen")

board:sync(game, "playing")
assert(visible(board.cells) == 0, "a fresh game shows no settled blocks")

-- Hidden cells wait, hidden, on their own grid spots; set_visible(false) keeps them out of every pass.
local function at_home(nodes)
    for i, node in ipairs(nodes) do
        local x, y = board:world((i - 1) % 10 + 1, 3 + math.floor((i - 1) / 10))
        local home = near(node.position[1], x, 1e-9) and near(node.position[2], y, 1e-9)
        if node.visible or not home then
            return false
        end
    end
    return true
end
assert(at_home(board.cells), "hidden cells stay hidden on their grid spots")

-- At spawn the piece sits in the hidden rows 1-2, so none of its cubes show.
assert(game.active.y == 1)
assert(visible(board.active) == 0, "cubes in the hidden spawn rows stay hidden")
assert(lit_glows() == 0, "cells in the hidden rows cast no glow")

game:soft_drop()
game:soft_drop()
board:sync(game, "playing")
assert(visible(board.active) == 4, "the piece shows once it is inside the board")
for i, cell in ipairs(Rules.cells(game.active.kind, game.active.rotation)) do
    local position = board.active[i].position
    assert(near(position[1], game.active.x + cell[1] - 5.5, 1e-9), "column maps to x - 5.5")
    assert(near(position[2], 22 - (game.active.y + cell[2]) + 0.5, 1e-9), "row maps to 22 - y + 0.5")
end
assert(visible(board.ghost) == 4, "the ghost shows the landing cells")

-- The piece glows onto the well in its own shape: one cell-sized area light in front of each cell.
assert(lit_glows() == 4, "four area lights glow behind the piece")
for i, cell in ipairs(Rules.cells(game.active.kind, game.active.rotation)) do
    local glow = glow_lights()[i]
    assert(near(glow.position[1], game.active.x + cell[1] - 5.5, 1e-9), "each glow sits behind its own cell")
    assert(near(glow.position[2], 22 - (game.active.y + cell[2]) + 0.5, 1e-9), "each glow sits behind its own cell")
    assert(glow.position[3] > 0.45 and glow.width == 1 and glow.height == 1, "a cell-sized glow in front of the cube")
end

local landing = game:ghost_y()
while game.active.y < landing do
    game:soft_drop()
end
board:sync(game, "playing")
assert(visible(board.ghost) == 0, "no ghost while the piece sits on its landing row")

-- After load, material.set only re-colours cells whose kind changed and the next piece.
local dropped = {
    kind = game.active.kind,
    rotation = game.active.rotation,
    x = game.active.x,
    y = landing,
}
local base_sets = record.material_sets.base_color or 0
local event = game:hard_drop()
board:sync(game, "playing")
assert(event and visible(board.cells) == 4, "the locked piece becomes four settled cells")
assert((record.material_sets.base_color or 0) - base_sets == 8, "four new cells and four next-piece cubes re-coloured")

for _, cell in ipairs(Rules.cells(dropped.kind, dropped.rotation)) do
    local index = (dropped.y + cell[2] - 3) * 10 + dropped.x + cell[1]
    local glow = board.cells[index].material.emissive
    assert(board.cells[index].visible, "settled cell " .. index .. " is shown")
    assert(near(glow.x, Board.HUES[dropped.kind][1] * Board.SETTLED_GLOW, 1e-9), "settled cells glow")
end

board:sync(game, "ready")
assert(lit_glows() == 0 and visible(board.active) == 0, "leaving play hides the piece and its glow")
assert(record.created_while_frozen == 0, "no nodes or new material properties after load")

-- Board: effects -----------------------------------------------------------------------------------

local function emitted_since(mark)
    local list = {}
    for i = mark + 1, #record.bursts do
        list[#list + 1] = record.bursts[i]
    end
    return list
end

local function clear_event(rows)
    return {
        kind = "clear",
        lines = #rows,
        rows = rows,
        points = 100,
        drop = 0,
    }
end

-- A hard drop throws sparks under the landing cells and jolts the camera a little.
local other = Rules.new(7)
other:soft_drop()
other:soft_drop()
board:sync(other, "playing")
local bottom = 0
for _, cell in ipairs(Rules.cells(other.active.kind, other.active.rotation)) do
    bottom = math.max(bottom, other:ghost_y() + cell[2])
end
stage.shake = 0
local mark = #record.bursts
board:impact(other:hard_drop(), true)
local sparks = emitted_since(mark)
assert(#sparks == 1 and sparks[1].texture_index == stage.textures.spark, "a hard drop emits one spark burst")
assert(near(sparks[1].position.y, 22 - bottom + 0.5 - 0.45, 1e-9), "sparks start under the landed piece")
assert(stage.shake == 1 and stage.shake_strength >= 0.08, "a hard drop jolts the camera")
board:sync(other, "playing")

-- Moves applied in the same frame as the drop still put sparks under the cells that actually locked.
other:soft_drop()
other:soft_drop()
board:sync(other, "playing")
other:move(-1)
other:move(-1)
other:rotate(1)
local moved = {
    kind = other.active.kind,
    rotation = other.active.rotation,
    x = other.active.x,
    y = other:ghost_y(),
}
local expected_x = 0
local expected_bottom = 0
for _, cell in ipairs(Rules.cells(moved.kind, moved.rotation)) do
    expected_x = expected_x + moved.x + cell[1] - 5.5
    expected_bottom = math.max(expected_bottom, moved.y + cell[2])
end
mark = #record.bursts
board:impact(other:hard_drop(), true)
local late = emitted_since(mark)[1]
assert(near(late.position.x, expected_x / 4, 1e-9), "sparks follow the piece moved in the drop frame")
assert(near(late.position.y, 22 - expected_bottom + 0.5 - 0.45, 1e-9), "sparks sit under the locked cells")
board:sync(other, "playing")

-- One line: two shard bursts, a light sweep along the row and a flash light.
mark = #record.bursts
board:impact(clear_event({ 22 }), true)
local shards = emitted_since(mark)
assert(#shards == 2 and shards[1].texture_index == stage.textures.shard, "a line clear emits two shard bursts")
assert(visible({ board.sweeps[1].node }) == 1 and board.sweeps[1].life > 0, "the row gets a light sweep")
assert(board.flashes[1].life > 0, "the clear starts a flash light")
assert(record.point_lights[board.flashes[1].light.index + 1].intensity == 0, "the flash lights on the next update")
board:update(0.01, true)
assert(record.point_lights[board.flashes[1].light.index + 1].intensity > 0, "the flash light is lit")

board:update(0.3, true)
assert(board.sweeps[1].life == 0 and not board.sweeps[1].node.visible, "the sweep ends within 0.3 s")
assert(board.flashes[1].life == 0, "the flash ends within 0.3 s")
assert(record.point_lights[board.flashes[1].light.index + 1].intensity == 0, "the flash light goes dark")

-- Four lines: bursts stay bounded, bloom flares and the camera jolts harder.
stage.pulsing = false
mark = #record.bursts
board:impact(clear_event({ 19, 20, 21, 22 }), true)
assert(#emitted_since(mark) == 8, "at most two bursts per cleared row")
assert(stage.pulsing and stage.shake_strength >= 0.25, "a four-line PRISM flares bloom and jolts the camera")
board:update(1.0, true)

-- FX off (user request 2026-10-15): no bursts, sweeps or flash lights, no jolt or flare.
stage.shake = 0
stage.pulsing = false
mark = #record.bursts
board:impact(clear_event({ 19, 20, 21, 22 }), false)
board:impact({
    kind = "lock",
    lines = 0,
    rows = {},
    points = 10,
    drop = 5,
    piece = "T",
    cells = { { 4, 20 }, { 5, 20 }, { 6, 20 }, { 5, 19 } },
}, false)
assert(#emitted_since(mark) == 0, "FX off emits no particle bursts")
for _, sweep in ipairs(board.sweeps) do
    assert(sweep.life == 0, "FX off starts no sweep")
end
assert(board.flashes[1].life == 0 and board.flashes[2].life == 0, "FX off starts no flash light")
assert(stage.shake == 0 and not stage.pulsing, "FX off neither jolts nor flares")
board:update(1.0, false)
other.level = 2
board:sync(other, "playing")
assert(#emitted_since(mark) == 0, "FX off: a level up bursts nothing")
other.level = 1
board:sync(other, "playing")
board:update(0.01, true)

-- A level up emits one burst and retints the aurora.
stage:shift(1)
mark = #record.bursts
other.level = 2
board:sync(other, "playing")
assert(#emitted_since(mark) == 1 and stage.tint_target[1] ~= 1, "a level up bursts and shifts the aurora tint")

-- Top out fades the settled blocks; a new game restores them and stops running effects.
local settled_index
for i = 1, 200 do
    if board.kinds[i] then
        settled_index = i
        break
    end
end
local settled_hue = Board.HUES[board.kinds[settled_index]]
board:impact({
    kind = "game_over",
    lines = 0,
    rows = {},
    points = 0,
    drop = 0,
}, true)
board:impact(clear_event({ 22 }), true)
board:update(2.0, true)
local faded = board.cells[settled_index].material.emissive
local faded_glow = settled_hue[1] * Board.SETTLED_GLOW * Board.FADE_FLOOR
assert(near(board.fade, Board.FADE_FLOOR, 1e-9) and near(faded.x, faded_glow, 1e-9), "top out fades blocks")

board:impact(clear_event({ 22 }), true)
board:reset()
local restored = board.cells[settled_index].material.emissive
assert(near(restored.x, settled_hue[1] * Board.SETTLED_GLOW, 1e-9) and not board.fading, "reset restores full glow")
for _, sweep in ipairs(board.sweeps) do
    assert(sweep.life == 0 and not sweep.node.visible, "reset stops sweeps")
end
for _, flash in ipairs(board.flashes) do
    assert(flash.life == 0, "reset stops flash lights")
end

-- Jev's board at x = +7.5: hidden it shows no node at all, shown it repaints from its own game, and as a
-- non-lead board its effects never reach the shared camera, bloom or aurora.
local function owned(target)
    local nodes = {}
    for _, list in ipairs({ target.cells, target.active, target.ghost, target.bars }) do
        for _, node in ipairs(list) do
            nodes[#nodes + 1] = node
        end
    end
    for _, sweep in ipairs(target.sweeps) do
        nodes[#nodes + 1] = sweep.node
    end
    return nodes
end

local function lit(target)
    local count = 0
    for _, glow in ipairs(target.glows) do
        if record.area_lights[glow.index + 1].intensity > 0 then
            count = count + 1
        end
    end
    return count
end

local rival_game = Rules.new(42)
rival_game:soft_drop()
rival_game:soft_drop()
rival:sync(rival_game, "playing")
assert(#rival.bars > 0 and visible(rival.bars) == #rival.bars, "a shown board draws its well")
assert(visible(rival.active) == 4 and lit(rival) > 0, "a shown board draws its piece and glow")
local wx = rival:world(1, 3)
assert(near(wx, 7.5 - 4.5, 1e-9), "Jev's board sits at x = +7.5")

rival:set_shown(false)
assert(visible(owned(rival)) == 0 and lit(rival) == 0, "a hidden board shows no node and no glow")
rival:sync(rival_game, "playing")
rival:update(0.1, true)
rival:impact(rival_game:hard_drop(), true)
assert(visible(owned(rival)) == 0 and lit(rival) == 0, "a hidden board stays hidden through sync and effects")

rival_game:soft_drop()
rival_game:soft_drop()
rival:set_shown(true)
rival:sync(rival_game, "playing")
assert(visible(rival.bars) == #rival.bars, "showing the board brings its well back")
assert(visible(rival.cells) == 4 and visible(rival.active) == 4, "showing the board repaints its game")
rival:set_shown(false)
rival:set_shown(true)
rival:sync(rival_game, "playing")
assert(visible(rival.cells) == 4, "cells painted before a hide show again")

stage.shake = 0
stage.pulsing = false
local tint = {}
stage.tint_target = tint
mark = #record.bursts
rival:impact(rival_game:hard_drop(), true)
rival:impact(clear_event({ 19, 20, 21, 22 }), true)
rival_game.level = 3
rival:sync(rival_game, "playing")
assert(#emitted_since(mark) > 0, "Jev's board still bursts on its own board")
assert(stage.shake == 0 and not stage.pulsing, "a non-lead board never shakes the camera or flares bloom")
assert(stage.tint_target == tint, "a non-lead board never shifts the aurora")
assert(record.created_while_frozen == 0, "effects create no nodes and no new material properties")

print("PASS PRISM render: stage, board, effects")
