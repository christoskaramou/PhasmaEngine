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
