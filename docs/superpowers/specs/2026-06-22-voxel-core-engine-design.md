# Voxel Core Engine — Design (Phase 1)

- **Date:** 2026-06-22
- **Status:** Approved (design); pending spec review → implementation plan
- **Owner:** Christos
- **Scope of this doc:** Phase 1 ("Core Voxel Engine") of a multi-phase program to build a faithful Minecraft-like voxel sandbox as a permanent, reusable PhasmaEngine subsystem.

---

## 1. Context & goal

The user asked for a faithful Minecraft-like replica (original assets/identity, Minecraft-like mechanics) built as a **serious, permanent C++ engine subsystem** — cross-backend (Vulkan + DX12), reusable, with API design, perf budgets, and wiki/MemPalace docs — not a throwaway prototype and not a Lua cube-per-block toy.

A faithful replica is a *program*, not a single feature. The strategy is to **design the architecture to support the whole thing, then build it in vertical slices**, each its own spec → plan → implementation cycle. This document specs the first slice.

### Locked decisions (from brainstorming)

| Question | Decision |
|---|---|
| World scale | **Infinite streaming** (endless gen as the player moves) |
| Block geometry fidelity | **Full fidelity ("all")** is the program target; architecture must accommodate non-cube models, fluids, etc. Phase 1 implements full **cubes** only. |
| First spec | **Phase 1 = Core Voxel Engine** |
| Perf bar (Phase 1) | **No hard FPS/render-distance target yet** — get a correct, infinite, editable world running; tune distance/LOD later. |
| LOD | No engine LOD exists; **build a LOD seam** into the voxel subsystem (voxel LOD = coarser voxel sampling, not mesh decimation). Ship LOD-0 in Phase 1. |
| Meshing/integration architecture | **Approach A** — CPU worker-thread greedy meshing on `ThreadPool::General` → `Scene::AddMesh`, reusing the existing render path. |

---

## 2. Program decomposition (roadmap)

Each phase is a separate spec → plan → implementation cycle. Phase 1's architecture leaves a named seam for each later phase.

1. **Core Voxel Engine** (this doc) — storage, infinite async streaming, greedy cube mesher, texture-array + custom shader, render integration, custom collision, break/place raycast, simple terrain. *Result: a walkable, editable, infinite cube world.*
2. **Block-model system** — data-driven block-model registry; stairs/slabs/fences/cross-plants/transparency/non-cube geometry.
3. **Lighting** — flood-fill block light + skylight + smooth AO, day/night.
4. **World generation** — noise terrain, biomes, caves, structures.
5. **Fluids** — flowing water/lava.
6. **Persistence** — region-file save/load.
7. **Gameplay layer** (Lua / PhasmaProjects) — inventory, hotbar, crafting, mobs, survival.

---

## 3. Phase 1 scope

**In:** chunk storage; infinite async streaming around an anchor; greedy meshing of full cubes; a texture-array block atlas + custom GBuffer shader; render integration via `Scene::AddMesh`; custom swept-AABB collision; DDA ray-pick for break/place; a **flat terrain generator** (single configurable ground height — noise/biomes are Phase 4); a C++ + Lua API; cross-backend correctness (Vulkan + DX12).

**Out (deferred to phases 2–7):** non-cube block models, transparency/water rendering, flood-fill lighting/AO/day-night, noise terrain/biomes/caves/structures, save/load, gameplay (inventory/crafting/mobs).

---

## 4. Architecture

### 4.1 Module layout — `Phasma/Runtime/Code/Voxel/`

Lives in Runtime because it depends on `Scene` / `Mesh` / `Material`. Each type has one responsibility and a narrow interface.

