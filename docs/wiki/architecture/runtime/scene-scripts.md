# Scene Scripts

## Native C++ scripts

Verified 2026-09-21 against [CppScriptSystem](../../../../Phasma/Runtime/Code/Script/CppScript.cpp),
the [module loader](../../../../Phasma/Runtime/Code/Script/ProjectNativeHooks.cpp), and the
[public scripting header](../../../../Phasma/ProjectNative/Api/ProjectNative.h).
Opt-in desktop C++ scripting uses a separate game DLL with a versioned function-table API.
PhasmaRuntime remains static and project code has no engine-header or engine-library dependency.
Global scripts and per-node `.cpp` source references (plus legacy `cpp:<name>`) run beside Lua; each native node attachment
owns a separate module-allocated instance. Stop destroys play-only instances before snapshot
restoration. Failed instances still receive destruction.

The editor and build-folder players (`NativeScripts.json` beside the executable) poll the built
DLL, load a per-process shadow copy, and validate the ABI and immutable script descriptors before
replacing active code; exported games load the installed module in place once.
Lua reloads never unload the module or reset C++ state. Rejection preserves the old module and state.
Acceptance destroys old instances before unloading, then recreates eligible instances against
the existing scene. Private script state resets; migration is not implemented.
Android player builds statically link the same game sources and validate descriptors at startup;
they never poll or reload a module. Gradle accepts `-PPE_PROJECT_NATIVE=ON` and optional
`-PPE_PROJECT_NATIVE_DIR=/absolute/path/to/native`. Code changes require rebuilding the APK.
The API currently covers logging, node lookup/validation, cube creation, local
position reads/writes, held-key input, and source-file metadata; ABI v4 (verified 2026-09-25) adds
prefab instantiation, subtree destroy, rotation/scale writes and recursive clip playback. Position
and rotation writes on a camera node also drive the `Camera`, because `Scene::UpdateCameras`
overwrites camera nodes from the camera every frame. Lua and native scripts share
`InputState::IsKeyDown`, including UI keyboard capture. The sample's `cpp:PlayerController`
uses WASD for normalized local X/Z movement at 4 units/second; it has no physics or touch input.
See [ProjectNative usage and contract](../../../../Phasma/ProjectNative/README.md).

Review hardening (2026-09-26, commit `59ac05eb`; standalone check and editor smoke passed on
Windows with Clang): ABI v5 is append-only (`ScriptAbiMinVersion` 5, frozen descriptor/module layouts);
Windows shadow copies get their own patched PDB; MSVC builds contain SEH faults in script
callbacks (instance disabled, state leaked); scenes and descriptors name sources by bare filename
(`PHASMA_SOURCE_NAME`, [CppScriptPath.h](../../../../Phasma/Runtime/Code/Script/CppScriptPath.h));
[NativeHandleTable](../../../../Phasma/Runtime/Code/Script/NativeHandleTable.h) prunes dead handles;
[NativeTrs](../../../../Phasma/Runtime/Code/Script/NativeTrs.h) keeps zero-scale axes finite and
preserves a reflection as a negative X scale (not the originally mirrored axis);
`engine.native_scripts_status()` and the Script Editor report whether a build's reload applied;
the sample no longer registers a global script.

Loader and status follow-up (2026-09-26, uncommitted; standalone check on Linux including an
unprivileged run, and on Windows with Clang with neither permission nor non-permission copy
checks skipped; Windows Release engine build and editor smoke, including skipped destruction
after a fault, passed with Clang): in
[ProjectNativeHooks](../../../../Phasma/Runtime/Code/Script/ProjectNativeHooks.cpp) shadow files are
tagged with the owning process id (`PhasmaGame_live_<pid>_<seq>`, `PG~<pid>.<seq>.pdb`), so hosts
sharing a build folder keep each other's files and only a dead process's files are swept. The PDB
name is short enough to fit the linker's `PhasmaGame.pdb` path; when it cannot, a warning is logged.
Only a permission refusal of the initial shadow copy falls back to an in-place load, which disables
live reload for the session; other copy errors stay failures and a running module is never replaced.
Status carries artifact identity (size and write time) and an attempt counter, so the Script Editor
matches its verdict to its own build; `live_reload` reports the effective policy (false for
exported, in-place, static and fallback loads). Validation checks unique source names with the same
separator-agnostic filename rule scenes use, and the editor smoke asserts that a faulted instance is
never destroyed, on Stop or on module unload.

