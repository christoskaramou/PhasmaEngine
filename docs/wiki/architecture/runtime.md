# Runtime and project boundary

PhasmaRuntime is the shared runtime layer between PhasmaCore and the host products. It owns project/runtime contracts that should be identical for the editor and a standalone player. It should not own editor UI, hot-reload UI, launcher UI, ImGui panels, or module-reload mechanics.
Migrated 2026-09-20. Open the relevant focused page; verify current behavior in its source before editing. Detailed contracts are preserved, not newly runtime-tested.

## Focused pages

- [Layer Shape](runtime/layer-shape.md)
- [Lean And Android Build Shape](runtime/lean-and-android-build-shape.md)
- [Runtime UI](runtime/runtime-ui.md)
- [Runtime UI Helper Surface](runtime/runtime-ui-helper-surface.md)
- [Runtime 2D Physics And Shapes](runtime/runtime-2d-physics-and-shapes.md)
- [Runtime 2D Skinned Procedural Animation](runtime/runtime-2d-skinned-procedural-animation.md)
- [Runtime Particle Helper Surface](runtime/runtime-particle-helper-surface.md)
- [MyProject Contract](runtime/myproject-contract.md)
- [Game export](runtime/game-export.md)
- [Engine, editor, and project assets](runtime/engine-editor-and-project-assets.md)
- [Scene Prefabs](runtime/scene-prefabs.md)
- [Scene Scripts](runtime/scene-scripts.md)
- [First Implementation Slice](runtime/first-implementation-slice.md)
