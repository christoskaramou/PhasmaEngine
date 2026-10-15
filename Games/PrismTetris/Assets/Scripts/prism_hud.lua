-- Retained runtime_ui overlay drawn over the 3D frame: glass cards, score, hold and next previews,
-- buttons, the ready / paused / over card and toasts. The board itself lives in prism_board3d.lua.
local Hud = {}
Hud.__index = Hud

local SCREEN = "prism"
local CLEAR = { 0, 0, 0, 0 }
local WHITE = { 0.91, 0.95, 1, 1 }
local MINT = { 0.42, 0.96, 0.83, 1 }
local MUTED = { 0.59, 0.68, 0.81, 1 }

-- Race seats: the card above each board and the hold/next column on its outer side (tools/make_art.py).
local SEATS = {
    you = { card_x = 410, column_x = 322 },
    jev = { card_x = 860, column_x = 1277 },
}

-- Overlay image, widgets that belong to one layout only, and where the shared buttons sit.
local LAYOUTS = {
    solo = {
        image = "hud",
        pause = { 1120, 92, 170, 44 },
        sound = { 1050, 885, 143, 30 },
        motion = { 1202, 885, 88, 30 },
        veil = { 630, 200, 340, 680 },
    },
    versus = {
        image = "hud_versus",
        pause = { 1180, 70, 170, 44 },
        sound = { 1205, 866, 145, 28 },
        motion = { 1205, 898, 145, 28 },
        veil = { 410, 250, 780, 600 },
    },
}