| Type | Responsibility |
|---|---|
| `VoxelWorld` | Top-level object; owns the chunk map + streaming; bound to one `Scene`. Created/destroyed via API. |
| `ChunkColumn` / `ChunkSection` | Voxel storage (a column of 16³ sections). |
| `BlockRegistry` / `BlockType` | Data-driven block definitions. |
| `IChunkMesher` → `GreedyMesher` | Section voxels → CPU vertex/index arrays; takes a `lod` parameter. |
| `ITerrainGenerator` → `FlatGen` (Phase 1) | Fills a column's voxels. Noise/biomes are Phase 4. |
| `VoxelCollider` | Swept-AABB collision + DDA ray-pick against the world. |
| `VoxelSystem : ISystem` | Engine-loop hook; drives `VoxelWorld::Update` (poll async results, budgeted uploads). |
| `voxel` Lua bindings | Scriptable surface. |

### 4.2 Data model

- **World:** infinite in X/Z, bounded in Y. Height configurable, **default 256** (16 sections), extensible to 384 later. Y-up; integer block coordinates.
- **Block id:** `uint16_t` (65,536 types; `0 = air`) — future-proof for "all" blocks.
- **Section:** flat `uint16_t[16³]` = **8 KB/section**, behind a `BlockStore` seam so palette compression can drop in later as a memory optimization. A 256-tall column = 128 KB.
- **Memory check:** ~8-column radius (≈289 columns) ≈ **37 MB** voxel data; 16-radius (≈1089) ≈ 140 MB. Comfortable.

### 4.3 Streaming & threading

