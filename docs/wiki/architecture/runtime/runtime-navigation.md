# Runtime Navigation

Navigation meshes run on Recast/Detour behind `PE_NAV` ([NavMesh.h](../../../../Phasma/Runtime/Code/Navigation/NavMesh.h), [NavigationSystem.h](../../../../Phasma/Runtime/Code/Systems/NavigationSystem.h)). Phase 1 (bake and queries) and phase 2 (agents and the swarm) were implemented 2026-10-06; moving a game's own seeded crowd kernel onto it is phase 3. Initial platform verification covered Windows, Linux (GCC 13) and Android (arm64 build). The boundary and mixed-mover hardening below was verified 2026-10-06 against the actual Windows Release and Linux GCC navigation sources, including `PE_NAV=OFF` on both. The Android arm64 Release native build also passes.

## Dependency

- recastnavigation `v1.6.0` comes in through FetchContent in the root `CMakeLists.txt`, static, with its demo, tests and examples off and warnings silenced.
- **Pitfall: v1.6.0 asks for CMake 3.1, which CMake 4 refuses.** The block sets `CMAKE_POLICY_VERSION_MINIMUM 3.5` around `FetchContent_MakeAvailable` only.
- `Recast`, `Detour` and `DetourCrowd` link PRIVATE into PhasmaRuntime. No public header names a Recast or Detour type: `NavMesh`, `NavCrowd` and `NavSwarm` keep them behind pimpls, and the shared Detour state lives in the module-private `Navigation/NavDetail.h`. So the editor module needs neither the define nor the link.
- With `PE_NAV` off, `NavMesh` compiles to stubs: `Build` fails with "PE_NAV is off" and every query returns nothing.

## Bake

- **Source geometry:** the active scene's static, non-trigger physics bodies, the same set the collider gizmos draw. The bake reads each node's `PhysicsBodyDesc` and computes its world matrix from current local matrices up the parent chain, not Jolt (Jolt holds no node bodies outside play mode). It works in edit mode and immediately after a script moves/scales a collider or its parent, without waiting a frame for render transforms.
  - Box, Sphere and Capsule are tessellated at their `ScaledColliderSize`, at the node origin with its rotation, like Jolt.
  - Mesh and ConvexHull use the node's LOD0 triangles placed by its world matrix. Ponytail: a ConvexHull bakes as its mesh, not its hull.
  - Not gathered yet: terrain tiles (`TerrainWorld::Tile` keeps no geometry outside Jolt) and play-only trigger-zone colliders.
- **Winding decides walkability.** Recast marks a triangle walkable when `cross(v1 - v0, v2 - v0)` points up within the max slope. Tessellated shapes are wound away from their centre (`Soup::Outward`); mesh colliders keep their authored winding (glTF counter-clockwise is outward).
- **Y up, metres.** Recast assumes it, and so does the engine. Feeding Z-up data bakes walls as floors.
- **One solo tile, one agent size, in memory.** A bake replaces the previous mesh. `NavigationSystem` remembers the scene and its `GetGeneration()`, so a scene load or new scene drops the mesh (queries return nothing until the next bake). Detour's single-tile limit is 65535 vertices; past it the bake fails with "raise the cell size", it does not crash.
- Recast's build log goes to the engine log as `[Nav]` warnings and errors.
- **Input boundaries** ([NavMesh.cpp](../../../../Phasma/Runtime/Code/Navigation/NavMesh.cpp), [NavDetail.h](../../../../Phasma/Runtime/Code/Navigation/NavDetail.h)): bake settings must be finite, with positive cell sizes/agent height, nonnegative radius/climb and slope in `[0, 90)`. Voxel height/climb must fit Recast's span range and erosion radius is at most 127 cells. Dimensions and world/query coordinates are limited to 1,000,000 m; geometry must contain finite xyz triples in that range and complete, in-range triangle indices. Mover speed is limited to 10,000 m/s, crowd acceleration to 1,000,000 m/s², and swarm stop distance to 1,000,000 m, so finite-but-extreme inputs cannot overflow float arithmetic. A solo tile is capped at 16M horizontal cells and Recast's vertical span range; larger bakes fail with a cell-size/height error before allocating. Allocation and query failures are checked. Invalid bake inputs return `nil, error`; Recast log errors warn instead of raising an engine fatal error.
- Ponytail ceilings, each with its upgrade path: tiled meshes (large worlds, `DetourTileCache` dynamic obstacles), several agent sizes (one mesh per size), a saved bake (no sidecar file format exists yet; bake on load is milliseconds for arena-sized scenes), automatic bake at play start (call `nav.bake()` from `on_play` instead).

