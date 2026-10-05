# Runtime 3D Physics

3D bodies run on Jolt behind `PE_PHYSICS` ([PhysicsSystem.h](../../../../Phasma/Runtime/Code/Systems/PhysicsSystem.h)). 2D physics is a separate Box2D system, see [Runtime 2D Physics And Shapes](runtime-2d-physics-and-shapes.md). Verified 2026-10-05.

## Fixed step

- The step rate is the scene setting `physics_rate` (Hz). Range 10–240, default 30. It is saved in the `.pescene` global settings. A scene without the key loads as 30 Hz, so it doesn't inherit the previously loaded scene's rate.
- To change it: the editor's Scene Settings "Physics Rate" slider, or Lua `settings.set("physics_rate", n)`. The value is read every frame, so it applies live.
- A long frame catches up at most 2/30 s of physics (`MAX_CATCHUP_SECONDS`), the same budget as the old 30 Hz × 2-step cap. The step count scales with the rate, so a high rate never plays in slow motion; the rest of a hitch is dropped.
- `time_scale` does not slow Jolt. Only animation, particles and script `dt` follow it.

Pool test `editor-physics-rate` checks the rate is applied.

- Jolt adds g·dt and then damps (`v *= 1 - 0.05·dt`) on every step.
- So a falling body's speed only takes exact values that depend on the rate.
- The test asserts the 120 Hz values and rejects the 30 Hz ones.

## Layers

**Data and encoding**
- There are 32 named layers per scene, stored in `SceneSettings`:
  - `physics_layer_names` gives each layer a name. An empty name means the layer is unused; layer 0 is "Default".
  - `physics_layer_ignore` holds one bitmask per layer. Bit j of `ignore[i]` set means layers i and j don't collide. All zero, the default, means everything collides.
- A scene without these keys resets to just "Default", like `physics_rate`. Ponytail: the table is per scene, while prefabs store layer indices, so the names can differ between scenes. Moving it into project settings later would be a relocation, not a format change.
- Each body has a `PhysicsBodyDesc::layer` field, saved as `"layer"` in the node's `physics` block. All four load paths read that block through one function, `ReadPhysicsBodyDesc` in [SceneSerializer.cpp](../../../../Phasma/Runtime/Code/Scene/SceneSerializer.cpp). It clamps the layer to 0..31 and keeps the default for any field of the wrong type.
- A Jolt object layer is `layer * 2 + moving` (`Layers::ToObjectLayer`). The broadphase keeps its static/moving split, and static bodies still never pair with each other.
- `ObjectLayerPairFilter` reads a copy of the ignore table, taken before every step, so matrix edits apply live. Either direction's bit blocks the pair.

**Pitfall: the matrix also gates triggers.** A trigger on a layer that ignores the player's layer reports no overlap. This is separate from a zone's `physicsFilterTag`, which filters by string tag after the overlap happens.

**Editor**
- Scene Settings has a "Physics Layers" table:
  - each row has an editable name;
  - the columns show the names as angled headers;
  - a triangle of checkboxes covers each pair once;
  - "Add Layer" names the next free slot.
- Each layer except Default has a remove (−) button. Removing a layer:
  - clears its name and its collision bits;
  - moves bodies on it in the open scene to Default.

  So a later "Add Layer" that reuses the slot starts clean. Prefab files on disk keep their stored index.
- Clearing the name field does not remove a layer. A cleared name would drop the row mid-edit, so the field never commits an empty name.
- The Physics component has a Layer dropdown, which calls `PhysicsSystem::SetBodyLayer`.

**Lua.** Wherever a layer is expected, a number is an index and a string is a name. An unknown layer warns and changes nothing.
- `physics.add_body(n, type, shape, {layer = "Enemy"})`
- `physics.set_layer(n, layer)` moves a body to another layer, live while in play.
- `physics.get_layer(n)` returns the index.
- `physics.set_layers_collide(a, b, bool)` edits the same bits as the editor table.
- `physics.raycast(ox, oy, oz, dx, dy, dz, maxDist, layers)` takes one layer or a table of them. It returns nil when nothing is hit, or when a layer is unknown.
- In C++, `PhysicsSystem::Raycast` takes a `layerMask` (bit i = layer i).

**Test.** Pool test `editor-physics-layers` drops balls onto a layer-1 floor in play mode. It checks that:
- the Default ball rests on the floor, and a layer-2 ball that ignores layer 1 falls through;
- a resting ball falls through after `set_layer` mid-play;
- a downward raycast hits the floor with `layers=1` and misses with `{"Default"}`.

Both physics pool tests launch the editor through `.testpool/tests/pe_editor.py`. It waits for the startup scene to finish loading before opening a new scene; a late startup load replaces the new scene. It also restores `phasma_settings.json` (`startup_scene`), which `file.new_scene` rewrites.

## Joints

