# Terrain System (streamed isosurface terrain)

Smooth, sculptable terrain as a dedicated subsystem under `Phasma/Runtime/Code/Terrain/`
(`TerrainSystem` reconciles the singleton Terrain node's `NodeTerrainTag`; `TerrainWorld` owns the
world). Carved out of the voxel subsystem — cubes stay in `Voxel/`; terrain shares only the worldgen
seam (`voxel::ITerrainGenerator`: `NoiseGen` / `MapGen`) and the `voxel::SurfaceNetsTile` mesher.

## The load-bearing idea: fixed tiles, updated in place

The world is a grid of fixed **tiles**: budgeted vertex/index ranges reserved once in the Scene's
SHARED geometry buffer (regular scene meshes on one `TerrainHost` node — so shadows, GPU cull, Hi-Z
and per-tile meshopt LODs all just work). A tile is re-meshed by rewriting its CPU store ranges and
staging just those bytes to the GPU via `Scene::UpdateStreamedMesh` (data + indirect draw +
`Mesh_Constants` + DX12 mirror — the same staged-copy pattern `AddArenaMesh` proves for the voxel
buffers). **No geometry rebuild per sculpt or stream step**; the only rebuilds are Create and the
rare budget-overflow "grow": one overflowing tile means the ring's shared budget estimate lost to
this worldgen region, so the WHOLE ring gets doubled ranges appended in ONE rebuild (slots are
reused toroidally — growing per tile would replay the rebuild per slot). Live content is copied
into the new ranges, so only the overflowed tile re-meshes; old ranges leak until scene load.

- A mesh slot must never hit `indexCount == 0` (it would lose its indirect/constants slot on the
  next full rebuild) — empty tiles keep one degenerate triangle far below the world.
- Indirect/constants slots shuffle on every full rebuild; `Scene::GetMeshRefIndirectSlot` re-queries
  per update, so an unrelated rebuild in between is safe (stores already hold the latest content).

## Density, overhangs, sculpting

Ring-0 tiles mesh a density field with Surface Nets (`voxel::SurfaceNetsTile`) sampled from an
IMPLICIT function — no stored volume, no memory ceiling, and sculpts survive streaming by
construction:

```
density(x,y,z) = generator->DensityAtHeight(x,y,z, h(x,z))   // h cached once per corner column
               ∘ CSG sculpt ops (sphere union/subtract, stroke order)
```

- **Worldgen overhangs**: `TerrainConfig::overhangs` (0..1) adds a near-surface 3D FBM warp in
  `NoiseGen::DensityAtHeight`. The quadratic fade bounds the displaced surface inside
  `SurfaceHeight ± 0.5·(heightMax−heightMin)` for ANY FBM amplitude (the fade fixed point
  `rel = c(1−rel²)` is < 1 for all c), so tile meshers can size their vertical band exactly.
- **Sculpt**: `TerrainWorld::Sculpt(center, radius, amount)` appends a CSG sphere op (amount < 0 =
  subtract, else union; magnitude ignored) and dirties overlapping ring-0 tiles. Ops are the
  persistent truth — a tile streamed out and back re-applies them. Digging sideways into a cliff
  undercuts it; a subtract below the surface makes a grotto with the surface intact. Lua:
  `terrain.sculpt(x, z, r, amount)` (raycasts the true surface first, so repeat digs burrow) and
  `terrain.sculpt3d(x, y, z, r, amount)`. Ops serialize with the scene (`sculptOps` on the terrain
  tag — flat vec4s: xyz centre, |w| radius, w < 0 digs; TerrainSystem syncs world → tag as you
  sculpt and seeds every recreate from it), so sculpts survive save/load and structural rebuilds.
  Sculpts made during play persist after stop — matching the mesh, which never reverts.
- **Painted caves**: `cavesPath` (tag/inspector/Lua `caves_map`) is a grayscale map painted in Map
  Painter's "Caves (Terrain)" layer, sharing the heightmap's extent/orientation (centred on the
  bounds column, both axes flipped, zero OUTSIDE the map so streamed worlds don't tile it). Pixel
  value = openness: the void is centred `3 + 3` m below the LOCAL surface with half-height
  `3 m · v`, pinching closed where the paint fades (bilinear). Applied between worldgen and sculpt
  ops, so sculpting can open entrances (or fill a cave back in); overhang-displaced surfaces open
  natural mouths. A heightmap can never roof a void — this layer is how caves get painted top-down.
- `terrain.raycast` / `IsSolidCell` march the density (overhang- and sculpt-aware);
  `terrain.height` is the pure worldgen heightfield.

### Watertight tile seams (SurfaceNetsTile contract)

Cells live on a GLOBAL cubic lattice; positions and densities derive from global integer corner
indices, so adjacent tiles compute bit-identical boundary vertices (deriving from a float origin
drifts by ulps and hairline-cracks the seam). Each tile scans a one-cell negative **apron** whose
cells emit stitch VERTICES but no quads (the neighbour owns those edges and emits them with its own
identical duplicates), plus a one-corner guard ring so central-difference normals stay full-stencil
at the seam (no lighting seam).

## Streaming (clipmap-style toroidal windows)

With `streaming` on, each ring's window follows the anchor (the active camera, fed by
`TerrainSystem::Update`) with **toroidal slot reuse**: slot s always holds the window tile with
`T ≡ s (mod N)`, so nothing moves — a slot is just re-meshed for its new world tile when the window
slides. Dirty tiles re-mesh nearest-first, a few per frame (`kMeshBudgetPerUpdate`), uploads ride
one submitted-not-waited command buffer per Update (VoxelWorld's retire pattern).

Two coarse view rings (cell size ×4, ×16; same tile counts) extend the view distance. Coarse tiles
are plain heightfield grid meshes (no ops/overhangs — a big dig pops at the fine rim, same trade as
voxel LOD cave mouths) with two-sided perimeter **skirts**, sunk 0.35 cells below the true surface
so their overlap band never pokes through finer terrain. A coarse ring holes out tiles the finer
ring covers, keeping a one-tile overlap band under the fine window's rim. Bounded mode
(`streaming` off) is the same machinery with a pinned window and ring 0 only.

## Collision

Per-tile static Jolt mesh bodies via `PhysicsSystem::AddStaticMeshBody` (raw, not node-backed —
handle-keyed, cooked from a tile's lod0 triangles). The collider ring covers tiles within
`collisionRadiusM` of the anchor (0 = every live tile, the bounded default), cooks a couple per
frame, and re-cooks on sculpt/re-stream. Friction/restitution apply live. The Jolt world outlives
play toggles, so bodies persist; `SetPhysicsEnabled(false)` drops them all.

## Config / plumbing

`TerrainConfig` ⇄ `NodeTerrainTag` (inspector under the Terrain node, serializer keys `streaming`,
`overhangs`, `collisionRadiusM`, Lua `node:set_terrain{streaming=, overhangs=, collision_radius_m=}`).
`streaming`/`overhangs` are structural (rebuild, debounced); collision radius and physics knobs are
live. Colour bands normalize to the CONFIGURED height range so they never shift while streaming.

## Known limits / follow-ups

- Meshing is main-thread, budgeted (~3 tiles/frame; overhang tiles cost ~5–10 ms each) — worker
  threads are the next step if streaming while walking ever stutters.
- RT/BLAS does not track in-place tile updates (stale RT terrain until some full rebuild).
- Coarse rings ignore sculpts/overhangs; grow leaks the old range until the next scene load.
