# Engineering decisions and pitfalls

Reviewed 2026-09-20 during migration from historical memory. These are navigation pointers and selected durable decisions, not a replacement for current source or project instructions.

## Gameplay scripting and native extension boundary

Lua remains the default gameplay scripting path. September 15 evaluations rejected adding Mojo or another VM merely for speed; prefer measured native extracts and batch APIs. This records the decision, not a promise that language capabilities never change.

Verified 2026-09-21: C++ scripts use a separate live-reloadable game module on desktop and a statically linked game module in the Android player, with the same narrow, versioned native API. Android validates descriptors at startup without live reload. PhasmaRuntime stays static; scripts include the public scripting header rather than engine internals. A desktop candidate is validated before old instances are destroyed, and private script state resets when the module is replaced. See the [ProjectNative contract](../../../Phasma/ProjectNative/README.md), [loader](../../../Phasma/Runtime/Code/Script/ProjectNativeHooks.cpp), and [native lifecycle](../../../Phasma/Runtime/Code/Script/CppScript.cpp). Committed 2026-09-26 (`294b3e17`); follow-up review hardening (append-only ABI v5, filename-only references, fault containment) is described in [scene scripts](../architecture/runtime/scene-scripts.md). Not evidence of a shipped release.

Historical provenance: archive drawer suffixes `2589b3e19a2c0b40`, `2fac0680fa6e7861`, `92404618a573ffe4`.

## Animator is a separate host

The September 3 user decision is that PhasmaAnimator is its own program, not an editor mode or an editor copy with hidden panels. Consult [Animator architecture](../architecture/animator.md) and [its build target](../../../Phasma/Animator/CMakeLists.txt). Preserve shared runtime utilities without moving editor-specific behavior into the runtime.

Historical provenance: archive drawer suffix `0b4f1858f090588f`.

## Editor control labels go above the control

User decision, 2026-10-05: in PhasmaEditor, a control's label sits on its own line above it, never to its right as in ImGui's default. Checkboxes, radio buttons and buttons keep their text beside them. Verified 2026-10-05.

- **List-style controls:** wrap the label with `ui::LabelAbove("Label##id")` ([Helpers.h](../../../Phasma/Editor/Code/GUI/Helpers.h)).
  - It keeps a width set by `SetNextItemWidth`; without one, the control spans the full width.
  - In auto-sizing windows (popups) it keeps the default width instead, because full width would collapse the field.
  - A hidden `##id` label passes through unchanged.
- **Labelled fields side by side in a `SameLine` row:** use the scoped `ui::LabelAboveInRow`, so each label stays over its own field.
- **One-line toolbars** whose other items have no labels, such as the File Browser's Sort combo and the Sprite Editor's atlas Zoom: put the text before the control instead.
- **PhasmaAnimator:** it has not been converted yet.

## Do not revive obsolete agent architecture

Old imported PhasmaAgent plans and quick-start examples described an obsolete/aspirational component. They are not evidence of a usable current PhasmaAgent library. Use [agent tooling](../architecture/agent-tooling.md) and the current MCP implementation. A historical correction was retained in the archive rather than importing old plans into this wiki.

## GPU resource reuse and parallel writes

Joint-palette workers write disjoint mapped ranges and join before submission; frame resources must be fenced before reuse. The migration checked the matching implementation comment and worker setup in [Scene.cpp](../../../Phasma/Runtime/Code/Scene/Scene.cpp), around `Scene::UpdateUniformData`. See [rendering](../architecture/rendering.md) for the explanation. This was source inspection, not a new runtime concurrency test.

## Historical facts are not current state

Past branch names, benchmark numbers, build status and next-task suggestions require fresh verification. Existing engine workflow rules remain in [AGENTS.md](../../../AGENTS.md). Do not replace those rules with archived memory directives.
