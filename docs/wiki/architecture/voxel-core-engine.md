# Voxel Core Engine (Phase 1)

A walkable, editable cube-voxel world that renders through the existing GBuffer/indirect/cull
path. Permanent C++ subsystem under `Phasma/Runtime/Code/Voxel/`. Phase 1 = flat terrain, greedy
meshing, break/place edits, CPU collision, streaming around an anchor.

## How it renders (the load-bearing idea)

Voxel section meshes are appended as first-class Scene meshes into a **pre-reserved arena inside the
shared `Scene` geometry buffer** — no `UploadBuffers`/`combined_Geometry_buffer` rebuild per add.
They inherit GPU frustum culling, shadows, and the GBuffer path for free. `CullingCS` lets voxel
arena draws through the frustum test into a voxel-only filtered indirect bucket; `GbufferPass` binds
a dedicated voxel pipeline + `Texture2DArray` atlas for those draws (not standard PBR).

The arena contract lives on `Scene` (because `GbufferPass` binds only `Scene::GetBuffer()` and sizes
cull buffers by `GetMeshCount()`): `Scene::ReserveArenaCapacity` (one-time), `AddArenaMesh`,
`RemoveArenaMesh`. Three gotchas this path must honor (each silently breaks rendering if missed):

- **Regrow `m_indirectAll` + `m_meshConstants` (+DX12 device mirror) unconditionally** on reserve —
  they are created exact-sized, not pow2-padded like the filtered/occlusion buffers.
- **Arena vertex k must share the same index in BOTH the `Vertex` and `PositionUvVertex` streams** —
  GBuffer binds the first, the depth prepass + shadows bind the second, both with the same
  `vertexOffset`. A naive tail-append makes depth mismatch → reverse-Z EQUAL test rejects every
  fragment → mesh is invisible with no error.
- **Free must neuter the draw's index bytes**, not just zero `m_indirectAll[idx]` — the two-phase
  occlusion path copies emitted draws into persistent filtered buffers the GBuffer reads.

DX12-only: the mesh-constants DEFAULT mirror (`m_meshConstantsDevice`) must be published before the
same-frame cull dispatch reads it (done in `UploadDynamicUniforms`).

## Play-mode lifecycle

The arena lives in the shared Scene buffer, so any Scene buffer rebuild collides with a live arena.
`Scene::CreateMeshConstants` frees route through the frame-fence deletion queue (not immediate), and
`StopRuntimePlaySession` tears the voxel world down before `RestoreSnapshot` wipes the buffers. The
voxel world is a play-scoped, script-created resource, symmetric with script UI screens.

## File map

- `VoxelTypes.h` — constants (`kSectionDim=16`, `kWorldHeight=256`), coord math (floor-div, x→z→y index).
- `BlockStore` / `ChunkSection` / `ChunkColumn` — uint16 block storage; `0 == air`.
- `BlockType` / `BlockRegistry` — block defs + lookup; air auto-registered at id 0.
- `GreedyMesher` (`IChunkMesher`) — sweep-and-merge coplanar faces; tile index packed into `Vertex.joints[0]`.
- `FlatGen` (`ITerrainGenerator`) — flat ground fill.
- `FreeListAllocator` / `GeometryArena` — coalescing byte suballocator + Scene arena integration.
- `VoxelMaterial` — builds the `Texture2DArray` atlas + custom-shader material.
- `VoxelCollider` — Amanatides–Woo DDA raycast + per-axis swept-AABB (`MoveAabb`, resolve order Y→X→Z).
- `VoxelWorld` / `VoxelSystem` — chunk map, streaming pipeline, edits, upload orchestration; idle until `voxel.create`.
- Shaders: `RuntimeAssets/Shaders/Voxel/VoxelGBuffer{VS,PS}.hlsl` + `voxel_gbuffer.passinfo`.

## Lua API (`voxel` table)

Blocks are registered C++-side (default registry) — there is no Lua `register_block`.

- `voxel.create{load_radius=, unload_margin=, ground_y=, upload_budget=}` — build the world on the active scene.
- `voxel.destroy()`
- `voxel.set_anchor(x, y, z)` — stream columns around this point.
- `voxel.get_block(x, y, z) -> id` / `voxel.set_block(x, y, z, id)`.
- `voxel.raycast(ox,oy,oz, dx,dy,dz, maxDist) -> {hit, cell={x,y,z}, adjacent={x,y,z}, normal={x,y,z}}`
  — `adjacent` is the place target (empty cell on the near side of the hit face).
- `voxel.move_aabb(px,py,pz, hx,hy,hz, dx,dy,dz) -> {x,y,z}` — swept-AABB slide; resolved center.

## Verification

Verified by running the game, not by tracked unit-test exes (voxel logic tests are deliberately kept
out of the repo). The `VoxelCraft` project in PhasmaProjects (`voxelcraft_smoke.lua`,
`voxelcraft_controller.lua`, `voxelcraft.pescene`) is the live walkable/break-place smoke;
`Sample/Assets/Scenes/voxel_playground.{pescene,lua}` is a second walkable demo.

## Follow-ups (Phase 2)

Hi-Z voxel occlusion culling, per-voxel light/AO, transparency (water/glass), noise terrain/biomes,
chunk save/load, LOD (mesher already takes a `lod` stride param). See the spec/plan under
`docs/superpowers/`.
