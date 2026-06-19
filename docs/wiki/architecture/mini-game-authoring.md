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

## Slice 1 — Shipped (2026-06-19)

The first slice landed and was verified by playtest. It refines the proposal
above in a few places (noted inline); the rest of this page is still the
forward roadmap.

### What shipped

1. **`scene.instantiate_prefab(path, parent?)` Lua binding.** Wraps the existing
   `Scene::InstantiatePrefab` (registered in
   `Phasma/Runtime/Code/Script/Bindings/Scene/SceneBindings.cpp`). The path
   resolves against the active project's `Assets/` first, then the engine
   `RuntimeAssets/` tree, with an as-given fallback for absolute paths. Returns
   the instance-root node handle, or `nil` on failure. This is what makes
   `.peprefab` assets reusable from Lua — before slice 1 they were loadable from
   C++/editor only.
2. **Full `gamekit` Lua library.** Canonical source under
   `Phasma/Runtime/RuntimeAssets/Scripts/gamekit/`: `init` (loader + assembly),
   `json`, `loop`, `scene`, `pool`, `actor`, `input`, `camera`, `pick`, `ui`,
   `timer`, `tween`, `wave`, `grid`, `deck`, `save`, `audio`. PhasmaEngine's Lua
   does not wire `require` to the project tree, so `gamekit/init.lua` reads and
   compiles its sibling modules via `fs.read` + `load` (mirroring the
   Warbound/ATH project convention), and a game's entry script bootstraps it the
   same way. `loop` owns the single `script.on_update(..., "play")` tick and fans
   systems out in a stable phase order (`pre` → timer/input, `main` →
   waves/actors, `post` → camera/tween).
3. **`topdown_arena` scene template.** Built programmatically by
   `tools/minigamekit.py` (the single source of truth for the `.pescene` /
   `.peprefab` JSON schema); a reference copy lives at
   `tools/templates/topdown_arena.pescene`. It carries an angled orthographic
   camera, a directional sun, a flat floor, a player sphere, an authored disabled
   pool of 32 `Enemy_NNN` + 64 `Shot_NNN` cubes parked offscreen at `Y=-1000`,
   and a top-left HUD (HP bar + wave label) on the `__scene_ui` screen.
4. **Prefab pack** under `Phasma/Runtime/RuntimeAssets/Prefabs/`: `actor`,
   `projectile`, `pickup`, `trigger`, `health_bar`, `selection_ring`, `card`.
5. **`tools/new_game.py`.** `--name <Name> [--template topdown] [--dir ...]
   [--api vulkan]` emits a self-contained project: `phasma_project.json`,
   `Assets/Scenes/main.pescene`, the entry script `Assets/Scripts/<name>.lua`,
   a pinned copy of `gamekit/` and the prefab pack into `Assets/`, and
   `run_smoke.ps1` (writes `phasma_settings.json` next to the player exe, launches
   `PhasmaPlayer --api vulkan`, and checks the log). `--update-gamekit` refreshes
   the copied snapshot in place.
6. **Launcher integration.** The PhasmaLauncher Editor tab has a **New Project**
   panel (name + template + folder) that creates a project and selects it for the
   editor (project creation is an authoring activity). The
   default **Empty** template is created natively in C++ — the `phasma_project.json`
   manifest via `ProjectConfig::WriteManifest`, plus a minimal camera + sun startup
   scene — with no external dependency. Other templates (currently **Topdown
   mini-game**) shell out to `new_game.py`; that shell-out is cross-platform
   (CreateProcess on Windows, `fork`/`exec` on Linux/WSL, mirroring the launcher's
   existing process-launch split) and finds `tools/new_game.py` by walking up from
   the launcher executable, so it needs Python on `PATH` and the engine tree
   reachable.

### Deltas from the proposal above

- **Templates:** slice 1 ships **topdown only**. `isometric` and `card` (§1, §2)
  are a fast-follow; `gamekit/grid` and `gamekit/deck` already ship for them but
  are not exercised by the topdown smoke.
- **Script layout:** the entry point is the scene's
  `scene_scripts.on_play` manifest → `Assets/Scripts/<game>.lua`, **not** the
  `Scripts/global/<game>.lua` + `Scripts/game/` skeleton sketched in §1.
- **Targeted bindings (§5):** only the prefab-instantiate binding was added in
  C++. Find-by-tag, input action maps, mouse-to-world, and save/load JSON are
  implemented in **pure Lua inside `gamekit`** (`scene.find_all`, `input`,
  `pick`, `save` + `json`) rather than as new engine surfaces.
- **Still deferred:** the Runtime UI Designer (§6), iso/card templates, and the
  procedural human-like rig (§3).

### Verification

Playtested via `new_game.py --name TestTopdown` → `run_smoke.ps1` against the
Vulkan player: scene loads at ~170 FPS with zero Lua errors, the pickup prefab
instantiates through the new binding, wave 1 spawns and (after the enemies home
in and recycle to the pool) wave 2 starts, and a forced movement axis displaces
the player node by the exact expected distance — confirming the
`input → actor:move_dir → set_position` path the WASD controls drive.

## Spatial Awareness — Phase 1 (Shipped 2026-06-19)

A separate track from MiniGameKit: give the agent (and Lua authoring scripts)
**scene perception** so they place objects from facts instead of guessing
coordinates and screenshotting to check. Phase 1 ships the precise-numbers half
(the digest); Phase 2a ships the clean top-down **map shot** + sidecar manifest.

### What shipped