Verified 2026-09-21: [ScriptEditor](../../../../Phasma/Editor/Code/GUI/Widgets/ScriptEditor.cpp)
opens native sources, creates/imports `.cpp` files in the configured native directory, and
uses the existing `RunProcess` helper asynchronously for Save & Build with compiler output.
Properties restores Edit Script for native attachments and lists source filenames.
The minus-button menu uses Remove Script for both Lua and C++; it detaches the script without deleting its source file.
Live editor verification created a new C++ file, saved/built it, and confirmed its movement
in Play; editing the existing controller also rebuilt and reloaded successfully.
Script Editor uses generation-checked node handles so saving after scene replacement cannot
reattach a file to a recycled node. Build failure output was not exercised through the UI.
`PHASMA_NODE_SCRIPT` registers one node script per file inside the module; unique source
basenames support portable scene references without requiring sources on Android.

The [standalone reload check](../../../../tools/tests/native-scripts/check.cpp) uses the production
loader and real modules to exercise replacement, invalid candidates, writable build output,
repeated reload, exception containment, and cleanup. Release editor/player/module builds and this
check passed on Windows. The [editor smoke](../../../../tools/tests/native-scripts/editor_smoke.py)
also passed live replacement, scene retention, rejected-ABI fallback, and actual editor
Play/Stop/re-Play with node scripts. These checks do not establish a rendering performance comparison.

## Lua and shared runtime behavior

`ScriptSystem::Init` explicitly opens LuaJIT's JIT library before loading gameplay. Linking the VM without this initialization leaves the compiler disabled. Startup logs whether compiled execution is available; the `jit` global is then removed, preserving the restricted script library surface. Both editor and player use this shared initialization.

Runtime profiling includes separate `Animation System` and `Audio System` CPU scopes, alongside physics, scripts, scene updates and GPU passes, so skeletal evaluation and main-thread audio maintenance can be measured independently. An instance rebuild refreshes image views, material tables and mesh constants together; `FlushPendingGpuWork` clears the texture dirty flag after that rebuild instead of repeating the same refresh through `UpdateTextures`.

Crowd scripts can use `separate_circles(input, count, output, minimumDistance, radiusFraction, strength)` for a native spatial-hash separation batch. Input contains flat `{x, z, radius, fixed, id, ...}` entries with unique numeric IDs; output receives `{dx, dz, ...}` offsets. The function visits each overlapping unordered pair once, preserves fixed-body response and deterministic coincident splits, and returns false without writing output for invalid input. Values must be finite, radii/fraction/strength nonnegative, minimum distance positive, and cell coordinates within +/-1e12. Count ignores a scratch table's stale tail. Scripts still own hero contact, displacement limits, obstacles, and final movement. ATH retains its Lua solver as a fallback and rejects distant bodies before rebuilding hero-contact shapes. Successful native separation does not also rebuild the Lua hit grid; that grid is built only for the fallback or subsequent projectile queries using updated positions. `Script Circle Separation` measures the native batch independently of those remaining rules.

ATH requests a 0.90-unit minimum separation target or 125% of the combined body radii, whichever is larger. The target is a soft response, not a guaranteed minimum distance. Its strength uses exponential relaxation across elapsed time. Applied separation is limited to 1.0 world unit/second, with new pushes building at up to 4.0 units/second squared. Released contacts, fixed bodies and obstacle stops can stop the correction immediately; stored velocity must not coast past requested clearance. Obstacle projection may stop a step but cannot amplify it or reverse its direction. Android accumulates both frames before its alternate-frame solve. Pursuit speed blends toward a continuous crowd-pressure target, with feedback limited per solve to prevent oscillation on slow frames. Pressure can brake pursuit but cannot boost it from behind. When deaths open a gap, the crowd speed multiplier recovers at no more than 2.0 world units/second squared relative to base movement speed. This replaces the 0.15-second stop/retry timer that caused visible movement bursts. Attack clocks, knockback and dashes continue.

Ordinary pursuit slows as it approaches the hero's exclusion boundary and cannot spend a long frame stepping through the hero. Separation publishes that boundary using the same body extent as hero exclusion; the last measured creep extent persists outside the near-contact query so the arrival target does not alternate between the hero radius and combined clearance. Dashes and knockback retain their separate movement. Hero exclusion also caps its relaxation at one. Validate changes with ATH's `tools/test_crowd_spacing.py` and native combat integration tests, then measure the live scene because spreading the crowd changes visible geometry and obstacle work. Settled-crowd averages alone missed aggressive transient pushes; checks now cover contact onset/release, maximum correction speed, obstacle amplification, actual melee reach and localized deaths. Death-gap regressions at 10/15/25/60 Hz check pursuit recovery and separation speed while requiring survivors to move into the opening. A separate one-step test removes a local patch, compacts and reorders survivors, and checks that distant corrections and positions are unchanged. Steering temporaries have narrow scopes to avoid LuaJIT register-coalescing failures in the enlarged separation loop.