**Data**
- A body has at most one joint, `PhysicsBodyDesc::joint` (`PhysicsJointDesc` in [PhysicsTypes.h](../../../../Phasma/Runtime/Code/Physics/PhysicsTypes.h)). It's saved in the node's `physics` block as `"joint"`, only when a joint exists, and read by `ReadPhysicsBodyDesc`.
- Fields:
  - type: Fixed, Hinge, Distance or Slider;
  - `connectedNode`: a node name; empty means the world;
  - `anchor` and `axis`, in the node's local space;
  - `connectedAnchor`: the far end of a Distance joint, local to the connected node, or a world point when joined to the world;
  - limits, a motor, and `breakForce`.
- Ponytail: one joint per body. A chain is one joint per link.

**Lifetime**
- `PhysicsSystem` creates the Jolt constraints in `StartSimulation`, after every body is in the world. Mid-play it creates them right away: `AddBody`, `SetJoint`, Lua `add_joint`.
- If the connected body isn't in the world, or neither body is dynamic, it warns and makes no joint. Nothing is queued for later.
- The live joints are tracked in `m_joints`, each holding one reference to its constraint. `StopSimulation` removes them all in one pass, and `DestroyJoltBody` removes any joint touching the body being destroyed.

**Sign convention**
- The owner is Jolt body 2, because Jolt measures body 2 relative to body 1. So:
  - slider position grows along +axis;
  - hinge angle and motor are right-handed about the axis, as the owner sees it;
  - both are measured from the pose when the joint is created.
- Pitfall, verified 2026-10-05: with the owner as body 1 the signs flip. A box on a downward-limited slider then refuses to fall.

**Limits and clamping.** Jolt asserts only in Debug, so bad values are clamped before they reach it:
- hinge limits to [-180, 0] / [0, 180] degrees;
- slider limits to min ≤ 0 ≤ max;
- motor force to ≥ 0;
- a zero axis falls back to +Y, with a warning.

A Distance joint without limits keeps its starting length (a rigid rod). With limits it allows a length range, like a rope.

**Breaking**
- After each frame's steps, a joint whose last-step positional impulse ÷ step exceeds `breakForce` (N) is removed. Its Lua callback runs after the loop, so a callback may add or remove joints.
- The first step after a joint is created is skipped, because it can carry a settling impulse.
- Break callbacks are cleared with the trigger callbacks when Play stops.

**Ponytail: the connected body is matched by name** (`FindNodeByName`, first match). Two instances of a jointed prefab therefore both connect to the first one's bodies. Upgrade path: prefer a match inside the owner's own subtree.

**Editor**
- The Physics component has a Joint section:
  - type;
  - Connected Body, listing the other physics nodes plus (World);
  - anchor and axis;
  - a range for the limits;
  - motor speed and maximum force;
  - break force.
- Edits apply when Play starts.
- With colliders shown, the viewport draws joints in orange: a ring on the pivot, the hinge or slider axis through it, and a line to a Distance joint's other end.

**Lua**
- `physics.add_joint(node, "fixed" | "hinge" | "distance" | "slider", {...})` takes:
  - `connected`: a node or a name;
  - `anchor`, `axis`, `connected_anchor` (vec3);
  - `limits = {min, max}`;
  - `motor_speed`, `motor_max_force`, `break_force`.
- `physics.remove_joint(node)`.
- `physics.set_joint_motor(node, speed | false[, max_force])`.
- `physics.get_joint_value(node)` returns the hinge angle in degrees, the slider position in m, or the distance length in m.
- `physics.on_joint_break(node, fn)`.

**Test.** Pool test `editor-physics-joints` joints five boxes to the world and plays. It checks that:
- Fixed holds;
- the Hinge motor turns at 90°/s;
- the Slider stops at its −1 m limit;
- Distance swings on a 2 m rod;
- a 1 N break force breaks and calls back.

## Collider shapes

- `ScaledColliderSize` ([PhysicsTypes.h](../../../../Phasma/Runtime/Code/Physics/PhysicsTypes.h)) is the one formula for Box, Sphere and Capsule sizes after world scale. Jolt shape creation and the editor overlay both use it.
- **The collider sits at the node origin, not the mesh centre.** Auto-fit takes only the size of the mesh bounds. A mesh authored with its origin at its feet gets a box centred on its feet.
- **Capsule auto-fit is wrong under non-uniform scale.** The cylinder half height is computed from the unscaled bounds (`size.y/2 - radius`, floored to 0.06), and then only that part is scaled by Y. A 2 m cylinder scaled to 4 m tall gets a capsule 2.24 m tall. Uniform scale is correct.

## Editor overlay

- SceneView `DrawVolumeGizmos` draws physics colliders as wireframes over the viewport.
  - Solid bodies are green, triggers are cyan.
  - The selected object is drawn brighter and thicker.
- The selected object's collider always shows. Gizmos → Colliders (agent action `gizmo.colliders`) shows every collider.
- ConvexHull and Mesh shapes are not drawn, because the mesh itself shows their extent.
- Trigger zones are drawn yellow by the same function.
