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
  (conservative silhouette — coarse never dips below fine, so band seams can't open holes), skips AO,
  and always caps section walls horizontally instead of sampling neighbor columns. Positions stay in
  block units, so the packed format and shaders are lod-agnostic.
- `NoiseGen` (`ITerrainGenerator`) — domain-warped FBM + ridged hills, worm-tunnel caves (default terrain).
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
save dir. Create it from the hierarchy Add menu ("Voxel World") or `node:set_voxel_world{...}` /
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

## Lua API (`voxel` table)

Blocks are registered C++-side — there is no Lua `register_block`.

- `voxel.create{load_radius=, unload_margin=, ground_y=, upload_budget=, lod0_radius=, world_radius=,
  streaming=, save_dir=}` — build the world. `save_dir` is relative to the project Assets root (or
  absolute); enables column `.pevcol` persistence. `lod0_radius` (columns) enables distance LOD: full
  detail inside it, one mip per band beyond (capped at lod 2 = 4-block cells); 0/absent = LOD off.
  Coarse columns skip neighbor snapshots and the neighbor-gen wait; band changes remesh through the
  budgeted remesh path. `world_radius` bounds the world (columns from origin; 0 = infinite);
  `streaming=false` loads a fixed grid once and ignores the anchor.
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