Both native separation and ATH's Lua fallback divide accumulated corrections by the body's contact weight, floored at one: a movable neighbour contributes 0.5 and a fixed neighbour contributes 1. This retains isolated-pair response while preventing many simultaneous contacts from amplifying correction stiffness. No neighbours are discarded. The weighted response intentionally differs from the earlier summed offsets; an independent all-pairs oracle covers both spacing parameter sets, fixed/coincident bodies, scratch tails and invalid input. A pursuing packed-crowd regression checks settling and direction reversals at 10/15/25/60 Hz, alongside spacing, Android elapsed time and braking recovery.

ATH native locomotion uses separate start/stop speed thresholds (0.12/0.04 units per second) and waits for 0.2 seconds of sustained stillness before changing the base clip to its standing pose. Brief crowd stops pause the current stride at speed zero. Gait uses world displacement minus this frame's `_sep_dx/_sep_dz` crowd slide, so packed standing bodies do not `animation.play("run")` from frame 0 as a kill hole fills. Android skipped solves publish a zero slide. The settling timer still prevents run/stand chatter during short stops; it does not delay pursuit or retime the independent attack layer. The settling timer resets when a pooled rig is reused.

`scene.set_positions` and `scene.set_rotations` accept flat `{node, x, y, z, ...}` batches (rotation in degrees). `animation.set_speeds(updates, count, layer?)` accepts `{node, speed, ...}` and optionally targets the override layer. Invalid handles are skipped; count ignores stale entries. `animation.get_layer_time(node)` returns `(active, seconds)` without allocating the full `get_layer_state` table. ATH uses these surfaces for native presentation while keeping the same combat clocks. Batch transform and animation-speed CPU scopes make their costs visible.

ATH's native shovel attack computes its nine authored sweep samples once per impact and tests conservative capsule bounds before the original narrow phase. Each sample owns distinct scratch storage; nearest-first hit order, damage and independent attack clocks are preserved. Static obstacle indices can retain an obstacle-free disk around the pursuit target: circles inside it, or capsules whose endpoints both fit, need no detailed obstacle query. This keeps movement and combat updates at full rate. Valid parked creep rigs remain reusable up to each archetype's allocated high-water mark; the former 110-entry cap retained overflow GPU allocations while making them unavailable to later waves. Stale-part rigs still retire until teardown.

The September 10 fixed-roster Release Vulkan comparison (1,536 Forklings, 256 Sackwalls, 256 Slingers; 2560x1369, full render scale, shadows and SSAO enabled) reduced mean frame time from 16.72 to 15.07 ms and the 95th percentile from 27.13 to 17.50 ms. These use consecutive frame histories across ten one-second sample intervals, excluding the initial screenshot publication. The old stress schedule gradually replaced melee units with more expensive ranged rigs; both sides of this comparison use the same fixed mix. The isolated shovel check fell from 9.67 to 0.20 ms per impact with 196,608 exact hit comparisons; that is a CPU test, not a whole-frame measurement.

The September 11 4,096-enemy check on the RTX 4080 SUPER (Release Vulkan, Immediate, 2560x1369, full render scale) still has an unresolved performance regression: two runs averaged 34.59 and 35.89 ms against the fixed 33.16 ms baseline, and both snapshot comparisons failed the performance gate. An isolated pair measured 35.90 ms without separation-subtracted gait and 36.37 ms with it; removing that last animation change did not recover the baseline. Its exact contribution remains uncertain from one pair. CPU correctness checks and the final Vulkan validation run passed, including actual shovel kills; the existing BloomBF render-graph producer warning remains. The crowd fixes should not be described as a clean 4K performance result.

Native separation resolves the nine neighboring cell heads once per occupied cell, then traverses input-ordered linked indices in the circle array. This removes per-cell vector allocations and repeated neighbor hash lookups while preserving pair traversal and floating-point accumulation order. `Crowd.SeparationBodies` and `Crowd.SeparationCells` expose density. Release CPU comparisons with identical inputs reduced the 2,048-body dense case from 1.033 to 0.898 ms and the sparse case from 0.541 to 0.404 ms; all-pairs checks and byte comparisons cover up to 10,000 bodies. The live crowd comparison was effectively unchanged in the native solver scope, so these isolated gains do not establish an overall FPS improvement. ATH also uses the already-validated result of `refresh_root` directly instead of checking the same handle again.

