# Mini-Game Authoring Speed

This page captures the 2026-06-18 recommendation for making small playable
games faster to create in PhasmaEngine.

## Current Bottleneck

Mini-game work still starts too close to raw engine surfaces. A new prototype
often has to recreate project setup, startup scene wiring, camera, input,
runtime UI, object creation, script layout, smoke commands, and known Lua
binding workarounds before gameplay can begin.

The better direction is a reusable MiniGameKit: project templates, preset
scenes, prefab/object libraries, and shared Lua modules that give agents and
humans known slots to fill instead of rebuilding the runway every time.

## Existing Engine Surface

- `PhasmaPlayer` can run standalone project manifests and startup scenes.
- `phasma_project.json` defines the project root, assets root, and startup
  scene contract.
- Runtime-safe Lua bindings exist for scene nodes, primitives, materials,
  camera, animation, physics, audio, input, settings, filesystem, and runtime
  UI.
- `.peprefab` assets already support reusable scene subtrees.
- Runtime UI can be driven through authored scene nodes with `node:set_ui`,
  `node:set_enabled`, and `node:set_visible`.
- Physics trigger callbacks are available through `physics.on_trigger_enter`
  and `physics.on_trigger_exit`.
- Sibling project history exists under `PhasmaProjects`, including
  `PhasmaSpace`; MemPalace also records AgainstTheHero prototype/foundation
  lessons.

## Recommendation

Build a first-class `MiniGameKit` before adding broad new gameplay-specific C++
systems.

### 1. Project Generator

Add a tool like:

```bash
tools/new_game.py --template topdown|isometric|card|space|physics|runner MyGame
```

It should create:

- `phasma_project.json`
- startup `.pescene`
- `Assets/Scripts/global/<game>.lua`
- `Assets/Scripts/game/` module skeleton
- `Assets/Prefabs/`
- `Assets/Scenes/`
- local `run_player` helper
- smoke command/log check

The generator should write executable-local settings only as a reversible local
run step, not as tracked engine state.

### 2. Preset Small Scenes

Create reusable `.pescene` templates:

- `topdown_arena`
- `isometric_board`
- `space_orbit`
- `physics_box`
- `card_table`
- `menu_shell`

Each scene should include a camera, lighting/sky, a minimal floor or play area,
authored HUD nodes, and disabled pools for common gameplay objects.

Use the current scene performance principle: pre-author runtime objects and UI
where possible, then drive them with enable/disable and content updates. Avoid
hot-path runtime node/widget creation.

### 3. Prefab And Object Library

Use `.peprefab` as the default reusable-object mechanism.

First useful prefabs:

- player pawn
- enemy
- projectile
- pickup
- trigger volume
- door/gate
- spawner
- health bar
- card
- button
- selection ring
- floor tile
- wall tile
- camera rig

For human-like actors, start with a procedural primitive rig with named parts
and simple clips: `idle`, `walk`, `attack`, `hit`, `death`. Later this can be
replaced or supplemented with real skinned assets.

### 4. Lua GameKit Modules

Create shared Lua modules under a reusable project/template path:

- `gamekit/scene.lua`
- `gamekit/pool.lua`
- `gamekit/actor.lua`
- `gamekit/input.lua`
- `gamekit/camera.lua`
- `gamekit/ui.lua`
- `gamekit/timer.lua`
- `gamekit/tween.lua`
- `gamekit/grid.lua`
- `gamekit/deck.lua`
- `gamekit/wave.lua`
- `gamekit/save.lua`
- `gamekit/audio.lua`

Agents should compose these modules instead of creating a new mini-framework
for each game.

AgainstTheHero history suggests the best pattern is data-driven composition:
actors are specs, animations are named procedural clips, cards are catalog data,
and modes implement a small hook contract.

### 5. Targeted Bindings

More Lua bindings would help, but they should be added where they remove
repeated prototype friction:

- prefab instantiate/query helpers
- node find-by-name/tag helpers
- input action maps
- mouse-to-world and screen-to-world helpers
- save/load JSON helpers
- light creation or practical light control
- runtime UI layout/state helpers
- editor/build-time object duplication from authored pools

Avoid broad game-specific C++ APIs until repeated Lua prototypes prove the need.

### 6. Runtime UI Designer

The highest-value editor tool is a Runtime UI Designer:

- palette of text, number, button, toggle, slider, image, progress, card, and
  inventory-slot controls
- drag/drop onto the actual viewport surface
- save as authored scene runtime UI nodes
- Lua updates values and consumes events instead of creating static layout

This should replace most per-frame `runtime_ui.set_quad` HUD construction in
real mini-games.

### 7. Use Existing Game History

Use MemPalace and sibling projects as seed material:

- `PhasmaSpace` exists under `/mnt/c/Users/Christos/repos/PhasmaProjects`.
- AgainstTheHero memory records a first PhasmaPlayer prototype, an isometric
  demo, a rush auto-battler direction, and a later reusable menu/multi-mode
  foundation.
- Warbound memory records the key performance rule: author pools in scenes, then
  enable/disable and update them at runtime.

When a new mini-game starts, search MemPalace first for similar prototypes and
gotchas before writing fresh framework code.

## Practical First Slice

The fastest useful slice is:

1. Add `tools/new_game.py` with `topdown`, `isometric`, and `card` templates.
2. Add a small `gamekit` Lua folder copied into generated projects.
3. Add one prefab pack with primitive actor, projectile, pickup, trigger, card,
   and health bar.
4. Add a smoke runner that launches `PhasmaPlayer`, waits briefly, and checks the
   log for scene load and Lua errors.

Expected result: simple mini-games should move from idea to playable loop in
under an hour, because the session starts from a working player project rather
than from raw engine setup.
