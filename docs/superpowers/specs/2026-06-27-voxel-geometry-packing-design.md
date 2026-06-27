# Voxel Geometry Packing — Design Spec (2026-06-27)

**Status:** proposed. Follows the Phase-1 voxel core (`docs/superpowers/specs/2026-06-22-voxel-core-engine-design.md`)
and the 2026-06-27 worldgen/AO/caves session that exposed the cost.

## Problem

Voxel chunk vertices are ~15× fatter than they need to be, and the arena over-reserves VRAM. Two
distinct costs, often conflated:

1. **Per-vertex bytes.** A voxel arena vertex occupies *both* shared Scene streams:
   - `Vertex` (`Core/Code/API/Vertex.h`): position f32×3, uv f32×2, normals f32×3, tangent f32×4,
     color f32×4, joints u32×4 = **80 B**.
   - `PositionUvVertex`: position f32×3, uv f32×2, joints u32×4 = **36 B**.
   - Combined **~116 B/vert** for data that is almost entirely constrained constants.
2. **Reservation waste.** The arena pre-reserves a shared pool `gridSections × kVertsPerSection`
   (`VoxelWorld.cpp` ~175). It cannot grow live (growing `m_buffer` collides with the live arena —
   see Spike-0A bug family). At `loadRadius=6`, `1024` verts/section the pool is ~4.7M verts ≈ **545 MB**,
   most of it covering always-empty sky sections.

AO-aware greedy merging (fewer merges near edges) + cave interiors (many new wall faces) pushed
sections from ~24 verts to ~1–4k, which is what surfaced both costs (arena OOM → terrain holes,
fixed for now by bumping the per-section budget to 1024).

## What a voxel vertex actually needs

| attribute | now | needs | source |
|-----------|-----|-------|--------|
| position  | 12 B | 5+5+5 bits | section-local int 0..16; section world origin moves to per-draw constants |
| normal    | 12 B | 3 bits | one of 6 face directions |
| tangent   | 16 B | 0 | derived from normal in the VS |
| color/AO  | 16 B | 2 bits | 4 AO levels (already only AO) |
| uv        | 8 B | ~5–8 bits | merged-quad extent (small ints) |
| tile      | 16 B (joints[0]) | 16 bits | already only joints[0] used |

**Packed target = 2× uint32 = 8 B/vert:**
- `w0`: posX:5, posY:5, posZ:5, normal:3, ao:2  (20 bits used, 12 spare for future block-light)
- `w1`: tile:16, u:8, v:8

~116 B → 8 B = **~14.5×**. Plus indices: section verts < 65536 ⇒ `uint16` indices halve the index region (bonus).

## Architecture change (the invasive part)

Today voxels live in the *shared* Scene `Vertex`/`PositionUvVertex` regions so they are "first-class
scene meshes." But voxels have **already diverged**: dedicated `VoxelGBufferVS/PS`, dedicated indirect
bucket (counter 7), cull-skip + standalone draw, no RT BLAS (Phase 1). Packing completes that
divergence: **voxel vertex data moves out of `m_buffer` into a dedicated packed buffer.**

- A new **dedicated voxel vertex buffer** (packed 8 B stride) owned by the arena/Scene, bound only by
  `VoxelGBufferVS`. The arena allocates voxel verts from this buffer, not the shared Vertex region.
- Draw indirection, frustum cull emit (counter 7), and the indirect bucket are **format-agnostic**
  (index/draw-level) and stay unchanged.
- `VoxelGBufferVS` unpacks `w0/w1` and adds the **section world origin** (new per-draw constant /
  reuse the host-node matrix path) to the section-local position. This removes the per-vertex baked
  world origin that currently forces f32 positions.