During each animation update, nodes with the same skeleton, clip table, base clip/time and override clip/time/bone mask reuse an exact evaluated pose, including non-adjacent instances. Every node still advances its own clocks/root motion and updates its own bounds. Procedural strips retain their individual evaluation. The cache ends with the update, so edited clips or reloaded scenes cannot retain an old pose. A controlled 1,024-instance crowd with 22 playback speeds reduced animation CPU time from 1.186 ms to 0.303 ms; independent attack clocks in live combat share fewer poses. Normal clip switches no longer write success logs on the frame path; rejected requests retain diagnostics.

Unique crowd poses are evaluated in at most four chunks on the existing Update pool, including one chunk on the calling thread. Fewer than 256 unique poses stay serial. Workers only write distinct joint-matrix vectors; root motion, procedural strips, hierarchy dirtiness and exact-pose copies stay ordered on the caller. Every job completes before the scene publishes the poses. `Animation Evaluate Poses` isolates this work. CPU validation compared 6,144 parallel poses bit-for-bit with serial base/overlay composition, including sparse curves, all interpolation modes, reversed bone order and planar splines.

`Animation.UniquePoses`, `Animation.ReusedPoses` and `Animation.ProceduralPoses` count actual evaluation jobs, exact-pose copies and individually evaluated strips per update. They count mesh poses, not enemies. Ten live 2,048-enemy samples recorded about 2,025 unique poses and 3,854 reused poses per frame (roughly 66% reused), with pose evaluation near 0.70 ms versus 10.49 ms for scripts. These counters support decisions about further sharing without assuming the cache is ineffective or synchronizing independent attack clocks.

Audio sources may opt into the ambient volume group with `audio.add_source(node, path, {ambient=true})`; existing sources default to SFX. `audio.set_ambient_volume(value)` controls that group independently of music and SFX, while master volume still applies. Per-node playback, live source updates and trigger-zone playback honor the selection. The `ambient` flag round-trips through scene saves, snapshots and prefab loads. ATH persists its Ambient Sounds menu value in the project settings and routes the farm loop to this group.

A scene can own Lua script references directly, persisted as a top-level `scene_scripts` object in the `.pescene` and applied on load. This is the scene-scoped counterpart to the directory-scanned `Scripts/Player` auto-load: it lets one scene declare exactly which gameplay scripts run when it plays and which named action entry points it exposes, without relying on file placement or a boot-script dispatcher. The manifest is `SceneScriptManifest` (`Phasma/Runtime/Code/Scene/SceneScriptManifest.h`), held on `Scene` and serialized next to global settings in both `SaveScene` and `TakeSnapshot`, so it survives editor play→stop snapshot/restore for free.

```json
"scene_scripts": {
    "on_play": ["Assets/Scripts/Scenes/space_gameplay.lua"],
    "actions": {
        "start_game": { "script": "Assets/Scripts/Scenes/space_actions.lua", "function": "start_game" }
    }
}
```

`on_play` scripts run with the existing `PlayerOnly` lifecycle through the normal `ScriptSystem` loops — `ScriptSystem::SyncSceneScripts` re-registers them whenever the active scene generation changes, resolving project-root-relative paths through the same `ResolveProjectScriptPath` helper node scripts use, then init at play-mode entry and destroy on stop. A scene-`on_play` path that is also picked up by the `Scripts/Player` directory scan is loaded once, not twice. Globals are intentionally **not** promoted from scene scripts (unlike directory-scanned scripts), so they stay self-contained and cannot collide across scenes.

`actions` are named `id -> {script, function}` entry points with **zero lifecycle**: the action's script is compiled lazily into a cached environment the first time it is invoked, then the named function is called. Lua `scene.run_action("id")` dispatches one (callable from UI buttons, triggers, the console). The cache is cleared when the scene changes.

The editor authoring surface is the **Scene Scripts** window (`Phasma/Editor/Code/GUI/Widgets/SceneScripts.cpp`, hidden by default — toggle from the window menu): it edits the live manifest (add/remove on_play paths, edit the id/script/function action table) and marks the scene dirty so Save Scene persists it. Paths are authored project-relative.

Deliberately omitted for now — both introduce a non-play lifecycle the snapshot/restore machinery does not yet model: `compose` (a scene-generator phase; the established pattern is to run a builder script in the editor and **bake** its output into the saved scene, à la Warbound's `WB_BAKE`) and `on_load` (runs on scene apply regardless of play). Add `on_play`/`actions` first; revisit these once the lifetime rules are settled.