local function previews(prefix, ids)
    for i = 1, 4 do
        ids[#ids + 1] = prefix .. i
    end
    return ids
end

local function layout_ids(name)
    local ids = {}
    if name == "solo" then
        for _, id in ipairs({ "score", "best", "level", "lines", "hold_hint" }) do
            ids[#ids + 1] = id
        end
        previews("hold", ids)
        for i = 1, 5 do
            previews("next" .. i .. "_", ids)
        end
        return ids
    end

    ids[#ids + 1] = "jev_stats"
    for seat in pairs(SEATS) do
        for _, field in ipairs({ "_score", "_level", "_lines" }) do
            ids[#ids + 1] = seat .. field
        end
        previews(seat .. "_hold", ids)
        for i = 1, 5 do
            previews(seat .. "_next" .. i .. "_", ids)
        end
    end
    return ids
end

LAYOUTS.solo.ids = layout_ids("solo")
LAYOUTS.versus.ids = layout_ids("versus")

function Hud.new(rules)
    runtime_ui.clear(SCREEN)
    runtime_ui.set_screen_overlay(SCREEN, true)
    runtime_ui.show(SCREEN)
    return setmetatable({
        rules = rules,
        layout = "solo",
        scale = 1,
        ox = 0,
        oy = 0,
        dirty = true,
        toast = "",
        toast_time = 0,
    }, Hud)
end

-- "solo" or "versus": hides the other layout's widgets and redraws the overlay on the next render.
function Hud:set_layout(name)
    if name == self.layout then
        return
    end

    runtime_ui.set_many_visible(SCREEN, LAYOUTS[self.layout].ids, false)
    self.layout = name
    self.width = nil
    self.dirty = true
end

function Hud:quad(id, x, y, w, h, options)
    options.x = self.ox + x * self.scale
    options.y = self.oy + y * self.scale
    options.width = w * self.scale
    options.height = h * self.scale
    options.fill = options.fill or CLEAR
    options.border = options.border or CLEAR
    options.accent = CLEAR
    options.no_input = options.no_input ~= false
    options.corner_radius = (options.corner_radius or 0) * self.scale
    options.font_scale = (options.font_scale or 1) * self.scale
    options.offset_x = 0
    options.offset_y = 0
    runtime_ui.set_quad(SCREEN, id, options)
end

function Hud:image(id, path, x, y, w, h, z, alpha, tint)
    local color = tint or WHITE
    self:quad(id, x, y, w, h, {
        style = "image",
        path = "Textures/" .. path .. ".png",
        z = z,
        image_tint = { color[1], color[2], color[3], alpha or 1 },
    })
end

function Hud:text(id, value, x, y, w, h, size, color, z, align)
    self:quad(id, x, y, w, h, {
        style = "text",
        body = tostring(value),
        font_scale = size,
        text_color = color or WHITE,
        z = z or 20,
        align_h = align or "left",
        align_v = "middle",
    })
end

function Hud:button(id, caption, x, y, w, h, primary)
    self:quad(id, x, y, w, h, {
        style = "button",
        title = caption,
        z = 70,
        no_input = false,
        corner_radius = 9,
        font_scale = 1.05,
        fill = primary and { 0.36, 0.87, 0.75, 1 } or { 0.08, 0.11, 0.18, 1 },
        border = primary and CLEAR or { 0.3, 0.36, 0.51, 0.6 },
        text_color = primary and { 0.025, 0.09, 0.11, 1 } or WHITE,
    })
end

function Hud:resize()
    local surface = runtime_ui.get_surface_size()
    local w = surface.width or 1600
    local h = surface.height or 1000
    if w <= 0 or h <= 0 then
        return
    end
    if self.width == w and self.height == h then
        return
    end

    self.width = w
    self.height = h
    self.scale = math.min(w / 1600, h / 1000)
    self.ox = (w - 1600 * self.scale) / 2
    self.oy = (h - 1000 * self.scale) / 2
    self.dirty = true
    self:image("hud", LAYOUTS[self.layout].image, 0, 0, 1600, 1000, 0)
end

function Hud:preview(id, kind, cx, cy)
    local ids = {}
    for i = 1, 4 do
        ids[i] = id .. i
    end

    if not kind then
        runtime_ui.set_many_visible(SCREEN, ids, false)
        return
    end

    local cells = self.rules.cells(kind, 0)
    local minx = 4
    local maxx = 0
    local miny = 4
    local maxy = 0
    for _, c in ipairs(cells) do
        minx = math.min(minx, c[1])
        maxx = math.max(maxx, c[1])
        miny = math.min(miny, c[2])
        maxy = math.max(maxy, c[2])
    end

    for i, c in ipairs(cells) do
        local x = cx + (c[1] - (minx + maxx) / 2) * 23 - 14
        local y = cy + (c[2] - (miny + maxy) / 2) * 23 - 14
        self:image(ids[i], "block_" .. kind, x, y, 28, 28, 10)
    end
end

function Hud:sidebar(game, state)
    self:preview("hold", game.held, 430, 292)
    self:text(
        "hold_hint",
        game.held and "C  /  SWAP" or "C  /  SAVE A PIECE",
        330,
        332,
        200,
        18,
        0.7,
        { 0.43, 0.52, 0.65, 1 },
        20,
        "center"
    )
    for i = 1, 5 do
        self:preview("next" .. i .. "_", game.next[i], 1170, 292 + (i - 1) * 73)
    end

    self:text("score", string.format("%06d", game.score), 330, 452, 200, 57, 2.35)
    self:text("best", string.format("%06d", state.best), 425, 535, 100, 25, 0.85, MINT, 20, "right")
    self:text("level", string.format("%02d", game.level), 330, 659, 85, 49, 1.9)
    self:text("lines", string.format("%03d", game.lines), 449, 659, 85, 49, 1.9)
end

-- One race seat: score, level and lines on the card above its board, hold and next on its outer column.
function Hud:seat(seat, game)
    local layout = SEATS[seat]
    local x = layout.card_x
    self:text(seat .. "_score", string.format("%06d", game.score), x + 16, 182, 170, 40, 1.75)
    self:text(seat .. "_level", string.format("%02d", game.level), x + 196, 184, 50, 36, 1.4)
    self:text(seat .. "_lines", string.format("%03d", game.lines), x + 252, 184, 64, 36, 1.4)

    self:preview(seat .. "_hold", game.held, layout.column_x, 318)
    for i = 1, 5 do
        self:preview(seat .. "_next" .. i .. "_", game.next[i], layout.column_x, 456 + (i - 1) * 78)
    end
end

function Hud:race(game, race)
    self:seat("you", game)
    self:seat("jev", race.rival)

    local jev = race.jev
    local stats = "WAITING FOR JEV"
    if jev.calls > 0 then
        stats = string.format("THINKS %d MS   /   %d CALLS   /   $%.4f", jev.think_ms, jev.calls, jev.cost)
    end
    self:text("jev_stats", stats, 860, 858, 330, 22, 0.75, MUTED, 20, "center")
end

-- MENU opens the menu during play and closes it again (user request 2026-10-15).
function Hud:controls(state)
    local layout = LAYOUTS[self.layout]
    local pause = layout.pause
    local sound = layout.sound
    local motion = layout.motion
    self:button("pause", "MENU", pause[1], pause[2], pause[3], pause[4])
    self:button("sound", state.muted and "SOUND OFF" or "SOUND ON", sound[1], sound[2], sound[3], sound[4])
    self:button("motion", state.motion and "FX  ON" or "FX  OFF", motion[1], motion[2], motion[3], motion[4])
end

local MODAL_IDS = {
    "veil",
    "modal",
    "modal_kicker",
    "modal_title",
    "modal_body",
    "modal_detail",
}
local MENU_IDS = {
    "start",
    "solo",
    "versus",
    "key",
    "paste",
    "remove",
    "back",
}

function Hud:modal(state, game, race)
    if state.mode == "playing" then
        runtime_ui.set_many_visible(SCREEN, MODAL_IDS, false)
        runtime_ui.set_many_visible(SCREEN, MENU_IDS, false)
        return
    end

    self:modal_card()
    local results = race and state.mode == "over" and not state.panel
    if state.panel == "key" then
        self:key_text(state.jev)
    elseif results then
        self:race_text(game, race)
    else
        self:modal_text(state, game)
    end

    if not results then
        runtime_ui.set_many_visible(SCREEN, { "modal_detail" }, false)
    end
    self:modal_buttons(state, race)
end

-- The menu: one main action, then the other choices in a row. Jev's key cannot change mid-race.
function Hud:modal_buttons(state, race)
    local primary = { "start", "PLAY   /   ENTER" }
    local row = { { "versus", "VS JEV" }, { "key", "JEV KEY" } }
    if state.panel == "key" then
        primary = { "paste", "PASTE KEY   /   CTRL+V" }
        row = { { "remove", "REMOVE" }, { "back", "BACK" } }
    elseif state.mode == "paused" then
        primary = { "start", "RESUME   /   ENTER" }
        row = { { "solo", "SOLO" }, { "versus", "VS JEV" } }
        if not race then
            row[#row + 1] = { "key", "JEV KEY" }
        end
    elseif race then
        primary = { "versus", "REMATCH   /   V" }
        row = { { "solo", "SOLO" }, { "key", "JEV KEY" } }
    end

    self:button(primary[1], primary[2], 674, 552, 252, 45, true)
    local shown = { [primary[1]] = true }
    local width = (276 - (#row - 1) * 9) / #row
    for i, entry in ipairs(row) do
        self:button(entry[1], entry[2], 662 + (i - 1) * (width + 9), 609, width, 30)
        shown[entry[1]] = true
    end

    local hidden = {}
    for _, id in ipairs(MENU_IDS) do
        if not shown[id] then
            hidden[#hidden + 1] = id
        end
    end
    runtime_ui.set_many_visible(SCREEN, hidden, false)
end

local KEY_MESSAGES = {
    saved = "Saved for this Windows user.",
    removed = "Key removed.",
    invalid = "That is not a Jev key.",
    ["clipboard-empty"] = "Copy your Jev key first.",
}

-- Whether a key is saved and what the last paste or remove did; the key itself is never shown.
function Hud:key_text(jev)
    local title = "Checking..."
    if jev.saved == true then
        title = "Key saved."
    elseif jev.saved == false then
        title = "No key saved."
    end

    local body = KEY_MESSAGES[jev.answer] or "Copy your Jev key, then paste it here."
    if jev.answer == "error" then
        body = jev.key_error or "The key could not be read or changed."
    end

    self:text("modal_kicker", "JEV KEY", 661, 409, 278, 22, 0.8, MINT, 50, "center")
    self:text("modal_title", title, 652, 449, 296, 42, 1.55, WHITE, 50, "center")
    self:text("modal_body", body, 656, 504, 288, 24, 0.9, MUTED, 50, "center")
end

function Hud:modal_card()
    local veil = LAYOUTS[self.layout].veil
    self:quad("veil", veil[1], veil[2], veil[3], veil[4], {
        style = "image",
        z = 40,
        fill = { 0.018, 0.026, 0.06, 0.55 },
    })
    self:quad("modal", 646, 375, 308, 285, {
        style = "image",
        z = 41,
        corner_radius = 16,
        fill = { 0.032, 0.047, 0.092, 0.97 },
        border = { 0.37, 0.49, 0.67, 0.65 },
    })
end

function Hud:modal_text(state, game)
    local titles = {
        ready = "Find your flow.",
        paused = "Take a breath.",
        over = "One more try?",
    }
    local bodies = {
        ready = "Stack. Clear. Breathe.",
        paused = "Your next move can wait.",
        over = string.format("%d lines. %06d points.", game.lines, game.score),
    }

    self:text(
        "modal_kicker",
        state.mode == "over" and "RUN COMPLETE" or "PRISM / MARATHON",
        661,
        409,
        278,
        22,
        0.8,
        MINT,
        50,
        "center"
    )
    self:text("modal_title", titles[state.mode], 652, 449, 296, 42, 1.55, WHITE, 50, "center")
    self:text("modal_body", bodies[state.mode], 656, 504, 288, 24, 0.9, MUTED, 50, "center")
end

-- The results card: who won (or why the race stopped), both runs, and what Jev's calls cost.
function Hud:race_text(game, race)
    local titles = {
        you = "You win.",
        jev = "Jev wins.",
        draw = "Draw.",
    }
    local rival = race.rival
    local jev = race.jev
    local runs = string.format(
        "YOU %06d  %d LINES   /   JEV %06d  %d LINES",
        game.score,
        game.lines,
        rival.score,
        rival.lines
    )
    local detail = race.reason or string.format("JEV  %d CALLS   /   $%.4f", jev.calls, jev.cost)

    self:text("modal_kicker", "RACE COMPLETE", 661, 409, 278, 22, 0.8, MINT, 50, "center")
    self:text("modal_title", titles[race.winner] or "Jev stopped.", 652, 449, 296, 42, 1.55, WHITE, 50, "center")
    self:text("modal_body", runs, 652, 497, 296, 22, 0.72, MUTED, 50, "center")
    self:text("modal_detail", detail, 652, 521, 296, 22, 0.72, MUTED, 50, "center")
end

-- Clear toasts; in a race they name the board that cleared.
function Hud:impact(event, who)
    self.dirty = true
    if not event or event.lines == 0 then
        return
    end

    local names = {
        "SINGLE",
        "DOUBLE",
        "TRIPLE",
        "PRISM / TETRIS",
    }
    local prefix = who and (who .. "   ") or ""
    self.toast = prefix .. names[event.lines] .. "   +" .. event.points
    self.toast_time = 1.5
end

function Hud:effects(dt)
    self.toast_time = math.max(0, self.toast_time - dt)
    if self.toast_time > 0 then
        self:text("toast", self.toast, 555, 906, 490, 30, 1.0, MINT, 30, "center")
        self.toast_visible = true
    elseif self.toast_visible then
        runtime_ui.set_many_visible(SCREEN, { "toast" }, false)
        self.toast_visible = false
    end
end

-- `race` is nil in solo; in a race it carries Jev's game (`rival`), driver stats (`jev`) and the outcome.
function Hud:render(game, state, dt, race)
    self:resize()
    if self.dirty then
        if race then
            self:race(game, race)
        else
            self:sidebar(game, state)
        end
        self:controls(state)
        self:modal(state, game, race)
        self.dirty = false
    end

    self:effects(dt)
end

return Hud
