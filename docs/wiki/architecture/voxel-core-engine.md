# Voxel Core Engine (Phase 1 + Phase 2 partial)

A walkable, editable cube-voxel world that renders through the existing GBuffer/indirect/cull
path. Permanent C++ subsystem under `Phasma/Runtime/Code/Voxel/`. Phase 1 = terrain, greedy
meshing, break/place edits, CPU collision, streaming around an anchor. Phase 2 (in progress):
temporal Hi-Z occlusion, noise terrain with caves, packed vertex stream, per-cascade shadow cull.

## How it renders (the load-bearing idea)

Voxel section meshes are appended as first-class Scene meshes into a **dedicated arena** (packed
`VoxelVertex` + `uint16` index buffers owned by `Scene`, not the shared `m_buffer`). They inherit
GPU frustum culling, shadows, and the GBuffer path. `CullingCS` (`VOXEL_HIZ`) frustum-culls voxel
draws into `m_indirectVoxels`; optional temporal Hi-Z (`VoxelHiZPyramidPass` → `voxelHiZ`) culls
chunks occluded by last frame's voxel-inclusive depth. `GbufferPass` and `ShadowPass` bind a
dedicated voxel pipeline + `Texture2DArray` atlas (not standard PBR).

**Packed vertices (8 B):** `GreedyMesher` emits `VoxelVertex` (section-local pos, face id, AO, tile,
UV). The same buffer feeds `VoxelGBufferVS` and `VoxelShadowVS`; world position =
`Mesh_Constants.aabbMin` + local (where `aabbMin = sectionOrigin + localMin` after tight re-base).

**Tight AABBs:** The mesher tracks `localMin`/`localMax`, re-bases packed positions, and
`GeometryArena` stores a tight world AABB (not the full 16³ cube). This keeps Hi-Z and light-frustum
culling effective on sparse cave/overhang sections.

**Shadows:** `ShadowPass` runs `ShadowCullCS` per cascade (light-frustum test, separate regular +
voxel compact buffers). `DispatchShadowCull` sets `arenaSlotBase = m_meshCount` when no voxel arena
is live so regular scenes classify all draws as non-voxel. Not the camera frustum bucket — off-camera
casters still shadow correctly.

The arena contract on `Scene`: `ReserveArenaCapacity` (one-time), `AddArenaMesh`, `RemoveArenaMesh`.
Gotchas (each silently breaks rendering if missed):

- **Regrow `m_indirectAll` + `m_meshConstants` (+DX12 device mirror) unconditionally** on reserve.
- **Do not add regular Scene meshes after the arena is live** without the defer-one-frame pattern
  (highlight mesh before `voxel.create` in VoxelCraft).
- **Free must neuter the draw's index bytes**, not just zero `m_indirectAll[idx]` — occlusion paths
  copy emitted draws into persistent filtered buffers.

DX12-only: the mesh-constants DEFAULT mirror (`m_meshConstantsDevice`) must be published before the
same-frame cull dispatch reads it (done in `UploadDynamicUniforms`).

## Play-mode lifecycle

The arena uses dedicated GPU buffers; `StopRuntimePlaySession` tears the voxel world down before
`RestoreSnapshot` wipes Scene buffers. The voxel world is play-scoped, script-created via
`voxel.create`, symmetric with script UI screens.

**Debug builds:** `load_radius` is clamped to 12 in `VoxelWorld::Create` (`PE_DEBUG`) so CPU
meshing/noise does not stall for tens of seconds. `PhasmaRuntime` compiles with `/O1` in Debug;
`NoiseGen` uses 1 cave octave instead of 2. Use Release for large-radius perf work.

## File map

