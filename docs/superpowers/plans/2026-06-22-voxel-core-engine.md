# Voxel Core Engine (Phase 1) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a walkable, editable, infinite cube-voxel world in PhasmaEngine as a permanent C++ subsystem, rendering through the existing GBuffer/indirect/cull path via an incremental geometry arena.

**Architecture:** New `Phasma/Runtime/Code/Voxel/` subsystem. Chunk gen + greedy meshing run on `ThreadPool::General`; results upload incrementally into a pre-reserved Scene geometry arena (no full rebuild). Chunks are first-class `Mesh`es, so they inherit GPU frustum + Hi-Z culling and shadows. Block textures live in a `Texture2DArray` sampled by a custom `PassInfoAsset` GBuffer shader. Collision and break/place run CPU-side against the voxel data (no Jolt).

**Tech Stack:** C++17, PhasmaCore/Runtime, RHI (Vulkan + DX12), HLSL shaders, sol2 Lua, `ThreadPool`, CMake/Ninja.

**Spec:** `docs/superpowers/specs/2026-06-22-voxel-core-engine-design.md`

## Global Constraints

- **Cross-backend:** every GPU-touching feature must work on **Vulkan AND DX12**. Smoke each on both.
- **Leave changes UNSTAGED by default (repo rule, OVERRIDES the skill's commit steps):** `AGENTS.md` says work on `master` uncommitted, leaving unstaged diffs for the user to review. So each task's final step is **`clang-format` only — do not `git add` and do not commit**. Stage or commit only when the user explicitly asks. Never add `Co-Authored-By` lines.
- **`clang-format -i`** every modified `.cpp`/`.h` before finishing a task.
- **No test-only code in the engine:** unit-test executables live under a Tests tree, never `#ifdef`-toggled hooks inside engine code. `third_party/` is off-limits.
- **PCH:** `Phasma/Core/pch/PhasmaPch.h` already covers std headers — do not add std includes to `.cpp`.
- **Block id type:** `uint16_t` everywhere; `0 == air`.
- **Section dims:** `kSectionDim = 16` (16³ blocks). **Chunk column:** 16×16 horizontal. **World height:** `kWorldHeight = 256` (16 sections), configurable constant.
- **Test pattern:** the repo has no gtest/CTest — unit tests are standalone `int main()` executables returning `0` on pass, `>0` on fail (pattern: `Phasma/WebGPU/Tests/DxilCacheHashTest.cpp`). Register each as a CMake `add_executable` target alongside existing test targets (find them: `grep -rn "DxilCacheHashTest" --include=CMakeLists.txt Phasma`).
- **Build (Windows, MSVC):** wrap in vcvars64 then `cmake --build build-ninja-full --config Release --target <T>` (pattern: `.build_player.bat`). Release for behavior; Debug only for crash investigation.
- **Editor module DLL lock (Bitdefender):** smoke via **PhasmaPlayer**, not the editor module, to avoid `PhasmaEditorModule*.dll` link locks.

## File Structure

```
Phasma/Runtime/Code/Voxel/
  VoxelTypes.h            // constants, coord types, world<->chunk<->section coord math (header-only inline)
  BlockStore.h/.cpp       // per-section uint16 storage behind a seam (palette later)
  ChunkSection.h/.cpp     // 16^3 section: block get/set, dirty flag, mesh handle
  ChunkColumn.h/.cpp       // stack of sections to kWorldHeight; column-local get/set
  BlockType.h             // POD block definition
  BlockRegistry.h/.cpp    // register/lookup block types
  IChunkMesher.h          // mesher interface (Mesh(section, neighbors, lod) -> MeshData)
  GreedyMesher.h/.cpp     // greedy cube mesher; packs tile index into joints[0]
  ITerrainGenerator.h     // generator interface
  FlatGen.h/.cpp           // flat ground generator
  GeometryArena.h/.cpp    // incremental suballocator + Scene indirect/constants integration
  VoxelMaterial.h/.cpp    // builds Texture2DArray + custom-shader Material
  VoxelCollider.h/.cpp    // DDA raycast + swept-AABB collision
  VoxelWorld.h/.cpp        // chunk map, streaming pipeline, edits, upload orchestration
  VoxelSystem.h/.cpp       // ISystem: ticks VoxelWorld

Phasma/Runtime/Assets/Shaders/Voxel/
  VoxelGBufferVS.hlsl
  VoxelGBufferPS.hlsl
  voxel_gbuffer.passinfo    // PassVariant JSON referencing the two shaders

Phasma/Runtime/Code/Script/Bindings/Voxel/
  VoxelBindings.cpp        // sol2 `voxel` table

Phasma/Runtime/Tests/Voxel/   (standalone test exes)
  VoxelCoordTest.cpp
  BlockStoreTest.cpp
  ChunkColumnTest.cpp
  BlockRegistryTest.cpp
  GreedyMesherTest.cpp
  FlatGenTest.cpp
  ArenaAllocatorTest.cpp
  RaycastDdaTest.cpp
  SweptAabbTest.cpp

C:\Users\Christos\repos\PhasmaProjects   (separate repo / working dir — NOT under PhasmaEngine):
  Sample/Assets/Scripts/voxel_smoke.lua
```

---

## Spike 0A: Incremental geometry arena (HIGHEST RISK — go/no-go)

**Goal:** Prove two meshes can be added to a pre-reserved Scene geometry buffer *incrementally* (suballocate ranges, in-place copy, incremental indirect+mesh-constant entries) — rendered through GBuffer, GPU-culled — then one freed, all with **no `UploadBuffers` rebuild and no `Submit`+`Wait` stall**, on Vulkan + DX12.

**Files:**
- Read: `Phasma/Runtime/Code/Scene/SceneBuffers.cpp` (`UploadBuffers` L21, `CreateGeometryBuffer` L47, `CopyVertices` L118, `RebuildRasterInstances` ~L1173, `CreateIndirectBuffers`, `CreateMeshConstants`), `Phasma/Runtime/Code/Scene/SceneNode.cpp:858-883` (in-place copy pattern), `Phasma/Runtime/Code/Scene/MeshConstants.h`.
- Scratch: a temporary `VoxelArenaSpike` driven from a throwaway Lua binding or a hardcoded path in PhasmaPlayer startup (deleted after the spike — do not commit).

- [x] **Step 1: Map the indirect-draw + mesh-constants data flow.** Read `CreateIndirectBuffers` and `CreateMeshConstants` in `SceneBuffers.cpp` and `MeshConstants.h`. Write down (in the spike notes section at the bottom of this file): the exact arrays that must grow when one mesh is added (indirect draw command array, mesh-constants array, per-instance/AABB arrays the culling pass reads), their element structs, and which GPU buffers hold them.

- [x] **Step 2: Reserve capacity in the SHARED Scene buffer (not a separate buffer).** **Critical (review finding 1):** [GbufferPass.cpp:180-181](../../../Phasma/Runtime/Code/RenderPasses/GbufferPass.cpp#L180-L181) binds **only** `m_scene->GetBuffer()` for vertex/index, and all indirect draws read Scene's culling/occlusion buffers sized by `GetMeshCount()`. A *separate* voxel buffer would **not** be drawn by this pass, so the "inherit existing cull/draw for free" benefit only exists if voxel geometry lives in the shared buffer as real Scene meshes. So: pre-grow `m_buffer` (and the index region) with reserved voxel headroom at world-create, and reserve **capacity** (not exact-count) across the dependent buffers (see Step 3). Decide the headroom from a max voxel mesh budget (≈ `loadRadius²×kSectionCount`). The fallback path (separate buffer) is only valid under the runner-up design in Step 6.

- [x] **Step 3: Incrementally add two quads — and reserve every dependent buffer (review finding 2).** Suballocate two vertex + two index ranges in `m_buffer`. `cmd->CopyBufferStaged(m_buffer, quadVerts, bytes, vtxOffset)` and same for indices (mirror `SceneNode.cpp:871-872`). Then incrementally register two draws. **Confirmed gaps to handle:** `m_indirectAll` is created **exact-sized** to current count ([SceneBuffers.cpp:305-306](../../../Phasma/Runtime/Code/Scene/SceneBuffers.cpp#L305-L306)) while visibility / occlusion-SS-A/B / depth buffers are sized to `m_indirectCapacity` (pow2) ([SceneBuffers.cpp:332](../../../Phasma/Runtime/Code/Scene/SceneBuffers.cpp#L332), [L406-L411](../../../Phasma/Runtime/Code/Scene/SceneBuffers.cpp#L406-L411)). Incremental add must therefore: (a) ensure `m_indirectAll` has reserved capacity (re-create it pow2-padded like the others, or pre-reserve at world-create); (b) append the `DrawIndexedIndirectCommand` + bump the live mesh count used by the draw (`GetMeshCount()`); (c) append a mesh-constants entry **and update the DX12 mesh-constants DEFAULT mirror** (the cached DEFAULT copy added for DX12 GPU-read parity — see commit `9f1e8cfb`); (d) seed the new `m_visibility` slot to `1` (`FillBuffer`, mirror [SceneBuffers.cpp:411](../../../Phasma/Runtime/Code/Scene/SceneBuffers.cpp#L411)); (e) keep AABB/instance data the culling pass reads in sync. Place the quads at distinct world positions.

- [x] **Step 4: Render + verify culling — including FIRST-frame correctness (review insist-item).** Run PhasmaPlayer with an empty scene + the spike. Confirm both quads draw through GBuffer. Move the camera so one quad is off-screen / behind another and confirm the existing GPU frustum + Hi-Z culling drops it (check the culling counters or just that it's culled). **Critically (DX12 silent-failure mode):** append the second quad on frame N and verify it is drawn AND culled correctly on frame **N itself**, not N+1 — i.e. the appended indirect/mesh-constants entry + the bumped `GetMeshCount()` are GPU-visible to the *same* frame's cull dispatch. This is exactly what the DX12 mesh-constants DEFAULT mirror exists to guarantee; a one-frame-stale read passes a naïve "no hitch" check but is a real bug. Test on **both Vulkan and DX12** — DX12 (DEFAULT-heap mirror) is the one that desyncs if the mirror isn't updated before the cull dispatch reads it. Screenshot.

Run:
```bash
cmake --build build-ninja-full --config Release --target PhasmaPlayer
# Vulkan:
build-ninja-full/Release/PhasmaPlayer.exe --api vulkan
# DX12:
build-ninja-full/Release/PhasmaPlayer.exe --api dx12
```
Expected: two quads visible; one culls when occluded/off-screen; no per-frame hitch; log shows no `UploadBuffers`/`combined_Geometry_buffer` recreation after startup.

- [x] **Step 5: Free one range.** Free quad B's ranges + remove its indirect/constants entries (swap-remove the dense indirect array, decrement count). Confirm B disappears, A remains, no stall, no rebuild.

- [x] **Step 6: Go/no-go decision.** If incremental indirect management (Step 3 a–e) works on both backends → proceed; productize in Task 7. **If it proves infeasible or too invasive** (indirect/constants/visibility capacity + DX12 mirror too entangled with the full-rebuild path) → STOP and switch the render-integration design to the spec's runner-up: **"Voxel-owned buffers + a dedicated `VoxelPass`"** — a new pass in `kSceneRenderGraphPasses` ([SceneRenderGraph.cpp:44](../../../Phasma/Runtime/Code/Render/SceneRenderGraph.cpp#L44)) that binds voxel-owned vertex/index/indirect buffers and writes the same GBuffer targets. **Trade-off if we fall back:** voxel chunks do their own frustum cull and do **not** inherit the engine's Hi-Z occlusion culling for free (a later phase could add it). Record the decision + the validated API shape in the spike notes.

- [x] **Step 7: Remove all scratch spike code** (it must not be committed; leave the working tree unstaged). Capture the validated arena API shape (alloc/free signatures, exact buffers/arrays touched, DX12-mirror handling) in the spike notes for Task 7.

---

## Spike 0B: Texture2DArray + custom GBuffer shader

**Goal:** Prove a 2-layer `Texture2DArray` renders correctly through a custom `PassInfoAsset` GBuffer shader that picks the layer from `joints[0]`, on Vulkan + DX12.

**Files:**
- Read: `Phasma/Core/Code/API/Image.h:14-101` (`ImageDesc.arrayLayers`, `CreateSRV(PE_IMAGE_VIEW_TYPE_2D_ARRAY)`, `CopyDataToImageStaged` with `baseArrayLayer`/`layerCount`), `Phasma/Core/Code/API/Shader.h:15-45`, `Phasma/Runtime/Code/Scene/PassInfoAsset.h`, `Phasma/Runtime/Code/RenderPasses/AabbsPass.cpp` (custom-shader example), `Phasma/Runtime/Code/Scene/Material.h:56-150` (`passInfoAsset`, `namedTextures`, `namedTextureIndices`), the existing GBuffer VS/PS HLSL (find: `grep -rn "GBuffer" Phasma/Runtime/Assets/Shaders`).
- Create (temporary): `Phasma/Runtime/Assets/Shaders/Voxel/VoxelGBufferVS.hlsl`, `VoxelGBufferPS.hlsl`, `voxel_gbuffer.passinfo`.

- [x] **Step 1: Build a 2-layer array image.** `Image::Create({.width=16,.height=16,.arrayLayers=2,.format=PE_FORMAT_R8G8B8A8_UNORM,.usage=...SAMPLED|TRANSFER_DST,.imageType=PE_IMAGE_TYPE_2D, .name="voxel_atlas_array"})`. Upload layer 0 = solid red, layer 1 = solid green via `CopyDataToImageStaged(cmd, data, size, baseArrayLayer, 1)` per layer. `CreateSRV(PE_IMAGE_VIEW_TYPE_2D_ARRAY)`. **[DONE — 2-layer red/green array + 2DArray view exercised cross-backend through the same RHI via a throwaway WebGPU sample; see 0B RESULT.]**

- [x] **Step 2: Author the custom GBuffer shaders.** Start from the engine's existing GBuffer VS/PS. In the PS, declare `Texture2DArray<float4> gVoxelAtlas` and sample `gVoxelAtlas.Sample(samp, float3(frac(input.uv), input.tileLayer))`. In the VS, read `joints[0]` as `uint tileLayer` and pass it through (`nointerpolation`). Write the same GBuffer outputs (albedo/normal/material) as the stock shader so it composites correctly. (Full shader bodies are produced in Task 8; here a minimal correct version suffices.)

- [x] **Step 3: Wire a PassInfoAsset material — and prove the reflection path accepts `Texture2DArray` (review finding 3).** **[DONE — reflector is dimension-agnostic (no view-type field in `ImageReflection`); array SRV resolves with no validation error on Vulkan+DX12. NOTE: opaque GBuffer pass ignores `passInfoAsset` today — wiring the custom pipeline is Task 8. See 0B RESULT.]** Create `voxel_gbuffer.passinfo` referencing the two shaders (mirror an existing `.passinfo`; find one: `grep -rln "fragmentShader" Phasma/Runtime/Assets`). Create a `Material`, set `material.passInfoAsset` to it, put the array image in `namedTextures["gVoxelAtlas"]`, let `Scene::UpdateImageViews()` assign the bindless index ([SceneBuffers.cpp:485](../../../Phasma/Runtime/Code/Scene/SceneBuffers.cpp#L485) handles named textures). **Before rendering, confirm the SPIR-V/DXIL reflection + descriptor layout actually resolves a `Texture2DArray` binding** (the named-texture path is proven for ordinary `Texture2D`; verify the reflector reports the array dimension and the descriptor is created — check the reflection output / no validation error on both backends). If reflection rejects `Texture2DArray`, that is the trigger for the Step 5 fallback.

- [x] **Step 4: Render two quads, layers 0 and 1.** Reuse the Spike-0A arena (or normal AddMesh + one-time UploadBuffers since this spike isn't about streaming). Quad A: `joints[0]=0` (expect red). Quad B: `joints[0]=1` (expect green). Tile the uv 4× across each quad and confirm the texture repeats (validates `frac` tiling across a merged quad). **[DONE — two quads RED(layer0)+GREEN(layer1), uv 0..4 frac-tiled, pixel-identical Vulkan vs DX12. Engine had no per-material custom pipeline to drive, so the render was proven through the identical RHI via a WebGPU graphics pipeline rather than `Scene::AddMesh`.]**

Run (both backends, as Spike 0A Step 4).
Expected: red quad + green quad, each tiled 4×4; identical on Vulkan and DX12.

- [x] **Step 5: Go/no-go.** **[DONE — GO/PROCEED with Texture2DArray. Array sampling + bindless binding works on both backends; no fallback to 2D atlas needed. See 0B RESULT decision.]**

- [x] **Step 6: Keep the shaders/passinfo** (they become Task 8 artifacts); remove any throwaway driver code. Leave the working tree unstaged; `clang-format` n/a for HLSL but keep consistent style. **[DONE — VoxelGBufferVS/PS.hlsl + voxel_gbuffer.passinfo kept under `Phasma/Runtime/Assets/Shaders/Voxel/`; throwaway WebGPU `VoxelArray` sample + its CMake registration removed.]**

---

## Task 1: Voxel coordinate math + constants

**Files:**
- Create: `Phasma/Runtime/Code/Voxel/VoxelTypes.h`
- Test: `Phasma/Runtime/Tests/Voxel/VoxelCoordTest.cpp`

**Interfaces:**
- Produces: `pe::voxel` namespace; constants `kSectionDim=16`, `kWorldHeight=256`, `kSectionCount=kWorldHeight/kSectionDim`; types `using BlockId = uint16_t;`, `struct BlockPos{int x,y,z;}`, `struct ColumnCoord{int cx,cz;}`; free fns `ColumnCoord WorldToColumn(int wx,int wz)`, `int LocalX(int wx)`, `int LocalZ(int wz)`, `int SectionIndex(int wy)`, `int LocalY(int wy)`, `int BlockIndex(int lx,int ly,int lz)` (= `lx + kSectionDim*(lz + kSectionDim*ly)`).

- [ ] **Step 1: Write the failing test**
```cpp
// VoxelCoordTest.cpp
#include "Voxel/VoxelTypes.h"
#include <cstdio>
using namespace pe::voxel;
static int g_fail = 0;
#define CHECK(c) do{ if(!(c)){ printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#c); ++g_fail; } }while(0)
int main(){
    CHECK(WorldToColumn(0,0).cx==0 && WorldToColumn(0,0).cz==0);
    CHECK(WorldToColumn(15,15).cx==0 && WorldToColumn(15,15).cz==0);
    CHECK(WorldToColumn(16,-1).cx==1 && WorldToColumn(16,-1).cz==-1); // floor-div for negatives
    CHECK(LocalX(-1)==15 && LocalZ(-16)==0);
    CHECK(SectionIndex(0)==0 && SectionIndex(16)==1 && LocalY(17)==1);
    CHECK(BlockIndex(0,0,0)==0 && BlockIndex(15,15,15)==kSectionDim*kSectionDim*kSectionDim-1);
    if(g_fail==0) printf("OK VoxelCoordTest\n");
    return g_fail;
}
```

- [ ] **Step 2: Run to verify it fails** (target won't compile / link — header missing).

- [ ] **Step 3: Implement `VoxelTypes.h`**
```cpp
#pragma once
#include <cstdint>
namespace pe::voxel {
    inline constexpr int kSectionDim = 16;
    inline constexpr int kWorldHeight = 256;
    inline constexpr int kSectionCount = kWorldHeight / kSectionDim;
    inline constexpr int kBlocksPerSection = kSectionDim * kSectionDim * kSectionDim;
    using BlockId = uint16_t;
    inline constexpr BlockId kAir = 0;

    struct BlockPos { int x, y, z; };
    struct ColumnCoord { int cx, cz; bool operator==(const ColumnCoord& o) const { return cx==o.cx && cz==o.cz; } };

    inline int FloorDiv(int a, int b){ int q=a/b, r=a%b; if((r!=0)&&((r<0)!=(b<0))) --q; return q; }
    inline int Mod(int a, int b){ int r=a%b; if(r<0) r+=b; return r; }

    inline ColumnCoord WorldToColumn(int wx, int wz){ return { FloorDiv(wx,kSectionDim), FloorDiv(wz,kSectionDim) }; }
    inline int LocalX(int wx){ return Mod(wx,kSectionDim); }
    inline int LocalZ(int wz){ return Mod(wz,kSectionDim); }
    inline int SectionIndex(int wy){ return FloorDiv(wy,kSectionDim); }
    inline int LocalY(int wy){ return Mod(wy,kSectionDim); }
    inline int BlockIndex(int lx,int ly,int lz){ return lx + kSectionDim*(lz + kSectionDim*ly); }
}
```

- [ ] **Step 4: Run to verify it passes** → `OK VoxelCoordTest`, exit 0.

- [ ] **Step 5: Run `clang-format`** on the new `.h` and leave it unstaged; register the CMake test target (see Global Constraints). Do not `git add` or commit.

---

## Task 2: BlockStore + ChunkSection

**Files:**
- Create: `Phasma/Runtime/Code/Voxel/BlockStore.h`, `BlockStore.cpp`, `ChunkSection.h`, `ChunkSection.cpp`
- Test: `Phasma/Runtime/Tests/Voxel/BlockStoreTest.cpp`

**Interfaces:**
- Produces:
  - `class BlockStore { public: BlockId Get(int idx) const; void Set(int idx, BlockId); void Fill(BlockId); bool IsAllAir() const; private: std::array<BlockId,kBlocksPerSection> m_data; uint32_t m_nonAir=0; };`
  - `class ChunkSection { public: BlockId Get(int lx,int ly,int lz) const; void Set(int lx,int ly,int lz, BlockId); bool IsEmpty() const; bool IsDirty() const; void SetDirty(bool); BlockStore& Store(); const BlockStore& Store() const; };` (dirty defaults true on first fill).

- [ ] **Step 1: Failing test** — set/get round-trip; `IsEmpty()` true after `Fill(kAir)`, false after one solid set; `m_nonAir` count via `IsAllAir()`; dirty toggles.
```cpp
#include "Voxel/ChunkSection.h"
#include <cstdio>
using namespace pe::voxel;
static int g_fail=0;
#define CHECK(c) do{ if(!(c)){ printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#c); ++g_fail; } }while(0)
int main(){
    ChunkSection s;
    CHECK(s.IsEmpty());
    s.Set(1,2,3, 7);
    CHECK(s.Get(1,2,3)==7);
    CHECK(!s.IsEmpty());
    s.Set(1,2,3, kAir);
    CHECK(s.IsEmpty());
    if(g_fail==0) printf("OK BlockStoreTest\n");
    return g_fail;
}
```

- [ ] **Step 2: Run to verify it fails.**

- [ ] **Step 3: Implement.** `BlockStore::Set` maintains `m_nonAir` (`if old==air && new!=air ++; if old!=air && new==air --`). `IsAllAir()` = `m_nonAir==0`. `ChunkSection` wraps a `BlockStore` + `bool m_dirty=true`, maps `(lx,ly,lz)` via `BlockIndex`, sets `m_dirty=true` on any `Set`, `IsEmpty()` = `m_store.IsAllAir()`.

- [ ] **Step 4: Run → `OK BlockStoreTest`, exit 0.**

- [ ] **Step 5: Run `clang-format` (leave changes unstaged); register the CMake test target.**

---

## Task 3: ChunkColumn

**Files:**
- Create: `Phasma/Runtime/Code/Voxel/ChunkColumn.h`, `ChunkColumn.cpp`
- Test: `Phasma/Runtime/Tests/Voxel/ChunkColumnTest.cpp`

**Interfaces:**
- Consumes: `ChunkSection`, `VoxelTypes.h`.
- Produces: `class ChunkColumn { public: explicit ChunkColumn(ColumnCoord); ColumnCoord Coord() const; BlockId GetLocal(int lx,int wy,int lz) const; void SetLocal(int lx,int wy,int lz, BlockId); ChunkSection& Section(int si); const ChunkSection& Section(int si) const; bool SectionInBounds(int wy) const; private: ColumnCoord m_coord; std::array<ChunkSection,kSectionCount> m_sections; };` `GetLocal/SetLocal` take **column-local** x/z (0..15) and **world** y (0..kWorldHeight-1); out-of-range y returns `kAir` / no-ops.

- [ ] **Step 1: Failing test** — set at `(3, 40, 9)`, read back; section index `40/16==2`, local y `40%16==8`; y past world height returns air, no crash.

- [ ] **Step 2: Run to verify it fails.**

- [ ] **Step 3: Implement** using `SectionIndex(wy)`/`LocalY(wy)`; guard `wy<0||wy>=kWorldHeight`.

- [ ] **Step 4: Run → OK, exit 0.**

- [ ] **Step 5: Run `clang-format` (leave changes unstaged); register the CMake test target.**

---

## Task 4: BlockType + BlockRegistry

**Files:**
- Create: `Phasma/Runtime/Code/Voxel/BlockType.h`, `BlockRegistry.h`, `BlockRegistry.cpp`
- Test: `Phasma/Runtime/Tests/Voxel/BlockRegistryTest.cpp`

**Interfaces:**
- Produces:
  - `enum class VoxelRenderClass : uint8_t { Air, Opaque /* Cutout, Transparent later */ };`
  - `struct BlockType { BlockId id; std::string name; bool solid; bool opaque; VoxelRenderClass renderClass; uint16_t faceTiles[6]; };` (face order: `+X,-X,+Y,-Y,+Z,-Z`).
  - `class BlockRegistry { public: BlockId Register(const BlockType&); const BlockType& Get(BlockId) const; bool IsSolid(BlockId) const; bool IsOpaque(BlockId) const; uint16_t FaceTile(BlockId, int face) const; size_t Count() const; };` Air (id 0) is auto-registered in the ctor: `{0,"air",false,false,Air,{0}}`.

- [ ] **Step 1: Failing test** — registry starts with air (`IsSolid(0)==false`, `IsOpaque(0)==false`); register stone `{1,"stone",true,true,Opaque,{3,3,3,3,3,3}}`; `IsSolid(1)==true`, `FaceTile(1,0)==3`.

- [ ] **Step 2: Run to verify it fails.**

- [ ] **Step 3: Implement** — `std::vector<BlockType> m_types` indexed by id; `Register` pushes (assert `id==m_types.size()` for dense ids) and returns id; accessors read `m_types`.

- [ ] **Step 4: Run → OK, exit 0.**

- [ ] **Step 5: Run `clang-format` (leave changes unstaged); register the CMake test target.**

---

## Task 5: GreedyMesher (IChunkMesher)

**Files:**
- Create: `Phasma/Runtime/Code/Voxel/IChunkMesher.h`, `GreedyMesher.h`, `GreedyMesher.cpp`
- Test: `Phasma/Runtime/Tests/Voxel/GreedyMesherTest.cpp`

**Interfaces:**
- Consumes: `BlockRegistry`, engine `Vertex` (`Phasma/Core/Code/API/Vertex.h`).
- Produces:
  - `struct MeshData { std::vector<Vertex> vertices; std::vector<uint32_t> indices; };`
  - `using BlockSampler = std::function<BlockId(int x,int y,int z)>;` // section-local coords 0..15, **may be queried at -1..16** to reach neighbors; returns neighbor block or air.
  - `class IChunkMesher { public: virtual ~IChunkMesher()=default; virtual MeshData Mesh(const BlockSampler& sample, const BlockRegistry& reg, int lod)=0; };`
  - `class GreedyMesher : public IChunkMesher { public: MeshData Mesh(const BlockSampler&, const BlockRegistry&, int lod) override; };`
- Vertex packing (per spec §4.4/4.5): `position` = block-space corner (caller offsets by section origin); `normals` = face normal; `tangent` = face tangent + handedness `w`; `color` = `{1,1,1,1}`; `joints[0]` = face tile index (`reinterpret` the uint into the `uint32_t joints[0]` slot); `weights`={0}; `uv` spans the merged-quad extent (0..w, 0..h) so the shader's `frac` tiles per block. **Note:** `joints` is `uint32_t[4]` ([Vertex.h](../../../Phasma/Core/Code/API/Vertex.h)), so a `uint16_t` tile index fits with room to spare; if tile indices are ever widened, re-check the shader's `nointerpolation uint tileLayer` read. Document this in `VoxelTypes.h`.

- [ ] **Step 1: Write the failing test** (use `lod=0`)
```cpp
#include "Voxel/GreedyMesher.h"
#include "Voxel/BlockRegistry.h"
#include <cstdio>
using namespace pe::voxel;
static int g_fail=0;
#define CHECK(c) do{ if(!(c)){ printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#c); ++g_fail; } }while(0)
int main(){
    BlockRegistry reg;
    reg.Register({1,"stone",true,true,VoxelRenderClass::Opaque,{0,0,0,0,0,0}});
    GreedyMesher m;
    // Empty section -> no geometry
    auto empty = m.Mesh([](int,int,int){return (BlockId)0;}, reg, 0);
    CHECK(empty.vertices.empty() && empty.indices.empty());
    // Single solid block at (0,0,0), all neighbors air -> 6 faces = 6 quads = 24 verts, 36 indices
    auto one = m.Mesh([](int x,int y,int z){ return (x==0&&y==0&&z==0)?(BlockId)1:(BlockId)0; }, reg, 0);
    CHECK(one.vertices.size()==24);
    CHECK(one.indices.size()==36);
    // Face coverage + winding sanity (count alone passes a wrong-winding cube): all 6 axis normals must appear,
    // and the packed tile index in joints[0] must be 0 (stone's faceTiles).
    bool seen[6]={false,false,false,false,false,false};
    for(const auto& v : one.vertices){
        if(v.normals[0]> 0.5f) seen[0]=true; if(v.normals[0]<-0.5f) seen[1]=true;
        if(v.normals[1]> 0.5f) seen[2]=true; if(v.normals[1]<-0.5f) seen[3]=true;
        if(v.normals[2]> 0.5f) seen[4]=true; if(v.normals[2]<-0.5f) seen[5]=true;
        uint32_t tile; memcpy(&tile,&v.joints[0],sizeof(uint32_t)); CHECK(tile==0);
    }
    for(int f=0; f<6; ++f) CHECK(seen[f]);
    // (Optional stronger check: assert the +Y quad's two triangles are CCW when viewed from +Y.)
    // Full 16^3 solid, all border neighbors air -> only the 6 outer faces, each merged to ONE 16x16 quad = 6 quads
    auto solid = m.Mesh([](int x,int y,int z){ return (x>=0&&x<16&&y>=0&&y<16&&z>=0&&z<16)?(BlockId)1:(BlockId)0; }, reg, 0);
    CHECK(solid.vertices.size()==24);   // 6 merged quads * 4
    CHECK(solid.indices.size()==36);
    if(g_fail==0) printf("OK GreedyMesherTest\n");
    return g_fail;
}
```

- [ ] **Step 2: Run to verify it fails.**

- [ ] **Step 3: Implement the greedy mesher** (standard sweep-and-merge over 3 axes × 2 directions). Reference algorithm:
```cpp
// GreedyMesher.cpp (core). For each of 3 axes d (0=x,1=y,2=z) and both directions:
//  - u,v are the other two axes; sweep slices along d.
//  - For each slice, build a mask[u][v] = tile index if the face between cell(slice-1) and cell(slice)
//    is visible (one side solid+opaque, the other not opaque) and belongs to the +/- direction; else -1.
//  - Greedily merge equal-mask rectangles into quads; emit 4 verts + 6 indices per quad.
// Face visibility: a +d face of a solid block is emitted iff neighbor in +d is NOT opaque.
// Tile index: reg.FaceTile(blockId, faceIndex(d, dir)).
// uv: (w,h) of the merged rect so frac() tiles per-block in the shader.
// Pack tile index into joints[0]: memcpy(&v.joints[0], &tileU32, sizeof(uint32_t)) (v.joints is uint32_t[4]).
```
Implement fully (the test pins vertex/index counts so correctness is checkable). Keep `lod` param: for `lod>0`, sample stride `1<<lod` and scale positions — but Phase 1 ships lod 0; still accept and honor stride so the seam is real (add a later test if desired).

- [ ] **Step 4: Run → `OK GreedyMesherTest`, exit 0.**

- [ ] **Step 5: Run `clang-format` (leave changes unstaged); register the CMake test target.**

---

## Task 6: ITerrainGenerator + FlatGen

**Files:**
- Create: `Phasma/Runtime/Code/Voxel/ITerrainGenerator.h`, `FlatGen.h`, `FlatGen.cpp`
- Test: `Phasma/Runtime/Tests/Voxel/FlatGenTest.cpp`

**Interfaces:**
- Consumes: `ChunkColumn`, `BlockRegistry`.
- Produces:
  - `class ITerrainGenerator { public: virtual ~ITerrainGenerator()=default; virtual void Generate(ChunkColumn& col)=0; };`
  - `class FlatGen : public ITerrainGenerator { public: FlatGen(int groundY, BlockId fill); void Generate(ChunkColumn&) override; };` Fills `wy in [0, groundY)` with `fill`, rest air, across all 16×16 columns-local cells.

- [ ] **Step 1: Failing test** — `FlatGen(8, 1)` on a column → `GetLocal(5,7,5)==1`, `GetLocal(5,8,5)==kAir`, `GetLocal(0,0,0)==1`.

- [ ] **Step 2: Run to verify it fails.**

- [ ] **Step 3: Implement** triple loop `lx,lz in 0..15`, `wy in 0..groundY-1` → `SetLocal(lx,wy,lz,fill)`.

- [ ] **Step 4: Run → OK, exit 0.**

- [ ] **Step 5: Run `clang-format` (leave changes unstaged); register the CMake test target.**

---

## Task 7: GeometryArena (productize Spike 0A)

**Files:**
- Create: `Phasma/Runtime/Code/Voxel/GeometryArena.h`, `GeometryArena.cpp`
- Modify (additive, gated): `Scene` gets capacity-aware incremental add/remove. Per finding 1, geometry must live in the **shared** `m_buffer`, so add (1) a world-create reservation that pre-grows `m_buffer` (vtx+idx headroom) and pads `m_indirectAll` + mesh-constants + visibility + occlusion-SS-A/B to a voxel mesh-count capacity; (2) `int Scene::AddArenaMesh(...)` / `void Scene::RemoveArenaMesh(int)` that append/swap-remove one indirect + mesh-constants entry, **update the DX12 mesh-constants DEFAULT mirror**, seed visibility=1, and adjust the live `GetMeshCount()` — **without** `UploadBuffers`. If Spike 0A fell back to the runner-up, instead create a `VoxelPass` + voxel-owned buffers and skip the Scene changes.
- Test: `Phasma/Runtime/Tests/Voxel/ArenaAllocatorTest.cpp` (CPU free-list only — GPU path verified by smoke).

**Interfaces:**
- Consumes: validated arena shape from Spike 0A; `Buffer`, `CommandBuffer`, `Scene`.
- Produces:
  - `struct ArenaHandle { uint32_t vtxByteOffset, vtxBytes, idxByteOffset, idxBytes; int sceneMeshIndex; bool valid; };`
  - `class FreeListAllocator { public: explicit FreeListAllocator(uint32_t capacityBytes); uint32_t Alloc(uint32_t bytes); /*0xFFFFFFFF on OOM*/ void Free(uint32_t offset, uint32_t bytes); uint32_t Used() const; };` (coalescing free list; **first-fit** for Phase 1). *Note: chunk streaming produces spatially-bursty alloc/free patterns that fragment first-fit as render distance grows; headroom is generous for Phase 1, but a best-fit or address-ordered policy may be needed later — leave the policy swappable behind this interface.*
  - `class GeometryArena { public: void Init(Scene* scene, uint32_t vtxCapBytes, uint32_t idxCapBytes); ArenaHandle Upload(CommandBuffer* cmd, const MeshData&, const vec3& sectionOrigin, Material* mat); void Release(const ArenaHandle&); void Destroy(); };`

- [ ] **Step 1: Failing test for `FreeListAllocator`** — alloc 100→0, alloc 50→100, free(0,100), alloc 80→0 (reuses), alloc beyond capacity → 0xFFFFFFFF; after free+free adjacent, a coalesced alloc spanning both succeeds.

- [ ] **Step 2: Run to verify it fails.**

- [ ] **Step 3: Implement `FreeListAllocator`** (sorted free spans, coalesce on free, first-fit alloc). Run → OK.

- [ ] **Step 4: Implement `GeometryArena`** using the Spike-0A-validated path: two `FreeListAllocator`s (vtx/idx) over the **reserved headroom region of the shared `m_buffer`** (not a separate buffer — per finding 1 only `Scene::GetBuffer()` is drawn); `Upload` allocs ranges, `CopyBufferStaged` the MeshData into `m_buffer` at the reserved offsets, calls `Scene::AddArenaMesh` (which registers the incremental indirect + mesh-constants entry with section AABB so culling works, updates the DX12 mirror, seeds visibility), returns handle; `Release` frees ranges + `Scene::RemoveArenaMesh`. **No `UploadBuffers` call.** (Under the Step-6 fallback, the arena owns its own buffers + the `VoxelPass` instead.)

- [ ] **Step 5: GPU verification via smoke** — temporary: from `voxel_smoke.lua` (stub) or a hardcoded call, upload one section's `MeshData` through the arena and confirm it renders + culls, both backends. (Folded into Task 9's smoke if preferred.)

- [ ] **Step 6: Run `clang-format`** on the `.cpp/.h` (leave changes unstaged); register the CMake test target for the allocator test.

---

## Task 8: VoxelMaterial + GBuffer shaders (productize Spike 0B)

**Files:**
- Create: `Phasma/Runtime/Code/Voxel/VoxelMaterial.h`, `VoxelMaterial.cpp`
- Finalize: `Phasma/Runtime/Assets/Shaders/Voxel/VoxelGBufferVS.hlsl`, `VoxelGBufferPS.hlsl`, `voxel_gbuffer.passinfo`

**Interfaces:**
- Consumes: Spike 0B result; `Image`, `Material`, `Scene`, `BlockRegistry`.
- Produces: `class VoxelMaterial { public: void Build(Scene* scene, const std::vector<std::string>& tilePngPaths); Material* Get() const; ResourceHandle<Image> Atlas() const; };` Builds a `Texture2DArray` (one layer per tile png), creates the custom-shader `Material` (`passInfoAsset` = `voxel_gbuffer.passinfo`, `namedTextures["gVoxelAtlas"]` = atlas), triggers `Scene::UpdateImageViews()`.

- [ ] **Step 1: Finalize the GBuffer shaders.** VS: read `joints[0]`→`uint tileLayer` (nointerpolation out), transform position with the standard camera/model constants (copy from stock GBuffer VS), pass uv/normal/tangent. PS: `Texture2DArray<float4> gVoxelAtlas; ... float4 albedo = gVoxelAtlas.Sample(samp, float3(frac(input.uv), input.tileLayer));` then write the same GBuffer MRT outputs as the stock PS (albedo, packed normal, material params; vertex color multiplies albedo — white now, light/AO in Phase 3).

- [ ] **Step 2: Implement `VoxelMaterial::Build`** (array image creation + per-layer staged upload from the tile pngs + Material wiring). Use a small set of placeholder original tiles under `Phasma/Runtime/RuntimeAssets/Textures/Voxel/` (grass/dirt/stone — original art, not Minecraft assets).

- [ ] **Step 3: Verify via smoke** — a section drawn with the voxel material shows the correct per-face tiles, tiled across merged quads, both backends. (Folded into Task 9 smoke.)

- [ ] **Step 4: Run `clang-format`** on the `.cpp/.h`; leave all new shader/asset files unstaged for review.

---

## Task 9: VoxelWorld + VoxelSystem + streaming

**Files:**
- Create: `Phasma/Runtime/Code/Voxel/VoxelWorld.h`, `VoxelWorld.cpp`, `VoxelSystem.h`, `VoxelSystem.cpp`
- Modify: the app/runtime system bootstrap to `CreateGlobalSystem<VoxelSystem>()` (find the existing `CreateGlobalSystem<PhysicsSystem>` call site: `grep -rn "CreateGlobalSystem<" Phasma`). `VoxelSystem` is created but **idle until a world is created via API**.

**Interfaces:**
- Consumes: `ChunkColumn`, `GreedyMesher`, `FlatGen`, `GeometryArena`, `VoxelMaterial`, `BlockRegistry`, `ThreadPool::General`, `Scene`, `ISystem`.
- Produces:
  - `struct VoxelConfig { int loadRadius=8; int unloadMargin=2; int uploadBudgetPerFrame=4; int groundY=64; };`
  - `class VoxelWorld { public: void Create(Scene* scene, const VoxelConfig&); void Destroy(); void SetAnchor(const vec3& worldPos); BlockId GetBlock(int x,int y,int z) const; void SetBlock(int x,int y,int z, BlockId); bool Raycast(const vec3& o,const vec3& d,float maxDist, /*out*/ BlockPos& hit, /*out*/ BlockPos& adjacent, /*out*/ vec3& normal) const; void Update(); BlockRegistry& Registry(); };`
  - `class VoxelSystem : public ISystem { public: void Init(CommandBuffer*) override; void Update() override; void Destroy() override; VoxelWorld* World(); };`

- [ ] **Step 1: Implement the chunk map + streaming state machine.** `std::unordered_map<ColumnCoord, ColumnState>`; `ColumnState{ std::unique_ptr<ChunkColumn> col; enum{Empty,Generating,Generated,Meshing,Ready,Unloading} state; std::array<std::shared_future<MeshData>,kSectionCount> meshFutures; std::array<ArenaHandle,kSectionCount> handles; SceneNode* node; }`. Hash `ColumnCoord` (combine cx,cz).

- [ ] **Step 2: Implement `SetAnchor` + load/unload.** On anchor change, compute desired column set within `loadRadius`; enqueue `Generate` (`ThreadPool::General.Enqueue`) for new columns; mark columns beyond `loadRadius+unloadMargin` for unload (free arena handles + remove node).

- [ ] **Step 3: Implement the gen→mesh gate.** When a column finishes Generate, and all its 4 horizontal neighbor columns are at least Generated, enqueue per-section `Mesh` jobs (the `BlockSampler` reads the column + neighbor columns; the section's vertical neighbors are other sections *within the same column*, so they're always present once the column is generated). Each job returns `MeshData`. *Note: this whole-column gate is correct because `FlatGen` fills a column atomically; a future per-section async generator (phase 4) would need a finer per-section "generated" gate including vertical-seam sections.*

- [ ] **Step 4: Implement `VoxelWorld::Update`** (main thread, called by `VoxelSystem::Update`): poll ready mesh futures; **upload at most `uploadBudgetPerFrame` sections/frame** via `GeometryArena::Upload` (acquire one command buffer, batch the frame's uploads, submit without blocking the next frame); attach meshes to the column node; advance state to Ready; process queued edits (Task 12).

- [ ] **Step 5: Wire `VoxelSystem`** — `Init` does **not** build `VoxelMaterial`/`GeometryArena` eagerly (review insist-item: there is no `Scene` and no world at `Init` time — building them eagerly creates a "which scene?" ambiguity). Instead `VoxelWorld::Create(Scene*, cfg)` — which receives the `Scene` — constructs the `GeometryArena` (reserving capacity in that scene's buffers) and the `VoxelMaterial`. `VoxelSystem::Init` only sets up state that needs none of those (e.g. the block registry defaults); `VoxelSystem::Update` calls `World()->Update()` only when a world exists; `Destroy` tears the world down. Register via `CreateGlobalSystem<VoxelSystem>()` at the bootstrap site (idle until `voxel.create`).

- [ ] **Step 6: Smoke (both backends)** — temporary C++ or Lua that creates a world with `FlatGen(groundY=64)`, sets anchor at origin, runs. Expect: a flat plane streams in around the camera; moving the anchor streams new columns and unloads far ones; no per-frame hitch; no `combined_Geometry_buffer` rebuild after startup. Screenshot. Verify Vulkan + DX12.

- [ ] **Step 7: Run `clang-format` (leave changes unstaged).**

---

## Task 10: VoxelCollider — DDA raycast

**Files:**
- Create: `Phasma/Runtime/Code/Voxel/VoxelCollider.h`, `VoxelCollider.cpp` (raycast portion)
- Test: `Phasma/Runtime/Tests/Voxel/RaycastDdaTest.cpp` (pure: takes a `BlockSampler`-style `std::function<bool(int,int,int)> isSolid`)

**Interfaces:**
- Produces (free function, engine-independent for testability):
  - `struct VoxelRayHit { bool hit; BlockPos cell; BlockPos adjacent; vec3 normal; float dist; };`
  - `VoxelRayHit RaycastVoxels(const vec3& origin, const vec3& dir, float maxDist, const std::function<bool(int,int,int)>& isSolid);` (Amanatides–Woo)

- [ ] **Step 1: Failing test** — solid block only at `(5,0,0)`; ray from `(0.5,0.5,0.5)` dir `(1,0,0)` maxDist 10 → `hit`, `cell=={5,0,0}`, `adjacent=={4,0,0}`, `normal=={-1,0,0}`. Ray pointing `-x` → no hit. Ray maxDist 2 → no hit.

- [ ] **Step 2: Run to verify it fails.**

- [ ] **Step 3: Implement Amanatides–Woo DDA** — init cell from `floor(origin)`, `step=sign(dir)`, `tMax`/`tDelta` per axis, march until `isSolid(cell)` or `dist>maxDist`; record the axis stepped to set `adjacent`+`normal`. Handle zero dir components (tMax=inf).

- [ ] **Step 4: Run → OK, exit 0.**

- [ ] **Step 5: Run `clang-format` (leave changes unstaged); register the CMake test target.**

---

## Task 11: VoxelCollider — swept AABB

**Files:**
- Modify: `Phasma/Runtime/Code/Voxel/VoxelCollider.h/.cpp` (add swept-AABB)
- Test: `Phasma/Runtime/Tests/Voxel/SweptAabbTest.cpp`

**Interfaces:**
- Produces: `vec3 MoveAabb(const vec3& pos, const vec3& halfExtents, const vec3& delta, const std::function<bool(int,int,int)>& isSolid);` Returns the resolved new position after sliding against solid blocks (per-axis resolve: move X, resolve; move Y, resolve; move Z, resolve).

- [ ] **Step 1: Failing test** — floor solid at all `y<=0`; AABB half `(0.4,0.9,0.4)` at `(0.5,2,0.5)`, delta `(0,-5,0)` → resolved y rests on the floor (`pos.y - half.y ≈ 1.0` i.e. on top of block y=0). Horizontal delta into a wall column resolves x to abut the wall, y/z unchanged.

- [ ] **Step 2: Run to verify it fails.**

- [ ] **Step 3: Implement per-axis swept resolve** — for each axis, advance, compute the block cell range the AABB overlaps, if any solid, clamp position to the contact face. Order Y last-or-first consistently; document choice. *Add a corner test (simultaneous X+Z, or X+Y, collision) — per-axis order determines the result and the corner case is the one that bites; Phase 1 may ship a documented choice but the test should pin it.*

- [ ] **Step 4: Run → OK, exit 0.**

- [ ] **Step 5: Run `clang-format` (leave changes unstaged); register the CMake test target.**

---

## Task 12: Edits — break/place

**Files:**
- Modify: `Phasma/Runtime/Code/Voxel/VoxelWorld.cpp` (implement `SetBlock` edit path)

**Interfaces:**
- Consumes: `VoxelWorld`, `GeometryArena`, `GreedyMesher`.
- Produces: `VoxelWorld::SetBlock` semantics: writes the block, marks the owning section (and any border-adjacent section in a neighbor column) dirty, enqueues a remesh, swaps the arena handle on the next `Update`.

- [ ] **Step 1: Implement `SetBlock`** (main thread): locate column+section, `SetLocal`, mark dirty; if the block sits on a section/column border, also mark the neighbor section dirty (its border faces change). Push dirty sections to a remesh queue.

- [ ] **Step 2: Implement remesh-on-edit in `Update`** — for each queued dirty section: enqueue a `Mesh` job (or mesh inline if small), then on completion `GeometryArena::Release(old)` + `Upload(new)` and swap the handle. Respect the upload budget.

- [ ] **Step 3: Smoke (both backends)** — Lua: raycast from camera, break the hit block (`set_block(hit, air)`), place on the adjacent cell (`set_block(adjacent, stone)`); confirm the mesh updates within a frame or two, neighbors re-cull correctly at chunk borders, no full rebuild. Screenshot before/after.

- [ ] **Step 4: Run `clang-format` (leave changes unstaged).**

---

## Task 13: Lua `voxel` bindings

**Files:**
- Create: `Phasma/Runtime/Code/Script/Bindings/Voxel/VoxelBindings.cpp`
- Reference pattern: `Phasma/Runtime/Code/Script/Bindings/Scene/SceneBindings.cpp:15-20` (static-struct + `ScriptSystem::AddBindings`).

**Interfaces:**
- Consumes: `VoxelSystem`/`VoxelWorld` via `GetGlobalSystem<VoxelSystem>()`.
- Produces: Lua `voxel` table: `voxel.create{load_radius=8, ground_y=64}`, `voxel.set_anchor(x,y,z)`, `voxel.set_block(x,y,z,id)`, `voxel.get_block(x,y,z) -> id`, `voxel.register_block{id=,name=,solid=,opaque=,tiles={..6..}}`, `voxel.raycast(ox,oy,oz, dx,dy,dz, max) -> hit table {hit,cell={},adjacent={},normal={}}`.

- [ ] **Step 1: Implement bindings** with the static-struct registration pattern; marshal vec3 in/out as numbers or the engine's Lua vec3 (note: per memory, Lua vec3 is float32 — block coords are small ints so fine). Each fn routes to `GetGlobalSystem<VoxelSystem>()->World()`.

- [ ] **Step 2: Smoke** — `voxel_smoke.lua` calls every binding; verify in PhasmaPlayer console no Lua errors and the world responds (anchor/edit/raycast).

- [ ] **Step 3: Run `clang-format` (leave changes unstaged).**

---

## Task 14: Integration smoke + docs

**Files:**
- Create: `C:\Users\Christos\repos\PhasmaProjects\Sample\Assets\Scripts\voxel_smoke.lua` (external working dir — the `PhasmaProjects` repo, not under `PhasmaEngine`). Confirm the `Sample` project layout there before authoring; route the script to wherever that project loads `Assets/Scripts`.
- Create: `docs/wiki/` page for the voxel subsystem (run `bash docs/wiki/tools/lint.sh` after); update MemPalace per CLAUDE.md.

- [ ] **Step 1: Author `voxel_smoke.lua`** — register 3 blocks (grass/dirt/stone, original tiles), `voxel.create{...}`, set anchor to the camera each frame (or on move), bind keys: ray from camera center, left = break, right = place; WASD moves a swept-AABB player via `MoveAabb`.

- [ ] **Step 2: Full smoke checklist (Vulkan + DX12)** — run PhasmaPlayer with the script:
  - Flat world streams in around the player; walking streams/unloads columns; no hitch; no geometry-buffer rebuild after startup (check log).
  - Player collides with the ground and walls (swept AABB), can't fall through.
  - Break removes a block and re-meshes; place adds one; chunk-border edits update neighbors.
  - Tiles render per-face, tiled across merged quads; Vulkan == DX12 visually.
  - GPU frustum/Hi-Z culling active on chunks (occluded chunks dropped).
  - Screenshot reference saved.

- [ ] **Step 3: Run `bash docs/wiki/tools/lint.sh`**; write the wiki page + MemPalace drawer/kg entries for the subsystem.

- [ ] **Step 4: Leave all changes unstaged; report results honestly** (what passed, what didn't). Stage/commit only on explicit user instruction.

---

## Self-Review notes

- **Spec coverage:** storage (T2,T3), streaming (T9), greedy mesher (T5), atlas/shader (T8, Spike 0B), render integration / incremental arena (Spike 0A, T7), collision (T11), break/place raycast (T10, T12), flat terrain (T6), LOD seam (T5 `lod` param), API surface (T9 C++, T13 Lua), testing (per-task + T14), perf budget (T9 uploadBudgetPerFrame). All spec §3 in-scope items map to a task.
- **Out-of-scope confirmed absent:** no block models, transparency, lighting, noise/biomes, save/load, gameplay.
- **Type consistency:** `BlockId`=uint16, `kAir`=0, `MeshData`, `BlockSampler`, `ArenaHandle`, `VoxelConfig` used consistently across tasks. `joints[0]` tile-index packing referenced identically in T5 and T8/Spike 0B.

## Spike notes (fill during Spikes 0A/0B)

- _0A indirect/constants arrays + decision (arena-in-Scene vs voxel-owned buffers):_
  Source map from `SceneBuffers.cpp` / `MeshConstants.h` / `GbufferPass.cpp`:
  `UploadBuffers` destroys buffers, creates the unified `combined_Geometry_buffer`, copies
  `m_indexStore`, `m_vertexStore`, `m_positionUvStore`, and AABB data, then rebuilds storage
  buffers, indirect buffers, image views/material table, and mesh constants. One raster draw is
  represented by a `Mesh` entry (`indexOffset`, `indexCount`, `vertexOffset`, `renderType`,
  material/materialInstance, `boundingBox`) plus a `MeshRuntime` entry (material GPU index and
  image-view indices), a node mesh-ref slot (`m_nodeComponentCache[*].meshRefs`), and
  `m_nodeRuntime[*].meshRefIndirect[slot]`.

  `CreateIndirectBuffers` assigns dense draw ids (`firstInstance`) and uploads
  `PeDrawIndexedIndirectCommand` entries to `m_indirectAll`. Per-swapchain filtered output buffers
  are then allocated for culling: `m_cullingCountersBuffers`, `m_indirectOpaqueSS`,
  `m_indirectAlphaCutSS`, `m_indirectOpaqueDS`, `m_indirectAlphaCutDS`,
  `m_indirectAlphaBlend`, `m_indirectTransmission`, `m_indirectSelected`, alpha sort-key buffers,
  Hi-Z occlusion counters (`m_occCountersA/B`), Hi-Z filtered indirect buffers
  (`m_occOpaque/AlphaCut{SS,DS}{A,B}`), and persistent `m_visibility`. `CullingCS.hlsl` indexes
  `IndirectCommandsIn[idx]` and `MeshConstants[idx]`, so an incremental add must append/update both
  `m_indirectAll` and `m_meshConstants` at the same dense draw index and seed visibility for that
  draw. Current `m_indirectAll` and `m_meshConstants` are sized to draw count, while filtered
  buffers use `m_indirectCapacity`; product code will need explicit reserved capacities before
  append can be no-rebuild.

  `CreateMeshConstants` writes one `Mesh_Constants` per non-line mesh. Required fields for voxel
  draws are alpha/base alpha, `meshDataOffset` (node storage byte offset consumed by culling and
  GBuffer), texture mask/material id/image indices, material byte offset, editor/double-sided flags,
  render type, and local AABB min/max. DX12 additionally mirrors this buffer to
  `m_meshConstantsDevice`.

  Decision for the spike: do **not** use a separate dedicated voxel geometry buffer if the goal is
  to inherit the current GBuffer/depth/culling draw path. `GbufferPass` binds a single index buffer
  and a single vertex buffer from `Scene::GetBuffer()`, so a separate voxel geometry buffer would
  need a new draw path or per-draw buffer indirection. Spike 0A should use a reserved tail/arena
  inside the scene geometry buffer (or deliberately choose the fallback voxel-owned pass if this
  becomes too invasive).

  ### 0A EXTENDED data-flow map (verified against current code 2026-06-22)

  **The complete index-aligned chain for ONE Scene raster draw at dense index `idx`.** Every piece
  below is addressed by the SAME `idx` (= `firstInstance`, = the cull dispatch thread id, =
  `MeshConstants[idx]`). `GBufferPass` (`GbufferPass.cpp:166`) and the cull dispatch
  (`Scene.cpp:1123/1441`) both use `maxDrawCount = m_meshCount`. So an incremental add must bump
  `m_meshCount` AND populate all of:

  1. **`m_indirectAll[idx]`** — `PeDrawIndexedIndirectCommand{indexCount, instanceCount=1,
     firstIndex=mesh.indexOffset, vertexOffset=mesh.vertexOffset, firstInstance=idx}`. Created
     EXACT-sized (`std::max(1u,indirectCount)`) at `SceneBuffers.cpp:305-313`, DEFAULT heap. Cull
     reads it as `IndirectCommandsIn` (binding 0). **Must be pre-reserved to a capacity** so an
     append doesn't recreate it.
  2. **`m_meshConstants[idx]`** (`Mesh_Constants`, 64B, layout in `MeshConstants.h`) — append one
     entry. Fields the cull/GBuffer actually consume: `renderType` (bucket select; 1=opaque),
     `editorFlags` bit1=doubleSided / bit0=selected, `meshDataOffset` (byte offset into NodeData,
     see #4), `aabbMin/Max{X,Y,Z}` (LOCAL aabb, transformed by the node matrix in the shader),
     plus material fields (`materialId`, `meshImageIndex[5]`, `materialByteOffset`, `textureMask`,
     `alphaCut`, `baseColorAlpha`) for GBuffer shading. Created exact-sized
     (`std::max(1,m_meshCount)`) at `SceneBuffers.cpp:936`.
  3. **DX12 ONLY `m_meshConstantsDevice[idx]`** — the cached DEFAULT mirror (commit `9f1e8cfb`).
     `GetMeshConstants()` (`Scene.cpp:224-229`) returns this on DX12, so the cull pass binds it
     (binding 1). The CPU-visible `m_meshConstants` is GPU_UPLOAD and is only the staging SRC. **An
     incremental add MUST copy the new entry from `m_meshConstants` → `m_meshConstantsDevice` (a
     `cmd->CopyBuffer` of the new 64B range, or the whole used region) with a COMPUTE/VERTEX-read
     barrier BEFORE the same frame's cull dispatch reads it** — otherwise DX12 reads a stale/zero
     entry for `idx` while Vulkan (which reads `m_meshConstants` directly) is correct. This is the
     exact DX12 silent-desync the spike must prove handled.
  4. **Per-node storage slot at byte `meshDataOffset`** in `m_storages[frame]` (and DX12
     `m_storagesDevice[frame]`). Holds `NodeGpuData` (144B): `worldMatrix` (cull `LoadMatrix` +
     `TransformAABB` at `CullingCS.hlsl:230`), and `renderVisible` at **+128** (cull early-out at
     `CullingCS.hlsl:224`). `meshDataOffset = m_nodeRuntime[node].dataOffset`, assigned ONLY in
     `CreateStorageBuffers` (`SceneBuffers.cpp:197`) for nodes with a drawable mesh. **This is the
     piece that makes a fully-generic AddArenaMesh invasive** — a brand-new node has
     `dataOffset=-1`/`hasUniformData=false` until a storage rebuild. Spike sidesteps it by attaching
     the arena meshes to a node that ALREADY owns a storage slot from the reservation rebuild, and
     reusing that node's `dataOffset` as `meshDataOffset` (quads share one identity world matrix;
     each quad's world position is baked into its vertices). The per-frame `UploadDynamicUniforms`
     (`Scene.cpp:914`) already mirrors `m_storages`→`m_storagesDevice` on DX12, so the shared node
     slot stays valid on both backends with no extra work.
  5. **`m_visibility[idx]` = 1** — seed via `FillBuffer` (mirror `SceneBuffers.cpp:411`). Sized to
     `m_indirectCapacity` (pow2), so already has reserved slack; just seed the one new slot.
  6. **Geometry bytes** — vtx into `m_buffer` at `m_verticesOffset + vtxByteOff`, indices at byte
     `idxByteOff` (index region starts at offset 0), positions-uv into `m_positionsOffset + ...`.
     `CopyBufferStaged` mirrors the in-place pattern at `SceneNode.cpp:871-872`.

  **Filtered/occlusion buffers** (`m_indirectOpaqueSS/DS`, `m_occ*`, sort keys, counters) are sized
  to `m_indirectCapacity` (pow2, `SceneBuffers.cpp:332/406`) — they are cull OUTPUTS, written by the
  dispatch, so they need only enough capacity for `m_meshCount` entries; pre-reserving
  `m_indirectCapacity` large enough (or recreating them pow2-padded once at reservation) covers all
  appends without per-add recreation.

  **Reservation strategy used by the spike:** after the scene's normal startup `UploadBuffers`, do a
  ONE-TIME reservation that (a) pre-grows `m_buffer` with vtx+idx+posUv voxel headroom (rebuild
  `m_buffer` larger, re-copy existing stores — done ONCE, not per-add), (b) bumps `m_indirectCapacity`
  to `pow2(currentMeshCount + voxelCapacity)` and recreates `m_indirectAll` + all filtered/occlusion
  buffers at that capacity, (c) recreates `m_meshConstants`(+device mirror) at that capacity, (d)
  creates one "voxel host" node with a real storage slot. Then each `AddArenaMesh` is pure append +
  the DX12 mirror copy, no buffer recreation. **`AddArenaMesh`/`RemoveArenaMesh` therefore never call
  `UploadBuffers`/`RebuildRasterInstances`** — that is the property the smoke verifies via the log.

  ### 0A RESULT — VALIDATED, DECISION = **GO / PROCEED** (productize in Task 7) — 2026-06-22

  Empirically verified on **Vulkan + DX12** (RTX 4080 SUPER) by appending one mesh into a pre-reserved
  arena in the SHARED Scene buffer of a live 50,000-mesh scene (`culling_test.pescene`), rendered
  through the existing GBuffer + GPU cull path, then freed — with **no `UploadBuffers` /
  `combined_Geometry_buffer` recreation after startup** (log shows 0 occurrences on both backends),
  no `Submit`-stall-induced failure, no validation errors. Three-shot harness (PhasmaPlayer):
  - **present**: appended draw renders through GBuffer (occludes scene geometry → correct depth).
  - **culled**: camera yawed 180° → appended draw drops out (inherits GPU frustum culling).
  - **freed**: `RemoveArenaMesh` → appended draw disappears, scene intact.
  DX12 path exercised the DEFAULT mesh-constants mirror (`m_meshConstantsDevice`) and rendered
  pixel-equivalently to Vulkan, proving the mirror is published before the same-frame cull reads it.

  **THREE REAL BUGS the spike surfaced (all fixed in the validated shape; this is why the gate exists):**
  1. **Exact-sized append buffers overrun.** `m_indirectAll` AND `m_meshConstants`(+DX12 device mirror)
     are created EXACT-sized to `m_meshCount` by `CreateIndirectBuffers`/`CreateMeshConstants`, NOT
     `m_indirectCapacity`-padded like the filtered/occlusion buffers. The first attempt grew them only
     inside `if (newCap > m_indirectCapacity)`; on a scene where `pow2(meshCount)` already equals
     `m_indirectCapacity` (the 50k case: 65536), that branch is skipped and the append writes past the
     buffer end → `CopyBufferStaged: dst range overflow` / `CopyDataRaw: too large` crashes. **Fix:**
     in `ReserveArenaCapacity`, regrow `m_indirectAll` and `m_meshConstants`(+mirror) **unconditionally**
     to `newCap` (the filtered/occlusion/visibility buffers stay gated on `newCap > m_indirectCapacity`,
     since they are already pow2-sized).
  2. **Dual vertex-stream `vertexOffset` invariant (the big one — caused silent invisibility).** The
     combined buffer is `[indices][aabbIdx][vertices: Vertex][positions: PositionUvVertex][aabbVerts]`.
     The **GBuffer** binds the `Vertex` stream at `m_verticesOffset`; the **DEPTH PREPASS**
     (`DepthPass.cpp:138`, binds at `m_positionsOffset`, stride `PositionUvVertex`) AND shadows use the
     `PositionUvVertex` stream. Both passes index with the SAME per-draw `vertexOffset` int. So a draw's
     Vertex index and PositionUvVertex index **must be EQUAL**. A naive tail-append (independent vtx and
     posUv tail byte-bases) makes `vertexOffset` correct for the GBuffer but WRONG for the depth prepass
     → the prepass writes garbage/no depth → the GBuffer's reverse-Z depth-**EQUAL** test rejects every
     fragment → **the mesh is silently invisible** (renders nothing, no error; ALL face-cull / winding /
     size / culling-off experiments stay black because depth never matched). **Fix:** `ReserveArenaCapacity`
     must **re-lay-out** the buffer — extend BOTH the vertices region and the positions region by the same
     vertex headroom and shift `m_positionsOffset`/`m_aabbVerticesOffset` — so arena vertex *k* lives at
     index `(origVertexCount + k)` in BOTH streams, and `AddArenaMesh` uses that single shared index as
     `vertexOffset` (and derives both byte offsets from it). Index data goes in a separate tail.
  3. **Free must NEUTER GEOMETRY, not just zero the indirect entry.** With `indirectCount` support, the
     two-phase occlusion path COPIES emitted draw commands into persistent filtered output buffers
     (`m_occOpaque*A/B`) that are NOT cleared per-frame; the GBuffer reads those, not `m_indirectAll`.
     So decrement-`m_meshCount` + zero-`m_indirectAll[idx]` alone can leave a stale filtered copy drawing.
     **Fix:** `RemoveArenaMesh` also zeroes the removed draw's INDEX bytes in `m_buffer` (degenerate draw)
     in addition to zeroing `m_indirectAll[idx]`, `m_visibility[idx]`, and decrementing `m_meshCount`.
  - Plus: bump `m_geometryVersion` on reserve/add so the GBuffer **set-1** (textures/material/MeshConstants
    for the PS) re-binds to the recreated buffers (the old ones are queued for deletion); set-0 (the VS
    `MeshConstants`) is already re-bound every frame, which is why this only matters once real materials
    are used.

  **Validated arena API shape (the Task-7 contract):**
  - `int Scene::ReserveArenaCapacity(uint32_t vtxHeadroomBytes, uint32_t idxHeadroomBytes,
    uint32_t posUvHeadroomBytes, uint32_t extraDrawCapacity)` — ONE-TIME after startup `UploadBuffers`.
    Re-lays-out `m_buffer` (vtx+pos regions each grown by a shared vertex headroom; idx tail), shifts
    `m_positionsOffset`/`m_aabbVerticesOffset`, unconditionally regrows `m_indirectAll`+`m_meshConstants`
    (+DX12 mirror) to `pow2(meshCount+extra)`, conditionally regrows filtered/occlusion/visibility,
    sets `m_geometryDirty=false`, bumps `m_geometryVersion`. Returns 0 / -1.
  - `int Scene::AddArenaMesh(const std::vector<Vertex>&, const std::vector<PositionUvVertex>&,
    const std::vector<uint32_t>& indices, const AABB& localBox, uint32_t reuseDataOffset,
    const MeshRuntime& runtimeForImages)` — appends at `idx=m_meshCount`: copies vtx+posUv at the SAME
    shared vertex index, indices to the tail; writes `m_indirectAll[idx]` (`vertexOffset=sharedIndex`,
    `firstIndex=idxByteOff/4`, `firstInstance=idx`); writes `m_meshConstants[idx]` (renderType=1,
    meshDataOffset=reuseDataOffset, local AABB, material fields from runtime); **DX12: copies that one
    entry into `m_meshConstantsDevice[idx]` with a compute/vertex-read barrier** (the same-frame mirror);
    `FillBuffer m_visibility[idx]=1`; `++m_meshCount`; bumps `m_geometryVersion`. Returns idx / -1.
    NOTE: caller must supply CCW-from-outside winding (engine is FrontFace=CW + cull FRONT); Task 7
    should fold an internal `ForceWindingCCW`-equivalent into AddArenaMesh so callers can't get it wrong.
  - `void Scene::RemoveArenaMesh(int idx)` — spike supports LIFO last-slot only: zeroes
    `m_indirectAll[idx]`, `m_visibility[idx]`, NEUTERS the index bytes in `m_buffer`, reclaims the arena
    vertex/idx ranges, `--m_meshCount`. Task 7 generalizes to a free-list (swap-remove needs index
    compaction since draw index == firstInstance == storage index).
  - **Storage-slot sidestep (still required):** the appended mesh REUSES an existing node's storage
    `dataOffset` as `meshDataOffset` (new nodes have `dataOffset=-1` until a storage rebuild). Task 7
    creates ONE persistent "voxel host" node at world-create whose identity-ish matrix all voxel section
    meshes share (each section bakes its world origin into its vertices), so per-section adds never touch
    `CreateStorageBuffers`. (The spike forced an existing scene node's slot to identity to prove this.)

  **Caveat on the spike driver only (NOT productization):** the throwaway `AddArenaMesh`/`Reserve`/`Remove`
  use per-op `Submit`+`Wait` (and `Queue::WaitIdle` in Reserve). The reservation stall is one-time and
  acceptable; the per-add stalls are NOT — Task 7 must route the per-add copies + DX12 mirror copy through
  the frame's command buffer (publish the mirror inside `UploadDynamicUniforms`, which already runs at the
  top of `ExecuteRenderGraph` before any cull dispatch) so streaming adds incur no stall. The spike proves
  *correctness* of incremental management on both backends; the no-stall shape is a mechanical follow-up.

- _0B array-texture + custom-shader result (works / fell back to 2D atlas):_

  ### 0B RESULT — VALIDATED, DECISION = **GO / PROCEED with Texture2DArray** (productize in Task 8) — 2026-06-22

  Empirically verified on **Vulkan + DX12** (RTX 4080 SUPER). A 2-layer `Texture2DArray`
  (16×16 RGBA8, layer 0 = solid red, layer 1 = solid green, uploaded per-layer with
  `baseArrayLayer` 0 then 1), bound through a graphics pipeline whose pixel shader declares
  `Texture2DArray<float4>` and samples `gVoxelAtlas.Sample(s, float3(frac(uv), layer))` with the
  array layer chosen per-quad from the instance id (the `joints[0]` analogue), renders TWO quads:
  **Quad A = RED (layer 0), Quad B = GREEN (layer 1)**, with uv spanning 0..4 (frac-tiled).
  - **Vulkan:** offscreen RGBA8 readback asserts leftQuad=(255,0,0,255), rightQuad=(0,255,0,255),
    exit 0. Screenshot `_voxel_array_Vulkan.png`.
  - **DX12:** live-window screen capture shows the identical red+green quads (title bar = DX12 device).
    Screenshot `_voxel_array_DX12_window.png`. Pixel-identical to Vulkan.

  **Reflection finding (the spec's Step-3 worry — RESOLVED, no fallback needed):** the engine's
  shader reflector is **dimension-agnostic by construction**. `ImageReflection`
  (`Phasma/Core/Code/API/Reflection.h:95-103`) records `name/set/binding/dxRegister/dxSpace/count`
  and **no view-type/dimension field** — there is no code path that can inspect or reject a
  `Texture2DArray` vs `Texture2D`. The descriptor layer treats every sampled image as a plain
  SAMPLED_IMAGE; the array dimension lives entirely in the `ImageView` created by
  `CreateSRV(PE_IMAGE_VIEW_TYPE_2D_ARRAY)`, which both backends map to a real array SRV
  (`Dx12ImageViewImpl::SrvDimension` → `D3D12_SRV_DIMENSION_TEXTURE2DARRAY`;
  `VulkanImageImpl`). Both the DX12 (SPIR-V-for-WebGPU compile path) and Vulkan reflectors accepted
  the `Texture2DArray` declaration and built the descriptor with **no validation error**; the draw
  succeeded on both. Corroborated by the already-shipping `WebGPUGenerateMipmapTest`
  (`Tests/GenerateMipmap/Shaders/texturedGeometry2dArray.pixel.hlsl`) which samples
  `Texture2DArray<float4>` by `float3(uv, layer)` and runs clean on DX12 in this same tree.

  **Kept Task-8 artifacts (in tree, unstaged):**
  - `Phasma/Runtime/Assets/Shaders/Voxel/VoxelGBufferVS.hlsl` — reads `joints[0]`→`nointerpolation
    uint tileLayer`, stock GBuffer transform, no skinning (voxels are static).
  - `Phasma/Runtime/Assets/Shaders/Voxel/VoxelGBufferPS.hlsl` — `Texture2DArray<float4> gVoxelAtlas`,
    `Sample(s, float3(frac(input.uv), input.tileLayer))`, writes the 6 stock GBuffer MRT targets.
  - `Phasma/Runtime/Assets/Shaders/Voxel/voxel_gbuffer.passinfo` — JSON mirroring `standard_pbr.pass`
    (surface/depthOnly/shadowCaster variants) referencing the two shaders.

  **CRITICAL render-integration finding for Task 8 (surfaced while tracing, not a 0B blocker):**
  the opaque GBuffer pass currently **ignores `Material::passInfoAsset`**. `GbufferOpaquePass`
  (`GbufferPass.cpp:179-205`) binds exactly ONE pipeline (`standard_pbr` `m_passInfo` + the
  double-sided `m_passInfoDS`) and draws ALL opaque meshes with `DrawIndexedIndirectCount`. There is
  no per-material pipeline switch — a mesh's `passInfoAsset` is not consulted at opaque draw time.
  So Task 8 must add a render path for the voxel material: bind the `voxel_gbuffer` pipeline and
  issue a SEPARATE indirect draw for the voxel section meshes (which the arena from Task 7 can group),
  writing the same GBuffer targets. The named-texture index path (`Material::namedTextures` →
  `Scene::UpdateImageViews` → `namedTextureIndices`, `SceneBuffers.cpp:485-503`) feeds the bindless
  index into the material byte buffer; the `gVoxelAtlas` binding in the custom PS resolves through
  that same dimension-agnostic mechanism. This pairs naturally with Spike-0A's "separate indirect
  draw for arena meshes" — voxels become their own draw call set under the existing pass.

  **Notes for Task 8:** (1) frac-tiling is exercised but visually flat with solid-color layers — use
  patterned tiles to eyeball the 4× repeat. (2) the engine reflector ignores every texture's
  dimension, so a single bindless `Texture2DArray` array (or discrete binding) works identically to
  `Texture2D`; no engine change is needed to accept the array. (3) the throwaway WebGPU
  `ReadTextureRgba8`/`DumpTextureToBmp` readback path access-violates on DX12 (harness bug, unrelated
  to array textures — DX12 array *rendering* succeeded); irrelevant to engine code.
