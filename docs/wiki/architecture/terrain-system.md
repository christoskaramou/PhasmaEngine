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
The estimate accounts for overhangs, a configured caves map (folded surface sheets) and the
scatter map's measured demand; and a **grow is remembered** across recreates of the same worldgen
(hash of the mesh-affecting config), so inspector edits/rebuilds never replay the grow chain —
a world pays each grow once per session.

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
  `terrain.sculpt3d(x, y, z, r, amount)`.
- **Level ops** (smooth/flatten): a second op type pulls the density toward the plane `y = targetY`
  inside the sphere with a quartic falloff (C1 at the rim, so strokes leave no seam) and a per-stamp
  `weight` — weight 1 is a hard flatten, partial weights toward a local surface average make a
  smooth brush that converges over a held stroke. Lua: `terrain.flatten(x, z, r, [y], [weight])`
  (default target = the true surface under the centre) and `terrain.smooth(x, z, r, [strength])`.
  Sphere and level ops live in ONE ordered list (stroke order matters: flatten-after-dig fills the
  hole, dig-after-flatten re-opens it).
- **Op persistence**: ops serialize with the scene as `terrainOps` on the terrain tag — flat 7-float
  records `[type, cx, cy, cz, radius, a, b]`; type 0 = sphere (a = 1 digs), type 1 = level (a =
  targetY, b = weight). Legacy `sculptOps` vec4 records still load (converted to sphere records).
  TerrainSystem syncs world → tag as you sculpt and seeds every recreate from it, so ops survive
  save/load and structural rebuilds. Sculpts made during play persist after stop — matching the
  mesh, which never reverts.