- `VoxelTypes.h` — constants (`kSectionDim=16`, `kWorldHeight=256`), coord math.
- `BlockStore` / `ChunkSection` / `ChunkColumn` — uint16 block storage; `0 == air`.
- `BlockType` / `BlockRegistry` — block defs + lookup; air auto-registered at id 0.
- `GreedyMesher` (`IChunkMesher`) — sweep-and-merge; packed `VoxelVertex` + tight bounds; cardinal +
  diagonal neighbor-column AO at horizontal seams (`BlockSampleFn` hot path, no `std::function`).
  `lod N` meshes `(16 >> N)`-cell grids where a cell is solid if ANY block in its `2^N`-cube is opaque
  (conservative silhouette — coarse never dips below fine, so band seams can't open holes), keeps
  cell-resolution AO at lod 1 (skipped at lod 2 — invisible at that distance), and always caps section
  walls horizontally instead of sampling neighbor columns. Positions stay in block units, so the
  packed format and shaders are lod-agnostic. Bands widen with distance: lod 0 in `[0, r)`, lod 1 in
  `[r, 3r)`, lod 2 beyond (`r` = `lod0_radius` columns). Band distance is Euclidean and 3D — circular
  rings around the anchor, and anchor height above `ground_y` counts, so a high top-down camera
  coarsens the whole view while an underground anchor keeps its full-detail core; anchor movement
  (including vertical) re-sweeps the bands once it drifts half a section. Band transitions have
  hysteresis (a live column only re-bands once the anchor is clearly past the boundary — no edge
  flip-flop) and swap instantly; the `fog` scene setting (distance haze toward the skybox color)
  masks the far ones. A screen-door cross-fade between the old/new meshes was tried and REVERTED
  2026-07-03: dithering between silhouettes that differ by blocks interleaves dark step-wall pixels
  into the surface — reads as dark-green mud, worse than the pop. **Generation is lod-aware too**
  (`ITerrainGenerator::Generate(col, lod)`): coarse bands generate coarse block data — one height
  sample per cell, no caves — which is where big radii win their load time (the per-block 3D cave fbm
  dominates). A column whose band later drops regenerates finer data in the background (old meshes
  stay live until the swap, then the column and its lod-0 neighbor seams remesh); block queries in
  coarse bands are approximate, and `SetBlock` out there queues the edit and forces a full-detail
  regen so edits are never lost or persisted against a coarse baseline.
- `NoiseGen` (`ITerrainGenerator`) — domain-warped FBM + ridged hills, worm-tunnel caves (default terrain).
  Shape knobs via `NoiseParams`: amplitude (0 = flat plain), feature wavelength, seed (domain shift),
  caves on/off, sea level (<0 = auto groundY-2, 0 = none). Defaults reproduce the historical look.
- `MapGen` (`ITerrainGenerator`) — layered heightmap terrain, selected when the config sets a heightmap
  path (falls back to noise if the image fails to load). Layer 0 = grayscale surface map: pixel value
  0..255 == surface height in blocks (1:1 with `kWorldHeight`), bilinearly sampled, one pixel spans
  `blocksPerPixel` blocks, centered on the world bounds center. Optional strata maps paint the
  thickness of the material bands under the surface block (thickness, not absolute height — strata
  can't invert through the surface); below the last band `fillBlock` fills to y=0 (0 = air →
  floating-island shells). A bounded non-streaming world with `worldRadius` 0 derives its radius from
  the map extent, so a painted map IS the island.
- `FreeListAllocator` / `GeometryArena` — coalescing suballocator + Scene arena; `GrowIfNeeded`.
- `VoxelMaterial` — builds the `Texture2DArray` tile atlas; bound via `Scene::SetVoxelAtlasView` (no Material object).
- `VoxelCollider` — DDA raycast + swept-AABB (`voxel.move_aabb`).
- `VoxelWorld` / `VoxelSystem` — chunk map, streaming, edits; idle until `voxel.create`.
  Cardinal-neighbor gen→mesh gate (no mesh until adjacent columns finish generating). Edits mark
  seam-adjacent sections dirty (vertical + horizontal + diagonal). `RemeshNeighborSeams` remeshes only
  seam-touching sections (cardinal: 2-voxel face slice; diagonal: 2×2 corner patch).
  `ColumnChunkStore` persists touched sections as sparse `.pevcol` overlays (procedural baseline +
  saved edits on load). Columns flush on unload (`ReleaseColumn`) and on `voxel.destroy()` /
  `voxel.save_all()`. All-air sections that were not edited this session are pruned on save; an
  emptied column file is deleted when no sections remain.
- Shaders: `VoxelGBuffer{VS,PS}.hlsl`, `VoxelShadowVS.hlsl`, `voxel_gbuffer.passinfo`.
- Render: `VoxelHiZPyramidPass` (temporal Hi-Z from post-G-buffer depth), `ShadowCullCS.hlsl`.

## Editor authoring — the "Voxel World" node

One `Component_VoxelWorld` node per scene (`NodeVoxelWorldTag`) holds every voxel-world setting:
enabled, streaming on/off, anchor-follows-camera, load radius, unload margin, upload budget, ground Y,
world radius (total bound in columns around the node; 0 = infinite), LOD on/off + full-detail radius,
save dir, plus the full worldgen block — noise knobs (mountain height, feature scale, seed, caves, sea
level) or heightmap mode (surface map + strata maps + per-band block ids + blocks-per-pixel + a
features map — pixel 1 = tree, 2 = rock, 3 = road, 4 = olive, 5 = cypress, spawned deterministically at
that pixel's center block by
`MapGen::SpawnFeatures` with a 3-block seam margin so canopies crossing column borders match; skipped
on coarse LOD bands; the inspector switches views on whether a heightmap path is set). Maps may be any
size and non-square (extent = size x blocks-per-pixel per axis). Tree/rock features use the wood (id 5,
bark + ring-end tiles) and leaves (id 6) blocks, atlas layers 4-6; road (id 3) paves the surface block
with cobblestone (block id 7, atlas layer 7) — draw a connected line of id-3 pixels for a continuous
road (contiguous at blocks-per-pixel 1), and grade the heightmap along it so the route stays walkable.
Olive (id 4, short trunk + silvery `olive_leaves` id 20) and cypress (id 5, tall narrow `cypress_leaves`
id 21) are tree variants. Feature values >= 64 paint block (value - 64) directly onto the surface (the
Map Painter's Block palette), so any block can be drawn onto the terrain.

**Elevation bands** (`surfaceBands`, inspector "Elevation Bands" checkbox / Lua `surface_bands`): when on,
MapGen picks the top block by height-above-sea instead of one `surfaceBlock` — sand (id 8, ≤ sea+2),
dry_grass (id 9, ≤ sea+35), rock (id 10, ≤ sea+70), snow (id 11, above) via `MapGen::BandBlock` (thresholds
hardcoded, tuned for the Greece map). The strata below stay unchanged. The block atlas is 22 32×32 tiles
(`VoxelMaterial::Build` order = atlas layer): 0 grass, 1 dirt, 2 stone, 3 water, 4 wood bark, 5 wood ends,
6 leaves, 7 cobblestone, 8 sand, 9 dry_grass, 10 rock, 11 snow, 12 gravel, 13 marble, 14 limestone,
15 terracotta, 16 whitewash, 17 blue_plaster, 18 roof_tile, 19 marble_column, 20 olive_leaves,
21 cypress_leaves. Build materials (marble..roof_tile, columns) are placeable blocks for villages/temples.
A "Rebuild World" button forces a
recreate — the only way to pick up a repainted map file behind an unchanged path (also scriptable via
`set_voxel_world{rebuild=true}`). Create it from the hierarchy Add menu ("Voxel World") or `node:set_voxel_world{...}` /
`node:get_voxel_world()` from Lua; the inspector edits it live. `VoxelSystem::ReconcileComponentWorld`
keeps the live world in sync every frame: create on enable, recreate on structural change (debounced 30
frames so inspector drags don't thrash), `SetLod0Radius` retunes LOD live without a rebuild, destroy on
disable/delete. The node's position is the volume center for bounded / non-streaming worlds. A world
whose shared arena got wiped by a scene rebuild (scene load, play-stop restore) is detected via
`IsArenaAlive` and rebuilt. Script worlds (`voxel.create`) take precedence while healthy; every
play-exit path (Stop button's `StopRuntimePlaySession` AND `engine.set_play_mode(false)`) destroys the
script world so a stale one can't shadow the component. Stale `VoxelWorldHost` nodes (saved or
snapshotted while a world was live) are deleted on the next world create. Section size (16) and block
size (1 unit) are engine constants baked into the packed vertex format — not per-world settings.

### Map Painter widget

The "Map Painter" window (`Widgets/MapPainter.{h,cpp}`, Window menu) paints the node's MapGen input
maps in-editor instead of an external paint app. Pick the layer (surface height / strata 1 / strata 2
thickness / features — each keeps its own unsaved buffer), brush over the canvas (size/strength,
linear falloff, segment-interpolated strokes; the full-canvas `InvisibleButton` so a drag paints
instead of moving the window). The canvas navigates with a manual pan/zoom (not child scroll):
**right-drag pans**, **mouse wheel zooms toward the cursor** (0.1x fit-the-map out to one map pixel
filling the view; the Zoom slider is logarithmic over the same range), and **Alt+LMB teleports the
editor camera** over that map spot in world X/Z (keeping its current height — inverts MapGen's
pixel↔world mapping via the Voxel World node's center). LMB paints only over the image; the margins are
pan/zoom-only. Gray-layer brush types: Raise/Lower (LMB adds, Shift+LMB
subtracts), Smooth (blend toward the 3x3 neighborhood average), Flatten (pull toward the value under
the stroke start), Set Value (pull toward an explicit 0-255 target; strength 64 = full effect per
stamp). The Features layer instead stamps Tree / Rock / Olive / Cypress / Block / Erase: the tree-type stamps
scatter sparse dots on a deterministic jittered grid (Spacing control; re-dragging the same area never
densifies); **Block** solid-paints any block onto the surface — a tile-thumbnail palette (loaded from
`RuntimeAssets/Textures/Voxel/`) picks the block, and the map stores `kBlockPaintBase (64) + blockId`,
which `SpawnFeatures` reads as "replace the surface top with that block" (so any of the 22 tiles can be
drawn straight onto the terrain). All previewed as colored dots/patches over a dimmed surface-height
underlay. On the Features layer **Ctrl+LMB erases** whatever the brush covers regardless of the selected
type (gray height/strata layers are unaffected). Then "Save + Rebuild" encodes the PNG through the
shared `pmcp::EncodeRGBA_PNG` screenshot encoder, writes it to the layer's path (Assets-relative or
absolute, resolved like MapGen via `ColumnChunkStore::ResolveRoot`) and sets `rebuildRequested` — the
same path as the inspector's Rebuild World button. A layer with no path (or a missing file) gets a
create panel (path, width x height, fill value; features maps start empty) that writes the fresh map
immediately, and a loaded map can be resampled to a new size with the Resize field (bilinear; features
nearest). The node's Blocks/Pixel is editable in the painter too, with a live "px = blocks" coverage
readout — raise it to span a bigger world from the same small texture (heights lerp between pixels). The preview is a nearest-sampled RGBA texture re-uploaded via `CopyDataToImageStaged` on
edit. Agents can paint without mouse input through editor actions: `voxelpainter.layer` (`layer` 0..3),
`voxelpainter.stroke` (`u`/`v` 0..1 across the map, optional `radius`/`strength`/`lower`/`value` and
`brush` raise|smooth|flatten|set on gray layers or tree|rock|erase on the features layer),
`voxelpainter.save`. Note the map center pixel sits at world `boundsCenterC*16 + 8`, not the node
origin. Painting top-down onto the terrain view itself is the planned follow-up.

## Lua API (`voxel` table)

Blocks are registered C++-side — there is no Lua `register_block`.

- `voxel.create{load_radius=, unload_margin=, ground_y=, upload_budget=, lod0_radius=, world_radius=,
  streaming=, save_dir=}` — build the world. `save_dir` is relative to the project Assets root (or
  absolute); enables column `.pevcol` persistence. `lod0_radius` (columns) enables distance LOD: full
  detail inside it, one mip per band beyond (capped at lod 2 = 4-block cells); 0/absent = LOD off.
  Coarse columns skip neighbor snapshots and the neighbor-gen wait; band changes remesh through the
  budgeted remesh path. `world_radius` bounds the world (columns from origin; 0 = infinite);
  `streaming=false` loads a fixed grid once and ignores the anchor. Worldgen keys (same model as the
  Voxel World node): `noise_amplitude`, `noise_feature_scale`, `noise_seed`, `caves`, `sea_level`,
  and heightmap mode via `heightmap`, `strata1_map`, `strata2_map`, `features_map` (pixel 1 = tree,
  2 = rock, 3 = road, 4 = olive, 5 = cypress), `blocks_per_pixel`, `surface_block`, `surface_bands`,
  `strata1_block`, `strata2_block`, `fill_block`,
  `strata1_thickness`, `strata2_thickness` (see `MapGen.h`).
- `voxel.destroy()` (auto-saves touched columns when `save_dir` is set)
- `voxel.save_all()` — flush edited column sections to disk immediately
- `voxel.set_anchor(x, y, z)` — stream columns around this point.
- `voxel.get_block(x, y, z) -> id` / `voxel.set_block(x, y, z, id)`.
- `voxel.raycast(ox,oy,oz, dx,dy,dz, maxDist) -> {hit, cell, adjacent, normal}`
- `voxel.move_aabb(px,py,pz, hx,hy,hz, dx,dy,dz) -> {x,y,z}` — swept-AABB slide.

## Verification

`VoxelCraft` in PhasmaProjects (`voxelcraft_controller.lua`, `voxelcraft.pescene`) is the live
walkable/break-place smoke. Enable `occlusion_culling` in scene settings for temporal Hi-Z; pan fast
to check for popping (tune `occlusion_culling_bias` if needed).

## Follow-ups

Non-water transparent block types (water shipped via the dual-stream mesher; glass-like blocks would
reuse the same transparent stream), LOD crack skirts if capped walls ever read as seams up close.