- **Voxel `PositionUvVertex` is NOT dead — it feeds shadows (CORRECTED 2026-06-27).** `DepthPass`
  draws only counters 0/1/5/6, never voxels (`DepthPass.cpp:128-146`) — so voxels skip the *depth
  prepass*. BUT `ShadowPass` (`ShadowPass.cpp:336-341`) binds `GetPositionsOffset()` (the
  `PositionUvVertex` stream) and draws `GetIndirectAll()` over `GetMeshCount()` — which **includes the
  voxel arena slots, every cascade**. So voxels cast shadows by reading their `PositionUvVertex`. The
  earlier "voxels skip shadows" assumption was false. Consequences:
  - **The standalone posUv-deletion step (old T1a) is removed — unsafe.** Voxels need position data
    for shadow casting regardless.
  - The win comes from **packing** that position, not deleting it: the dedicated packed buffer must
    feed a **dedicated voxel shadow draw** (packed-aware shadow VS) *and* voxels must be excluded from
    the standard `ShadowPass` `GetIndirectAll` draw (else it reads stale/missing posUv for voxel slots).
  - Decision required: keep voxel shadows (adds the voxel shadow draw — recommended; terrain
    self-shadowing matters) vs drop them (simpler, visual regression).

## Grow-on-pressure reservation (subsumes the VRAM trim)

Because the dedicated voxel buffer is **voxel-only** — not entangled with regular meshes / instances /
cull buffers like `m_buffer` is — it can be grown safely when the free-list runs low (drain in-flight
frames, realloc + copy, like a vector). So: reserve a **modest** initial pool and **grow on pressure**
instead of pre-reserving for the worst case. This is the correct fix for the reservation waste; the
fragile alternative (a hardcoded "occupied sections per column" fraction) is rejected because worldgen
is now pluggable (`ITerrainGenerator`), so `VoxelWorld` cannot know the terrain height profile.

## Risks

- **Lost "shared buffer" property.** Voxels stop being literal shared-buffer scene meshes. Acceptable —
  they already are not (own VS/PS/bucket/cull-skip; no BLAS). RT reflection of voxels stays out of scope.
- **Cross-backend packed fetch.** Vulkan + DX12 must both fetch the 8 B format (ByteAddressBuffer load +
  bit unpack in the VS is the safe, backend-neutral route — mirrors how the cull shader already does
  `NodeData.Load`). Verify pixel-parity both backends.
- **Section origin in constants.** Moving origin out of vertices needs a per-section offset the VS reads;
  reuse the existing mesh-constants/host-node matrix plumbing rather than a new buffer.
- **uint16 indices** need the section vertex count < 65536 (true) and a backend index-type plumb.

## Phased tasks (each GPU-smoked on Vulkan + DX12, force-kill after)

- **T1 — dedicated voxel vertex buffer, same attributes, + voxel shadow draw.** Move voxel verts out
  of the shared streams into a new buffer with the *current* fat `Vertex` layout; `VoxelGBufferVS`
  binds it; section-local pos + origin-in-constants. **Also**: exclude voxels from the standard
  `ShadowPass` `GetIndirectAll` draw and add a dedicated voxel shadow draw (voxel bucket + the new
  buffer + a depth-only voxel shadow VS) so voxels keep casting shadows. No bit-packing yet. Goal:
  prove the decoupling renders + shadows identically. Drops the shared `Vertex`+`PositionUvVertex`
  occupancy and lets the buffer grow-on-pressure. (The old "delete posUv in the shared buffer" step is
  gone — posUv is live for shadows; see the Architecture correction above.)
- **T2 — pack to 8 B.** Switch the buffer to the `w0/w1` layout; mesher emits packed verts; VS unpacks.
  Verify parity (terrain + AO + caves) both backends.
- **T3 — grow-on-pressure.** Replace the fixed reservation with a modest initial pool + grow-on-low.
  Removes the ~hundreds-of-MB over-reservation; remove the `kVertsPerSection=1024` worst-case bump.
- **T4 (bonus) — uint16 indices.** Halve the index region.

## Expected outcome

Per-vert ~116 B → ~8 B (~14×) and right-sized reservation → voxel geometry VRAM from hundreds of MB to
tens, at `loadRadius=6`. Greedy meshing remains the primary face-count reducer; this attacks the
bytes-per-retained-vertex, which AO + caves inflated.