## Settings

- Per scene, in `SceneSettings`, saved in the `.pescene` `settings` block: `nav_agent_radius` (0.4 m), `nav_agent_height` (1.8 m), `nav_agent_climb` (0.4 m), `nav_agent_slope` (45 degrees), `nav_cell_size` (0.2 m), `nav_cell_height` (0.1 m). An absent key loads as the default, not the previous scene's value (the `physics_rate` rule).
- Lua: `settings.set("nav_agent_radius", 0.6)` and friends.

## Lua

`nav` module ([NavBindings.cpp](../../../../Phasma/Runtime/Code/Script/Bindings/Nav/NavBindings.cpp)); positions are separate numbers, like `physics.raycast`.

- `nav.bake([settings])` returns `{polygons, vertices, ms}`, or `nil, error`. The optional table overrides the scene settings for this bake: `{radius, height, climb, slope, cell_size, cell_height}`.
- `nav.is_ready()`: a mesh exists for the active scene.
- `nav.find_path(ax, ay, az, bx, by, bz)` returns `{points = {{x, y, z}, ...}, complete}` or `nil` when either end is off the mesh. `complete` is false when the end is unreachable; the path then stops at the reachable point nearest to it. Query points may lie up to 2 m beside and 4 m above or below the mesh.
- `nav.raycast(ax, ay, az, bx, by, bz)` walks the mesh: the point where an edge stops it (its height taken from the mesh), `false` when the way is clear, or `nil` when the start is off the mesh or an end is invalid.
- `nav.nearest(x, y, z)`: the mesh point nearest to it, or `nil`.
- Agents (play only, after a bake): `nav.add_agent(node, [{radius, height, speed, accel}])` returns true when the node stood on the mesh; `nav.set_target(node, x, y, z)` (false off the mesh); `nav.stop(node)`.
- Swarm (play only, after a bake): `nav.swarm_add(node, [{radius, speed, stop}])`, `nav.swarm_target(x, y, z)`, `nav.is_anchored(node)`, `nav.swarm_stats()` returning `{members, steps, ms}` (the last frame's fixed steps and their mean cost).
- Both: `nav.remove(node)`, `nav.get_velocity(node)` returning `{x, y, z}` or `nil`.

## Editor

- Scene Settings > Navigation: the six bake settings, a Bake button with the result (polygons and milliseconds, or the error), and a Show NavMesh toggle.
- Gizmos > NavMesh (agent action `gizmo.navmesh`) outlines every polygon in blue, 3 cm above its surface. The edges are cached at bake. Like the collider gizmos it is screen-space and not depth tested. A bake from the button turns it on.
- Flat tops of obstacles bake as small separate islands. That is Recast's normal output; no path reaches them.

## Tests and costs

Pool test `editor-nav-agents` sends four agents across a wall's line on swapped lanes: they arrive within 0.6 m, never enter the wall, and stay at least 0.99x their radii's sum apart (verified closest 0.81 m against 0.8, 2026-10-06). The 1% allowance is numerical tolerance, rather than the initial test's 20% overlap allowance.

Pool test `editor-nav-swarm` sets 150 members (radius 0.3, stop 1.5) on a point past three blocks: no pair under 0.99x the radii's sum, none inside a block, 90% within 6 m (150 bodies packed behind the stop line fill about that), at least 8 anchored. Verified 2026-10-06: closest 0.61 m against 0.6, all 150 within 6 m, 9 anchored, mean step 0.149 ms.

Pool test `nav-native-edges` compiles the actual navigation sources against the configured Windows/Linux Release Recast libraries, without an editor or GPU. It covers invalid bake geometry/settings, invalid mover parameters/indices, non-finite deltas, an empty swarm, a first target off the mesh, a ramp to a target directly upstairs, and moving/stopped crowd agents crossing a swarm member's path with at least 0.99x their summed radii separation. Reintroducing the old target-field initialization condition causes an access violation in this test. `nav-disabled` compiles and exercises all three `PE_NAV=OFF` implementations without Recast linkage.

Standalone Windows Release comparison, 2026-10-06, identical 600-step runs after 240 warm-up steps: 150 members 0.119 -> 0.130 ms/step; 1000 members 1.240 -> 1.294 ms/step. These timings are separate from the editor measurements below.

Saved Sponza Release verification, 2026-10-06: screenshot before the changes, immediate present mode, 10 profiler snapshots one second apart in each fixed baseline/current capture, and `tools/compare_snapshots.py` reported no regression. Frame time 0.517 -> 0.434 ms, FPS 2018 -> 2292, application VRAM unchanged at 885 MB. GPU timestamps were unavailable; this checks CPU/frame timings and VRAM, and does not establish GPU pass timings.

Initial measurements, 2026-10-06, Windows Release editor: the swarm step costs 0.17 to 0.21 ms for 150 members and 1.5 ms (worst 2.0) for 1000, once per 60 Hz step. The 1000-member pack settles into an even lattice around a clear ring at the stop line. Android cost is unmeasured (no device attached); expect several times the desktop figure.

Platforms, 2026-10-06: the Android arm64 player (`gradlew :app:externalNativeBuildRelease`, CMake 3.22, NDK 28) and the Linux editor and player (GCC 13, a WSL clone: a Windows checkout cannot build Linux because git stores `libshaderc_shared.so` as a symlink and the Windows tree has `core.symlinks=false`) build with no warning from the navigation sources. A standalone Linux run of the navigation sources (bake, path, raycast, swarm with determinism, crowd) passes.

Pool test `editor-nav-path` builds a static floor and a wall across it from Lua, bakes in edit mode, and checks that:
- the bake returns polygons;
- a path from one side of the wall to the other is complete, has corners, and is over a metre longer than the straight line, with no sample on the wall's footprint;
- a straight walk through the wall stops at it (wall half-width plus agent radius), and one alongside it is clear;
- a point above the floor snaps onto it.
- zero cell size is rejected with an error, and a wall moved immediately before baking is gathered at its new position in the same Lua callback.

## Movers (play only)

`NavigationSystem` moves two kinds of node during play, on the baked mesh:
- **Agents** ([NavCrowd.h](../../../../Phasma/Runtime/Code/Navigation/NavCrowd.h)), on Detour's crowd module: each walks its own path to its own target. When no swarm coexists, they steer around each other by sampling velocities ahead (`DT_CROWD_OBSTACLE_AVOIDANCE` with Detour's default sampling; `dtCrowd::init` gives all its preset slots the same parameters); `DT_CROWD_SEPARATION` is off, so there is no push. Detour still resolves bodies that start inside each other. Up to 256 agents of radius 2 m or less (Detour fixes the largest radius at creation). Radius/height must be positive and no larger than the baked agent; speed/acceleration must be nonnegative, and every parameter finite. Invalid parameters and out-of-range agent indices fail safely. Bake for the widest/tallest mover. An isolated crowd steps on the frame delta (capped at 250 ms per update; excess hitch time is dropped), so it repeats exactly only for the same sequence of deltas (two initial runs were bit-identical on Linux).
- **The swarm** ([NavSwarm.h](../../../../Phasma/Runtime/Code/Navigation/NavSwarm.h)): one per scene, every member chasing one target. Built for hordes (hundreds to a thousand):
  - a flow field over the mesh's polygons (Dijkstra from the target's polygon, recomputed only when the target changes polygon) gives each member the next polygon edge to aim for, at its nearest point kept a radius from the edge's ends, looking one polygon ahead near an edge; a staggered raycast (every 4th step per member) sends it straight at the target when the mesh allows and the ray ends on the target's floor;
  - ORCA (reciprocal velocity obstacles) between the 10 nearest neighbours on the same floor picks each member's velocity, avoiding as if 10% wider: packed tight, the true radii left a few centimetres of overlap (three-way squeezes, wall clamps), and the margin absorbs it without any push;
  - members within their 3D stop distance of a directly reachable target are anchored: they hold still and the others take the whole avoidance, so the back ranks flow around the front. A target directly above another floor cannot anchor that floor's members. Indirect routes use distance to the next portal for step limiting, so sharing the target's X/Z does not stall an upstairs route. A held body directly ahead biases preferred velocity tangentially; local avoidance cannot guarantee passage through a fully blocked crowd;
  - members move with `moveAlongSurface`, so they never leave the mesh;
  - deterministic for one sequence of calls and independent of frame rate: fixed 60 Hz steps, members in index order, last-step velocities read. A Linux run of the same scenario twice is bit-identical. Ponytail: at most 4 steps run per frame and the rest of a long frame is dropped, so below 15 fps the swarm runs slower than real time.
