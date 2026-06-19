-- gamekit/init.lua — load every gamekit module and assemble the `kit` table.
--
-- PhasmaEngine's Lua does not wire `require` to the project asset tree, so the
-- project convention (Warbound wb_loader, ATH ath_common) is to read and compile
-- module files via the fs binding. A game's entry script bootstraps gamekit with:
--
--     local kit = (function()
--         local src = fs.read("Scripts/gamekit/init.lua")
--         return load(src, "@Scripts/gamekit/init.lua", "t", _ENV)()
--     end)()
--
-- Every module is compiled with this chunk's _ENV (the entry script's environment,
-- which inherits all engine globals: scene, input, vec3, math, fs, ...), so modules
-- reference engine bindings directly. Classes (pool/actor/wave/grid/deck) are
-- returned as their class table — call kit.pool.new(...), kit.actor.wrap(...), etc.

local GAMEKIT_DIR = "Scripts/gamekit/"

-- Modules in load order. json first (save depends on it); everything else is flat.
local MODULES = {
    "json", "loop", "scene", "pool", "actor", "input", "camera",
    "pick", "spatial", "ui", "timer", "tween", "wave", "grid", "deck", "save", "audio",
}

local function read_module(name)
    local path = GAMEKIT_DIR .. name .. ".lua"
    local src = fs and fs.read and fs.read(path)
    if not src then error("[gamekit] missing module: " .. path) end
    local chunk, err = load(src, "@" .. path, "t", _ENV)
    if not chunk then error("[gamekit] compile error in " .. path .. ": " .. tostring(err)) end
    return chunk()
end

local mods = {}
for _, name in ipairs(MODULES) do
    mods[name] = read_module(name)
end

local kit = {
    json = mods.json,
    loop = mods.loop,
    scene = mods.scene,
    pool = mods.pool,     -- class: kit.pool.new(prefix[, max])
    actor = mods.actor,   -- class: kit.actor.wrap(node, opts)
    input = mods.input,
    camera = mods.camera, -- kit.camera.topdown(node, opts)
    pick = mods.pick,
    spatial = mods.spatial, -- placement helpers over scene.digest()
    ui = mods.ui,
    timer = mods.timer,
    tween = mods.tween,
    wave = mods.wave,     -- class: kit.wave.new(cfg)
    grid = mods.grid,     -- class: kit.grid.new(cfg)
    deck = mods.deck,     -- class: kit.deck.new(specs)
    save = mods.save,
    audio = mods.audio,
}

-- Inject cross-module dependency: save serialises with the json module.
kit.save._json = kit.json

function kit.log(msg)
    if pe_log then pe_log("[gamekit] " .. tostring(msg)) end
end

-- Install the built-in per-frame systems into the loop in canonical phase order
-- (timer/input in "pre", tween in "post") and start the single update callback.
-- Call after wiring your game systems with kit.loop.add(); add-vs-start order does
-- not matter because phases impose the dispatch order. `id` names the on_update
-- registration (default "gamekit").
function kit.start(id)
    kit.loop.add(function(dt) kit.timer.update(dt) end, "pre")
    kit.loop.add(function(dt) kit.input.update(dt) end, "pre")
    kit.loop.add(function(dt) kit.tween.update(dt) end, "post")
    kit.loop.start(id)
    kit.log("started")
end

return kit