- **Painted caves**: `cavesPath` (tag/inspector/Lua `caves_map`) is a grayscale map painted in Map
  Painter's "Caves (Terrain)" layer, sharing the heightmap's extent/orientation (centred on the
  bounds column, both axes flipped, zero OUTSIDE the map so streamed worlds don't tile it). Pixel
  value = openness: the void is centred `3 + 3` m below the LOCAL surface with half-height
  `3 m · v`, pinching closed where the paint fades (bilinear). Applied between worldgen and sculpt
  ops, so sculpting can open entrances (or fill a cave back in); overhang-displaced surfaces open
  natural mouths. A heightmap can never roof a void — this layer is how caves get painted top-down.
- `terrain.raycast` / `IsSolidCell` march the density (overhang- and sculpt-aware);
  `terrain.height` is the pure worldgen heightfield.
- **Interior tint**: vertices well below their column surface with solid overhead (cave floors and
  walls, checked with two density taps above the vertex) and down-facing undersides darken toward a
  rock tint, so painted caves and grottos stop reading grass-green inside. Under the triplanar path
  the tint modulates the textures (the underwater blue mix rides the same channel); pure vertex
  colouring, no shader branch.

## Terrain texturing (triplanar splat)

Terrain draws through a **dedicated GBuffer pipeline** (`Shaders/Terrain/TerrainGBufferPS.hlsl`),
not the standard PBR shader — the voxel subsystem's precedent for a subsystem that needs its own
gbuffer texturing. It reuses the stock `GBufferVS` (terrain tiles are standard float meshes, so
`positionWS` + world normal arrive interpolated) and only swaps the pixel shader. Four **layer**
albedo textures (0 grass, 1 rock, 2 sand, 3 snow — configurable `layerPaths`, default the built-in
`Textures/Voxel/*` tiles) are sampled **triplanar** (three world-plane projections blended by the
sharpened normal, so cliffs never stretch) and blended by per-pixel weights. Weights come from an
optional RGBA **splat map** (`splatPath`; R/G/B/A = layer weights) where painted, else an **auto
height/slope selection**: sand near sea level, grass on the mid band, snow on peaks, rock on any
steep face. So terrain is fully textured with zero authoring, and a splat map is a pure override.

- **No per-draw constants**: the mesher packs the normalized surface height into `color.a` (scatter
  props < 0.5 keep their baked colour; ground = 0.5 + 0.5·f), so the shader keys its auto selection
  off the vertex alone — no material table / uniform buffer to keep in sync. `color.rgb` is the
  interior/underwater tint. Layers load UNORM (SRGB storage images are illegal on DX12) and are
  linearized in-shader.
- **Dedicated cull bucket**: terrain is standard geometry (not a voxel arena), so a triplanar
  pipeline needs its own GPU-cull bucket, not just a shader swap. `Material::terrain` sets
  `editorFlags` bit 0x10 (`ComputeMeshConstants` + `UpdateMeshSelectionFlags`), `CullingCS` routes
  those draws to bucket 8 (`IndirectTerrainOut`, counter index 8) and excludes them from the
  standard/opaque buckets, and `GbufferOpaquePass` draws bucket 8 with `m_terrainPassInfo` after the
  standard + voxel draws. The layer/splat views bind to a dedicated descriptor set
  (`Scene::SetTerrainLayerView`/`SetTerrainSplatView`), like the voxel atlas.
- Terrain still casts shadows and writes depth normally (the shadow cull only tests the voxel bit,
  so terrain rides the regular shadow bucket; the surface variant writes its own depth like voxel).
  Isolating the ~12-tap triplanar shader to terrain draws keeps it off every other opaque pixel.
- **Per-layer material maps** (optional): each layer may also supply a material texture (`materialPaths`,
  RGB = tangent-space normal, A = roughness), sampled triplanar and reoriented into world space with
  the **Whiteout blend** (no per-vertex tangents needed), then weight-blended like the albedo. The
  blended normal drives the gbuffer normal and the blended roughness drives `metRough.y`. A layer with
  no map is bound a **1x1 flat-normal / full-roughness default** (`128,128,255,255`): the whiteout
  blend collapses it back to the exact geometric normal + roughness 1, so unauthored terrain is
  byte-identical to before and the default texels are cache-resident (no bandwidth). Cost is opt-in —
  measured ~0% fps delta with no maps, ~13% on the terrain pass when every layer has a real map.

**Painting the splat (Map Painter layer 6 "Splat (Terrain)")** — paints which of the 4 layers
textures the surface. The painter stores a discrete layer index per pixel (0 = unpainted → auto,
1..4 = grass/rock/sand/snow); Save expands it to a one-hot RGBA PNG. The terrain sampler is linear,
so adjacent one-hot texels blend into soft seams for free, and a zero texel keeps the auto selection.
Strokes apply **LIVE** through `TerrainWorld::UpdateSplatMap` — it re-uploads the GPU splat texture in
place (`CopyDataToImageStaged`, or a full recreate + `SetTerrainSplatView` on the first paint / a
resize) with **no rebuild or re-mesh** (the tile mesh already carries the splat uv). The painter's
pixel→texel mapping is 1:1 with the shader's uv: the painter's terrain-flip (`col 0 = +X`,
`row 0 = +Z`) exactly cancels the mesher's `uv = 0.5 − (worldXZ − boundsCenter)/sizeMeters` flip, so
the CPU buffer uploads unrotated. Live upload needs the textured pipeline active (`Material::terrain`,
i.e. `BuildTerrainTextures` succeeded); otherwise the painter falls back to save + rebuild. Editor
actions: `voxelpainter.layer {layer:6}` + `voxelpainter.stroke {u,v,radius, brush:<1=grass 2=rock
3=sand 4=snow, 0 erases to auto>}` + `voxelpainter.save`. Size the splat map like the heightmap so the
preview underlay and Alt+LMB camera teleport line up.

## Painted mesh scatter (trees, rocks, grass, props)

A grayscale **scatter map** (`scatterPath`, same extent/orientation rules as the caves map) whose
pixel value is a 1-based index into `scatterMeshes` on the terrain tag: builtin low-poly templates
(`"tree"`, `"rock"`, `"grass"` — `Terrain/ScatterTemplates.*`) or any model asset path (baked at
Create: node transforms + material base colour folded into vertices, capped at 2500 verts —
low-poly props only, they are duplicated per instance).

Instances are **baked into the ring-0 tile geometry at mesh time** — the engine has no GPU
instancing and per-instance nodes would be thousands of draws, so props ride everything the tiles
already have: streamed in-place updates, Hi-Z/GPU cull, the meshopt LOD chain, shadows, and the
per-tile Jolt collider. Placement is deterministic per map pixel (anchor = texel centre, yaw/scale
from a pixel hash, snapped to the TRUE surface by a density march — sculpt/cave aware; skipped when
carved away or underwater), so re-meshed tiles always regenerate identical props. Non-colliding
kinds (grass) are appended after colliding ones and excluded from the collider via a lod0 prefix
(`Tile::collideIndices`). The measured worst-tile vertex demand joins the ring-0 budget at load;
a per-tile cap (8192 scatter verts) keeps dense paint from grow-looping the ring.

Painting routes:
- **Map Painter layer 5 "Scatter (Terrain)"** — jittered-grid stamps like the voxel Features layer
  (idempotent: dragging never densifies), kind combo from `scatterMeshes` + Erase. Strokes apply
  LIVE through `TerrainWorld::UpdateScatterMap` (touched tiles re-mesh + re-cook, no rebuild); Save
  just persists the PNG.
- **Viewport Terrain Brush** in Scatter mode (below) plants/erases directly on the 3D terrain.
- **Editor actions**: `voxelpainter.layer {layer:5}` + `voxelpainter.stroke {u,v,radius, brush:
  <kind id 1..N, 0 erases>}` + `voxelpainter.save`.
- Coarse rings never carry props (like sculpts/overhangs — pop at the fine rim is the accepted
  trade).

## Viewport Terrain Brush

`TerrainBrush` (editor widget, Windows menu) sculpts in the Scene view: SceneView feeds it the
mouse each frame; it raycasts the terrain density under the cursor, draws a projected ring decal
(ImDrawList polyline — the engine has no 3D decal API), and applies strokes as deferred ops
(`QueueSculpt`/`QueueLevel`, the safe mid-frame entry). While armed and over terrain it owns the
left button (object picking is skipped). Modes: Raise/Dig (CSG spheres at the 3D hit — digging a
cliff face undercuts it; Shift inverts), Smooth (level toward the average of 5 down-ray taps),
Flatten (level toward the stroke-start height), Scatter (routes through the Map Painter's scatter
layer so the map stays the single truth). Stamps are spaced at 0.45·radius along a held stroke.

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
`overhangs`, `collisionRadiusM`, `scatterPath`, `scatterMeshes`, `splatPath`, `layerPaths`, Lua
`node:set_terrain{streaming=, overhangs=, collision_radius_m=, scatter_map=, scatter_meshes={...},
splat_map=, layers={...}}`). `streaming`/`overhangs`/scatter/splat/layer config are structural
(rebuild, debounced); collision radius and physics knobs are live (and scatter PAINT is live — only
the config strings rebuild). The auto-splat height selection normalizes to the CONFIGURED height
range so it never shifts while streaming.