- Swarm radius must be positive and no larger than the bake radius; speed/stop distance must be nonnegative, and all parameters/targets finite. Invalid inputs are rejected or leave the previous target unchanged. A first off-mesh target initializes an empty-reachability field safely and members head straight toward it along the surface. Empty updates return zero and drop accumulated time; non-finite deltas are ignored.
- **Mixed movers** ([NavSwarm.cpp](../../../../Phasma/Runtime/Code/Navigation/NavSwarm.cpp), [NavigationSystem.cpp](../../../../Phasma/Runtime/Code/Systems/NavigationSystem.cpp)): when a crowd and swarm coexist, Detour computes agents' path-following preferred velocities each 60 Hz step and both groups share one ORCA pass. Corrected crowd positions are written back into Detour's path corridors. Stopped/zero-speed movers hold still and neighbours take the full avoidance. Direct C++ consumers call `swarm.Update(dt, &crowd)` instead of also stepping the crowd separately; both must refer to the same baked mesh. Swarm stats count swarm members and include the mixed step's cost. Crowd acceleration shapes its preferred velocity; shared collision avoidance can correct that velocity immediately.
- **Lifecycle.** Movers exist while the script play flag is on (`IsScriptPlayMode`, set before the scripts' init in both the editor and the Player, so `on_play` can bake and add movers); `RuntimePlaySession` calls `StopPlay` at stop (every mover dropped before the scene is restored) and `SetPaused`. A successful bake drops every mover (their polygon references die with the old mesh); a failed bake keeps the current mesh and movers. A deleted node leaves its crowd or swarm on the next update.
- **Time and order.** Movers step on the frame delta times `time_scale` (gameplay slow motion slows them, like animation and scripts; Jolt ignores `time_scale`). `UpdateGlobalSystems` runs systems from an `unordered_map`, so movers may update before or after physics: give a mover's node no dynamic body.
- **Known limits (2026-10-06).** Swarm positions change only on fixed 60 Hz steps (no interpolation), so above 60 fps members move on some frames and not others. Per-node queries (`nav.get_velocity`, `nav.is_anchored`) scan the mover list, so calling them for every member each frame is quadratic in the swarm size. A pair of members can still come about 2% inside their radii at moments (the pool's 0.99 spacing check saw 0.59 m for 0.6 m on 2026-10-06). A target snapped onto a polygon's +x or +z edge (standing within the agent radius of a wall on that side) never counts as directly reached, because Detour's `getPolyHeight` treats those edges as outside: no member anchors and all drive at the target point. Testing `closestPointOnPoly` instead fixes that case in isolation, but in the 150-member pool scenario only 132-133 members then arrived within 6 m (150 before), so the change waits for phase 3 tuning.
- **Placement.** Each frame the mover's world position is written into its node's local matrix through the parent's world matrix (computed from local matrices, current this frame). Movers do not turn their nodes; scripts read `nav.get_velocity`.

## Phase 3 notes

- A game that runs its own seeded crowd kernel (own grid flow field and obstacle data) moves onto `NavSwarm` by feeding its arena geometry to `NavMesh::Build` directly and driving one `NavSwarm`, so its seeded checks stay deterministic.
- An open-ground jam seen in game use (a pack stalled near a standing target) is still unreproduced; phase 3 needs a real repro before claiming the swarm fixes it.