- Chunk map keyed by `(cx, cz)`. Load radius `R` (configurable) around an **anchor** position (set via API in Phase 1; the player later). Unload beyond `R + hysteresis`.
- **Two async stages on `ThreadPool::General`** ([ThreadPool.h:11-23](../../../Phasma/Core/Code/Base/ThreadPool.h#L11-L23)):
  1. **Generate** — terrain gen fills column voxels.
  2. **Mesh** — greedy-mesh each dirty section → CPU vertex/index arrays. Border face-culling needs the 6 neighbor sections, so a section meshes only after its neighbors are generated (a "generated" gate).
- **Main-thread `VoxelWorld::Update`** (driven by `VoxelSystem`): poll finished `std::shared_future`s; **upload a bounded number of section meshes per frame** (upload budget → no hitches) via `Scene::AddMesh` + a per-chunk node; retire nodes for unloaded columns.
- **Thread-safety:** a column's voxels are immutable while a mesh job reads them; edits queue and apply on the main thread, which re-dispatches the mesh job. No locks in the hot mesher.

### 4.4 Mesher (`GreedyMesher`)

Standard greedy meshing: for each of 6 face directions, sweep slices and merge coplanar, same-block, visible faces into maximal quads. A face is emitted only if its neighbor is air/transparent.

Output uses the engine `Vertex` struct ([Vertex.h:5-15](../../../Phasma/Core/Code/API/Vertex.h#L5-L15)):
- `position` / `normals` / `tangent` — per-face.
- `color[4]` — left white; **reserved for Phase 3 light/AO bake**.
- **Block-tile index packed into `joints[0]`** (unused for static meshes).
- `uv` — tiled coordinate that may exceed `0..1` across a merged quad.

### 4.5 Block registry + texture array + shader

- `BlockType { uint16_t id; std::string name; bool solid; bool opaque; RenderType renderType; uint16_t faceTiles[6]; }`. Phase 1 registers a handful (air/stone/dirt/grass/…).
- Block textures are assembled into a **`Texture2DArray`** (one layer per tile) bound into one of the material's 5 texture slots ([Material.h:89](../../../Phasma/Runtime/Code/Scene/Material.h#L89)) via a **custom `PassInfoAsset` GBuffer shader**. The shader reads `joints[0]` as the array layer and samples with `frac`-tiled `uv`, so greedy-merged quads tile correctly. This routes around the "5 discrete slots, no built-in texture array" constraint and leans on the existing PassInfoAsset + SPIR-V-reflection pipeline.

### 4.6 Render integration — incremental geometry arena

**Critical constraint discovered during planning:** the Scene geometry path rebuilds wholesale on structural change. [`UploadBuffers`](../../../Phasma/Runtime/Code/Scene/SceneBuffers.cpp#L21) destroys + recreates the entire unified geometry buffer and re-copies all vertices/indices, and [`UpdateGeometryBuffers`](../../../Phasma/Runtime/Code/Scene/Scene.cpp#L370) wraps it in a synchronous `Submit`+`Wait` (full GPU stall) + RT BLAS invalidation. There is an in-place single-mesh overwrite path ([SceneNode.cpp:858-883](../../../Phasma/Runtime/Code/Scene/SceneNode.cpp#L858-L883)) but **no append-a-new-mesh path**. Calling the rebuild per streamed chunk would be fatal.

**Decision (user-approved): add an incremental geometry arena to Scene.** Sub-allocate/free per-section vertex + index byte ranges and add/remove indirect-draw + per-mesh-constant entries incrementally — no full rebuild, no stall. Each non-empty section is thus a first-class `Mesh` that **inherits the engine's existing GPU frustum + Hi-Z occlusion culling, shadows, and indirect draw for free**.

**The arena lives in the SHARED Scene buffer** (verified against code review): [GbufferPass.cpp:180-181](../../../Phasma/Runtime/Code/RenderPasses/GbufferPass.cpp#L180-L181) binds *only* `m_scene->GetBuffer()` and draws via Scene's culling/occlusion buffers sized by `GetMeshCount()` — a *separate* voxel buffer would not be drawn by this pass, so the "inherit culling for free" benefit requires geometry in the shared buffer as real Scene meshes. Concretely this means reserving **capacity** (not exact count) across several buffers at world-create, because today `m_indirectAll` is created exact-sized ([SceneBuffers.cpp:305-306](../../../Phasma/Runtime/Code/Scene/SceneBuffers.cpp#L305-L306)) while visibility / occlusion-SS-A/B / depth buffers use `m_indirectCapacity` (pow2). An incremental add must: pre-grow `m_buffer`; pad `m_indirectAll` + mesh-constants + visibility to a voxel mesh-count capacity; append one indirect + mesh-constants entry; **update the DX12 mesh-constants DEFAULT mirror** (commit `9f1e8cfb`); seed the new visibility slot to 1; and adjust the live `GetMeshCount()`.

- One `Mesh` per non-empty section, shared voxel material, attached to a chunk `SceneNode` at the section origin.
- Trade-off accepted: this touches core Scene buffer/indirect code (blast radius across all scenes), so the arena path is **additive and gated** — default scenes keep the rebuild path; the capacity-reserved arena path is used only when a voxel world exists. Validated by an early spike (§7), which gates the fallback.
- **Fallback if the incremental path proves too invasive:** voxel-owned buffers + a dedicated `VoxelPass` in the render-graph table that writes the same GBuffer targets and does its own frustum cull (loses free Hi-Z occlusion culling). This was the runner-up render-integration option.
- **No new render pass in Phase 1** under the primary path (opaque cubes reuse GBufferOpaque); transparency/water is a later phase.

### 4.7 Collision & ray-pick (`VoxelCollider`, no Jolt)

- **Player:** swept-AABB vs voxel grid — broadphase the AABB's cell range, resolve per-axis against solid blocks, reading CPU voxel data directly.
- **Break/place:** voxel DDA ray-march (Amanatides–Woo) along the view ray to a reach distance → returns hit cell + face normal.
- Jolt ([PhysicsSystem.h:57-76](../../../Phasma/Runtime/Code/Systems/PhysicsSystem.h#L57-L76)) is untouched (reserved for dropped items/entities in a later phase).

### 4.8 Edits

`SetBlock(x,y,z,id)` on the main thread: write voxel → mark that section (and a border neighbor if on a seam) dirty → enqueue remesh → budgeted re-upload swaps the section's `Mesh`. O(1) edit + one ~16³ section remesh.

### 4.9 API surface

- **C++:** `VoxelWorld::Create(Scene&, const VoxelConfig&)`, `SetAnchor(vec3)`, `GetBlock`/`SetBlock`, `Raycast`, `Update`; `BlockRegistry::Register(const BlockType&)`.
- **Lua `voxel` table** (via the `ScriptSystem::AddBindings([](sol::state&){...})` static-struct pattern, e.g. [SceneBindings.cpp:15-20](../../../Phasma/Runtime/Code/Script/Bindings/Scene/SceneBindings.cpp#L15-L20)): `create`, `set_anchor`, `set_block` / `get_block`, `raycast`, `register_block`.

### 4.10 LOD seam

`IChunkMesher::Mesh(section, lod)` exists from day one — `lod 0` = full, `lod N` samples stride `2ᴺ` and merges. A `lodForDistance()` policy stub returns 0 in Phase 1. A `ChunkColumn` may hold meshes at multiple LODs later. Real LOD enables with no API change.

---

## 5. Testing strategy

- **Integration smoke** — a PhasmaPlayer (or editor play-mode) Lua scene: create a voxel world with simple terrain → walk (collision) → break/place (raycast) → move the anchor (streaming). Verify: chunk load/unload correctness, no main-thread upload hitches, edit→remesh, **Vulkan + DX12 parity**, plus a screenshot reference.
- **Unit-level** — greedy mesher on a small known volume → expected quad count; DDA raycast on a known volume → expected hit cell/face.

---

## 6. Perf budgets (soft — no hard Phase-1 target)

- Meshing + generation run entirely off the main thread (`ThreadPool::General`).
- `VoxelWorld::Update`'s upload phase is capped at a small number of sections/frame so a burst cannot hitch.
- Internal targets only; revisit when a render-distance bar is set.

---

## 7. Risks & early spikes

Two spikes run first, before the subsystem proper, because they are the load-bearing technical bets:

1. **Incremental geometry arena + incremental indirect-draw management** (the render-integration spine; see §4.6). Spike: sub-allocate two ranges in the **shared** `m_buffer` (with reserved headroom), `CopyBufferStaged` two small meshes in place, and add their entries incrementally — which requires reserving **capacity** across `m_indirectAll` (exact-sized today), mesh-constants, visibility, and occlusion-SS-A/B, **plus updating the DX12 mesh-constants DEFAULT mirror** and seeding visibility=1. Confirm both render, that GPU frustum/Hi-Z culling still works, then free one range and confirm it disappears with no `UploadBuffers` rebuild and no stall. Validate on **Vulkan + DX12**. *Highest-risk item — if this incremental capacity management proves infeasible or too invasive, fall back to "Voxel-owned buffers + a dedicated `VoxelPass`" (own frustum cull, no free Hi-Z occlusion), the runner-up option.*
2. **`Texture2DArray` + custom `PassInfoAsset` GBuffer shader binding** across Vulkan & DX12. `Image::Create({.arrayLayers=N, .imageType=PE_IMAGE_TYPE_2D})` + `CreateSRV(PE_IMAGE_VIEW_TYPE_2D_ARRAY)` + a custom GBuffer shader sampling `Texture2DArray` by `joints[0]` layer with `frac`-tiled uv. **Fallback:** a single 2D atlas with tile padding + restricted greedy merge (no merging across the tiling axis).

Lower risks:

3. **Border meshing dependency** (the neighbor-generated gate) — ordering/edge cases at the load front.
4. **Upload-budget tuning** to avoid hitches at higher render distances.

---

## 8. Open questions (non-blocking)

- World height final default (256 vs 384) — currently 256, configurable.
- Anchor source — API-set in Phase 1; auto-follow the active camera could be a convenience later.
- Whether the voxel world is ever editor-authored/persisted, or purely runtime-instantiated (Phase 1 assumes runtime-instantiated via API/Lua; persistence is Phase 6).