## Known limits / follow-ups

- Meshing is main-thread, budgeted (~3 tiles/frame; overhang tiles cost ~5–10 ms each) — worker
  threads are the next step if streaming while walking ever stutters.
- RT/BLAS does not track in-place tile updates (stale RT terrain until some full rebuild).
- Coarse rings ignore sculpts/overhangs/scatter; grow leaks the old range until the next scene load.
- Scatter props share the terrain material (baked vertex colours, no textures — the shader passes
  their `color.a < 0.5` verts straight through) — textured props need per-material instanced draws, a
  render-side feature that does not exist yet.
- Triplanar splatting supports albedo + per-layer normal/roughness maps (above), but metal is fixed 0
  and there is no per-layer AO or height/parallax. `kTexScale` (3 m/tile) is a shader constant — a
  configurable per-terrain or per-layer texture scale is a follow-up.
- Splat painting (Map Painter layer 6, above) is one-hot per pixel — you pick which single layer wins
  a pixel, and the linear sampler softens the seams. True multi-weight brushes (soft per-channel
  blends authored directly) would need a 4-channel painter buffer; not built (the single-channel index
  buffer reuses all the existing painter plumbing). Live upload re-uploads the whole texture per stroke
  (fine for ≤2048² maps); a dirty-rect sub-region copy is the upgrade if huge maps ever hitch.
- A missing or broken `terrain_gbuffer.passinfo` / `TerrainGBufferPS.hlsl` leaves terrain routed to
  cull bucket 8 but never drawn (`PE_WARN` only) — mirrors the voxel pipeline's identical gap when
  `voxel_gbuffer.passinfo` fails. Texture-load failure is the only runtime fallback (vertex-colour
  bands via the standard pipeline).