1. **`ComputeSceneDigest(Scene&)`** — a single shared C++ aggregator in
   `Phasma/Runtime/Code/Scene/Scene.cpp` (declared with the `SceneDigest` /
   `SceneDigestNode` structs in `Scene.h`). It walks every mesh-bearing node,
   reading the already-cached world AABB (no GPU work), and returns: aggregate
   `world_bounds`, a `ground_y` estimate, per-node `{id, name, aabb, enabled,
   visible, in_frustum, ground_outlier}`, and pairwise AABB `overlaps`. Cheap
   enough to call per authoring step.
2. **Lua `scene.digest()`** (`SceneBindings.cpp`) and **MCP `get_scene_digest`**
   (`EditorToolRuntime` + `EditorToolCatalog`) both serialize that one struct —
   the Lua path for runtime/console scripts, the MCP path for the agent. Per-node
   `id` is the stable `node:<index>:<revision>` form, so a digest entry
   round-trips straight into `frame_node` / `set_camera` / `get_node_info`.
3. **`gamekit/spatial.lua`** — declarative placement helpers over `scene.digest()`:
   `place_on_ground`, `in_front_of_camera`, `ring`, `grid`, `stack`,
   `resolve_overlaps`, `frame_camera` (and `move_center_to` / `bounds` /
   `ground_y`). They move nodes via `node:set_position` using a world delta —
   exact for scene-root / identity-parent nodes (the common authoring case).

### Key design decisions

- **Aggregate stats gate on `enabled && visible`, not just `enabled`.** Pools are
  parked offscreen by *two* conventions: hierarchy-disable (`enabled=false`, e.g.
  the topdown template parks at `Y=-1000`) **and** the render-visible cull-flag
  (`visible=false`, which avoids TLAS churn — Warbound parks 314 members at
  `Y=-2080` this way). `world_bounds` / `ground_y` / `overlaps` therefore consider
  only enabled-and-visible nodes; without the visible gate, Warbound's bounds
  blew up to `size.y≈2088` / `ground_y≈-2080`. Every mesh node is still **listed**
  with its flags, so the agent can see the parked pool.
- **Robust vertical band rejects far outliers (`ground_outlier`).** The
  `enabled && visible` gate handles pools hidden *at play time*, but a scene viewed
  *before* play can still have a pool authored at `Y=-1000` while enabled+visible
  (Warbound does exactly this). A median+MAD band over live node centres (generous
  50-unit floor, only with ≥8 live nodes) flags those as `ground_outlier` and drops
  them from `world_bounds` / `ground_y` / `overlaps`. Without it, the editor's
  authored Warbound reported `ground_y=-1000`, `size.y≈1008`; with it, `ground_y=-0.5`,
  `76×8.9×76` — matching the play-mode bounds. Outliers are still listed (flagged).
- **Overlaps skip flat-in-Y nodes** (floors / decals, `size.y < 0.05`) so the
  ground plane does not report as overlapping everything on it.
- **Overlaps are capped** at 256 nodes (O(n²)); above that `overlaps_truncated`
  is set and the pass is skipped — expected on large scenes.

### Verification

The topdown template smoke confirmed correct bounds, the `enabled=false`
exclusion, flat-floor de-noising, and a working `place_on_ground` move. A second
smoke ran `scene.digest()` against the live **918-node Warbound skirmish** in the
Vulkan player: it surfaced the render-hidden parking gap above (bounds polluted to
`size.y≈2088`), which the `enabled && visible` gate fixed — re-smoke produced a
clean `76×8.9×76` map with `ground_y=-0.50`, the 314 parked members still listed
but excluded from the aggregate. No Lua errors; player stable.

## Spatial Awareness — Phase 2a: Map Shot (Shipped 2026-06-19)

The agent-readable picture half. The MCP tool **`get_map_shot`**
(`EditorToolRuntime::GetMapShot`) renders a clean top-down view of the scene and
writes a paired manifest — the "annotated image + JSON digest" split, achieved by
**composing existing passes**, with **no new render pass**:

1. Frame an **orthographic top-down camera** over the digest's `world_bounds`
   (`orthographic_size` covers both screen axes via the viewport aspect; look down
   `-Y`); save the prior camera + settings.
2. Let a few frames render, then capture the **`rt:viewport`** RT to PNG via the
   existing `CaptureImageResource` readback. `viewport` is the scene colour *before*
   the editor composites ImGui — `rt:display` in-editor contains the whole UI, so it
   is the wrong target. (`GridPass` draws only to `display`, so the map has no drawn
   grid; coordinates come from the manifest instead.)
3. Write a sidecar **`<image>.map.json`**: for every visible, non-outlier node, its
   AABB projected to **pixel space** (`center_px`, `aabb_px`) keyed to `name`/`id`,
   plus `view_projection`, `camera`, `world_bounds`, `ground_y`. The agent reads the
   picture for layout and the manifest for exact boxes/labels/coords (overlay them
   itself if it wants them drawn).
4. Restore the camera + settings.

Design notes: the image is **clean by default**; `draw_boxes:true` opts into the
engine `AabbsPass` wireframes (noisy on dense scenes — the manifest already carries
the boxes). Args: `padding` (fit margin, default 1.1), `max_dimension` (PNG cap,
default 2048), `draw_boxes` (default false).

**Verified** end-to-end via the editor MCP on the live 918-node Warbound skirmish:
clean top-down render (no UI, no clutter); `Ground` (world origin) projects to the
exact image centre; all in-band nodes project in-frame; camera restored. The first
smoke caught two issues now fixed — `rt:display` carried ImGui (→ `rt:viewport`) and
AABB-in-image webbed the scene (→ opt-in).

### Next (Phase 2b)

A dedicated `MapShotPass` rendering to its own offscreen RT (decoupled from the live
editor camera), worldXZ / node-ID data passes for programmatic pixel→world/node
readback, and adaptive tiling for large maps. Deferred — 2a's compose-and-capture
covers the common case.
