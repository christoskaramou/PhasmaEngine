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
they never poll or reload a module. C++ scripting is always built (the `PE_PROJECT_NATIVE` switch was removed 2026-09-29: a build without it ran
`cpp:` scripts silently as nothing); Gradle accepts an optional
`-PPE_PROJECT_NATIVE_DIR=/absolute/path/to/native`, and an empty dir builds the Orbit/PlayerController sample. Code changes require rebuilding the APK.
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

Loader and status follow-up (2026-09-26, commit `c3e08738`; standalone check on Linux including an
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

Review verified 2026-09-26 at `c3e08738`: isolated Windows repros using the production
[loader](../../../../Phasma/Runtime/Code/Script/ProjectNativeHooks.cpp) found three remaining
edge cases. `SweepStaleShadows` converts every filename to the ANSI code page before filtering;
an unrelated CJK/emoji filename throws on code page 1252 and propagates to the editor host's
exit-on-exception handler. The sweep also deletes malformed names such as
`PhasmaGame_live_notes.txt` and `PG~notes.pdb`. `Poll` records failed copy attempts as tried,
so releasing a temporary sharing lock does not retry the unchanged artifact. The standalone
suite still passes; these repros expose cases it does not cover.

Follow-up fixes for those repros (2026-09-26; standalone check on Linux as root and
unprivileged, and on Windows with Clang and with MSVC `cl.exe` with nothing skipped; Windows Release
editor smoke passed with Clang): the sweep compares directory entries in the native encoding
and deletes only fully validated shadow names (this process's stale or a dead process's
`<stem>_live_<pid36>_<seq36>`, Windows-only `PG~<pid36>.<seq36>.pdb`, or the legacy clock-tagged
format); any other name, prefix matches included, is kept. Non-permission copy failures are retried
for the same artifact with backoff (0.5 s doubling to 30 s). `Stage` refuses a file that changed
since `Poll` observed it, notices are counted so identical repeats are logged, `Reset` clears
diagnostics, and a failed in-place fallback keeps the copy error in its message. Still unverified:
debugger-attached rebuilds, a `cl.exe` engine build, and Android on a device.

ABI 6 (2026-09-26) appends runtime UI to
[`ScriptApi`](../../../../Phasma/ProjectNative/Api/ProjectNative.h): `showScreen`, `setQuad`
(frozen `UiQuad` mirroring `runtime_ui.set_quad` options and `RuntimeUiQuadDesc` defaults),
`removeWidget`, `getSurfaceSize` and `getWidgetState` (quad clicks; the Lua `consume_click` only
sees Button widgets, so scripts read `get_state().clicked`); the host side lives in
[`CppScript.cpp`](../../../../Phasma/Runtime/Code/Script/CppScript.cpp). The calls return 0 without
logging when no runtime UI is active, because HUDs call them every frame. No `clear` call: Play Stop
already runs `ClearAllScreens`, and a reloaded module redraws its own IDs. Verified with Clang on
Windows: standalone check, editor smoke (the `ApiProbe` covers screen, quads, style fallback, stale
node and empty-id rejection, widget state, removal, and clearing on Stop), a v5-built project module loading and
running on the v6 host, and a project HUD drawn from a node script; the standalone check also
passes with MSVC `cl.exe` against the v6 header.

ABI 7 (2026-09-26) appends `isLeftMouseDown` (`World::LeftMouseDown`). Lua
`input.is_left_mouse_down` and the native call share `InputState::IsLeftMouseDown` in
[InputBindings.cpp](../../../../Phasma/Runtime/Code/Script/Bindings/Input/InputBindings.cpp), so UI
mouse capture matches; held state only, no click edge. The `ApiProbe` bit 14 checks it reads false
in the hidden smoke editor. The same version appends `setAnimationSpeed` / `getClipDuration` (`World::SetSpeed`,
`World::GetClipDuration`). Both walk the subtree like `playAnimation` (`SetSpeedTree` /
`GetClipDurationTree` in [CppScript.cpp](../../../../Phasma/Runtime/Code/Script/CppScript.cpp));
speed goes through `AnimationSystem::SetSpeed`, duration is `clip.duration / ticksPerSecond`
(25 when unset). `ApiProbe` bit 15 checks both reject an empty node. Version 7 also appends
`setVisible` (`SetNodeRenderVisible`, as `node:set_visible`), `getBonePosition` (the
`animation.get_bone_position` math: joint matrix times inverse offset, last posed frame) and `findChild`
(depth-first by exact name, root included, standing in for a Lua `get_children` walk; child enumeration
stays off the ABI). `ApiProbe` bits 16-17 check lookup, rejection and that the smoke sees the node hidden.
`CppScript.cpp` and `InputBindings.cpp` also compile with MSVC `cl.exe` (2026-09-27, Release objects in the
MSVC tree; no full `cl.exe` engine link).

ABI 8 (2026-09-27) appends `createSphere` (`World::CreateSphere`, the `createCube` path with
`Primitives::CreateSphere`) and `setMaterialColor` (`World::SetColor`). The tint follows Lua
`material.set` in [MaterialBindings.cpp](../../../../Phasma/Runtime/Code/Script/Bindings/Material/MaterialBindings.cpp):
first mesh (`Scene::GetMeshRef`), per-mesh `MaterialInstance` created on demand,
`SetMaterialDirty` only when a factor changed, so calling it every frame for a pulse costs a
material upload only while the color moves. `ApiProbe` bits 18-19 check creation, rejection
(zero radius, empty name, meshless node, NaN), and the smoke reads the tint back through `material.get`.

Fire-and-forget clips (`PlaySound`, `PlaySound3D`) register their file with the miniaudio resource manager on first play (`AudioSystem::KeepResident`), so later plays reuse the loaded data; each play used to re-open and re-read the file once its last instance ended, 2-57 ms on the main thread for a gun's shot after a pause (2026-10-02).

ABI 9 (2026-09-27) appends `playSound` / `playMusic` / `stopMusic` (`AudioSystem::PlaySound` /
`PlayMusic` / `StopMusic`, as the Lua `audio` table in
[AudioBindings.cpp](../../../../Phasma/Runtime/Code/Script/Bindings/Audio/AudioBindings.cpp); `WithAudio`
in `CppScript.cpp` returns false without `PE_AUDIO`) and `launchOption`. Per-node sources were left off the
ABI: scenes already serialize a node's `audio` block, so a looping bed belongs in the `.pescene`. Autoplay
used to fire only at `StartPlayMode`, so a scene loaded during Play (a game's `scene.load` from its menu)
stayed silent; [AudioSystem.cpp](../../../../Phasma/Runtime/Code/Systems/AudioSystem.cpp) `AddSource` now
plays an autoplay source at once while `m_playMode` is set (fixed 2026-09-27). `ReadScriptLaunchOption` moved from
`ScriptSystem.cpp` to [ScriptRuntimeHooks.cpp](../../../../Phasma/Runtime/Code/Script/ScriptRuntimeHooks.cpp)
so Lua `script.launch_option` and the native call share the name check and `_dupenv_s`. `ApiProbe` bit 20
checks the audio rejections, bit 21 reads `PE_SCRIPT_NATIVE_PROBE`, which the smoke sets on the editor it
launches (a too-small buffer or a lowercase name returns false with an empty string).

ABI 10 (2026-09-27) appends `emitBurst` and `setRenderType`. Both share code with Lua instead of copying it:
the four burst presets moved from `ParticleBindings.cpp` to `ParticleManager::FillBurstPreset`
([ParticleManager.cpp](../../../../Phasma/Runtime/Code/Particles/ParticleManager.cpp)), and the render-type
name parse plus apply is `SetNodeRenderTypeByName`
([MaterialBindings.h](../../../../Phasma/Runtime/Code/Script/Bindings/Material/MaterialBindings.h)).
`ParticleBurst` mirrors `emit_burst`'s optional table with a `set` bitmask; every float and vector is
finite-checked. `ApiProbe` bit 22 emits a burst and rejects a NaN position, bit 23 sets `alpha_blend` on the
probe sphere (the smoke reads it back with `material.get_render_type`). The probe bitmask is now 2^24 - 1, the
last value a float position holds exactly; the next ABI's checks need another channel.

ABI 11 (2026-09-27) appends `readFile` / `writeFile` and `createTorus`. The Lua `fs.read` / `fs.write`
bodies moved into `ReadAssetsFile` / `WriteAssetsFile`
([FilesystemBindings.h](../../../../Phasma/Runtime/Code/Script/Bindings/Filesystem/FilesystemBindings.h)), so both
languages share the Assets sandbox (`ResolveAssetsPath` + `IsUnderAssets`) and the game-pack read path;
`readFile` reports the file's size even when the buffer is too small, which `World::ReadFile` uses to grow
once. `ApiProbe` checks 24-26 publish on the node's z (bits 24+): a write and read-back under
`NativeProbe/`, rejections outside Assets and for a missing file, and the torus. The editor's Assets root is
the runtime project's (`build/Release/DefaultProject/Assets` for a bare editor), so the smoke asks Lua for
`assets_path` and removes the probe folder in its `finally` (later Play sessions write it again).

ABI 12 (2026-09-27) appends `addFullscreenPass` / `removeFullscreenPass`, a native equivalent of Lua's
`render_graph.add_pass` fullscreen-post pattern
([RenderGraphBindings.cpp](../../../../Phasma/Runtime/Code/Script/Bindings/API/RenderGraphBindings.cpp),
[PipelineBindings.cpp](../../../../Phasma/Runtime/Code/Script/Bindings/API/PipelineBindings.cpp)). The pass
state (a `PassInfo` per name) is owned by `CppScriptSystem`
([CppScript.cpp](../../../../Phasma/Runtime/Code/Script/CppScript.cpp)), not by the calling script: the
`viewport` / `depthStencil` / `normal` targets are re-resolved every frame (resize-safe) and the `PassInfo`
is created lazily the first frame all three exist, matching the Lua `if not pass then ... end` idiom.
`RemoveFullscreenPass` and the script-system teardown paths (`StopPlay`, `Destroy`) wait for the device to
go idle before deleting a pass's shaders, exactly like the Lua stop sequence's `rhi.wait_device_idle()` +
`destroy_pass_info(pass)`. Script passes carry an owner in the shared registry
([ScriptRenderPasses.h](../../../../Phasma/Runtime/Code/Render/ScriptRenderPasses.h)): the Lua system's
teardown clears only Lua's, so a Lua reload keeps the C++ scripts' passes (before 2026-09-27 it cleared
all of them). A pass whose shader fails to load logs one `[CppScript] fullscreen pass ... failed; pass disabled` error and stays off until re-added, instead of throwing out of the render graph (the editor died on it before 2026-09-27). `ApiProbe` bit 27 adds a pass with a real shader
([FullscreenTest.hlsl](../../../../tools/tests/native-scripts/FullscreenTest.hlsl), copied into the project
Assets by the smoke, never shipped) and rejects a null name/shader and a NaN param; the pass draws until
stage 2, across a Lua reload the smoke checks with `render_graph.has_pass`, and bit 28 removes it and
rejects removing an unknown name. The bitmask needs 29 bits, inside the `z`-channel's `passed >> 24` split.

ABI 13 (2026-09-27) appends `setVolume` / `getVolume` over the audio buses (`AudioBus::Master`, `Music`,
`Sfx`, `Ambient`), the same `AudioSystem` setters Lua's `audio.set_*_volume` calls; values clamp to [0, 1]
and an unknown bus or a non-finite value is refused. `ApiProbe` bit 29 round-trips the music bus
(set, read back, clamp, restore) and checks the refusals.

ABI 14 (2026-09-27) appends `setStyleBackground`, `RuntimeUiSystem::SetStyleBackground` behind Lua's
`runtime_ui.set_style_background`. The plates are global runtime-UI state, so a C++ HUD whose look depended
on a Lua theme call earlier in the session (a Lua boot scene) drew on the dark default plates once its scene
was booted directly; it now sets them itself. `ApiProbe` bit 30 checks the call and its refusals.

ABI 15 (2026-09-27) appends `setNodeUi`, `setNodeEnabled`, `isNodeEnabled`, `getUiRect` and `getScale` so a
C++ script can drive authored scene UI as Lua does (`node:set_ui` / `set_enabled` / `is_enabled` /
`get_ui_rect` / `get_scale` in SceneNodeBindings.cpp). `setNodeUi` validates the whole `NodeUi` before it
writes, so a refused call (no runtime-UI tag, a null string field, a non-finite number, an alignment past 3)
changes nothing. Authored buttons are polled with `getWidgetState("__scene_ui", id)`; C++ scripts get no
`action_function` dispatch. A `setQuad` on an authored widget id loses to `RuntimeUiSystem::SyncSceneWidgets`,
which re-issues every authored widget from its node each frame. The probe's pass mask is 64 bits now
(`z = passed >> 24`, exact to bit 47); bits 31-34 check node UI, enable, scale and the rect of the probe's
node-anchored quad.

ABI 16 (2026-09-27) appends what a settings screen and a quit button reach: `getSetting` / `setSetting`
(Lua `settings.get` / `set` on the bool and number keys, a bool as 0 or 1), `getPresentMode` /
`setPresentMode` (`rhi.change_present_mode`, reporting the mode in effect), `getWindowMode` /
`setWindowMode` (false in the editor), `getTextScale` / `setTextScale`, `getFrameSeconds` (the unscaled
frame time; `update`'s dt follows `time_scale`), `loadScene` and `quit`. The Lua bindings and the ABI share
one implementation in [SettingsBindings.h](../../../../Phasma/Runtime/Code/Script/Bindings/Settings/SettingsBindings.h)
(present-mode request and window mode moved there from RHIBindings / EngineBindings). `loadScene` is queued
and applied after the C++ update loop, never with a script's update on the stack; `StopPlay` drops a queued
load. `quit` is Lua's `on_quit_app`: leave play mode (the Player then exits), else `EventType::Quit`.
`ApiProbe` bits 35-37 round-trip `fxaa`, `render_scale` (clamped) and the text scale, read the frame time and
the modes, and check the refusals; `loadScene` and `quit` are only checked for refusing bad input.

ABI 17 (2026-09-27) appends `getMouseWheel`, `input.get_mouse_wheel`'s frame wheel steps, for script pages
that scroll by wheel (a hub's feats list). `ApiProbe` bit 38 reads a zero wheel and the null refusal.

ABI 18 (2026-09-27) appends `getMousePosition` and `getWindowSize`, `input.get_mouse_position` and
`engine.get_window_size` on shared helpers (`InputState::GetMousePosition`, `GetWindowSize` in
`SettingsBindings.h`), for a pointer in runtime UI surface pixels (a drag stick). `ApiProbe` bits 39 and 40
check a non-negative pointer, a non-empty window and the null refusals.

ABI 19 (2026-09-27) appends `playAnimationLayer`, `setAnimationLayerSpeed`, `stopAnimationLayer`,
`getAnimationLayer`, `getClipMarker` and `setLightIntensity`: `animation.play_layer` / `set_layer_speed` /
`stop_layer` / `get_layer_state` / `get_markers` over the node's animated tree (the bone mask as one
`,`-separated string; an empty or blank name is refused, as the engine's layer refuses no bones) and
`lights.set_property` intensity on every light the node owns, found through `Scene*Light::nodeId`, so a C++
script drives rig attack layers timed to their markers and prefab lights. `ApiProbe` bits 41-43 check the
refusals on an empty node.

ABI 20 (2026-09-28) appends `vibrate`: `input.vibrate`'s haptic pulse through the shared
`InputState::Vibrate` (Android's Vibrator over JNI, 12 ms when `ms <= 0`), returning 0 on the desktop, which
has no vibrator; `ApiProbe` bit 44 checks that.

ABI 21 (2026-09-28) appends `createQuad`, `setMaterialTexture`, `setDoubleSided` and `setAlphaCutoff`:
`primitives.quad`, `material.set_texture`, `material.set_double_sided` and `material.set(node, "alpha_cutoff")`
through helpers `MaterialBindings.h` now exports beside `SetNodeRenderTypeByName`, so a C++ script builds the
textured ground sprites Lua's `Art.part{kind = "quad", texture = ...}` makes. `ApiProbe` bits 45-47 check a made
quad, its cutoff and two sides, and the refusals (no name or size, no mesh, an unknown slot, an empty path); the
smoke's z reaches 2^24 - 1, the last bit a float position carries exactly.

ABI 22 (2026-09-29) appends `playAnimationLayerAnchored` (`PlayLayer(..., anchor)`): `animation.play_layer`'s anchor
bone for C++ scripts. `EvaluateStatePose` copies a layer's masked bones as whole rig-space poses, so without an
anchor an attacking chest keeps the attack clip's hips while the base run moves the real ones (AgainstTheHero's
running bomb toss sat 5-6 cm off its pelvis); anchored, the group is realigned to the base pose's anchor bone each
frame. It refuses a rig without the anchor, an empty anchor or mask. No new probe bit (z is full): bit 41 adds the
anchored refusals.

ABI 23 (2026-09-29) appends `getTouch` (`Touch(index, id, x, y)`): the two fingers `InputState` tracks
(`GetTouch`), with SDL's finger id and window pixels, so a script can run two on-screen sticks (the first finger
alone also reaches scripts as the synthesized left mouse; a second never did). A finger landing on the runtime UI
stays the UI's (`PlayerHost` forwards `SDL_FINGERDOWN` only when the UI is not capturing), but a tracked finger's
motion and lift are always forwarded: a thumb that slid over a HUD button before lifting stayed down before. Bit 44
adds "no fingers on the desktop" (z is full).

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

Runtime profiling includes separate `Animation System` and `Audio System` CPU scopes, alongside physics, scripts, scene updates and GPU passes, so skeletal evaluation and main-thread audio maintenance can be measured independently. An instance rebuild refreshes image views, material tables and mesh constants together; `FlushPendingGpuWork` clears the texture dirty flag after that rebuild instead of repeating the same refresh through `UpdateTextures`. A texture-only flush (a script's first `material.set` / C++ `setColor` on a node creates its `MaterialInstance` and marks textures dirty) keeps `UpdateTextures`' queue wait: `UpdateImageViews` bumps the geometry version and GbufferPass / DepthPass then rewrite every frame's descriptor set, so the wait is what keeps in-flight frames safe (a no-wait variant was reverted after review, 2026-10-02; the instance rebuild does the same rewrite without a wait, a pre-existing hazard). Mid-gameplay that wait is a full GPU drain, so a game that tints spawned rigs should create the instance in the spawn frame, where the instance rebuild absorbs it (ATH does, `AthMatch.cpp`). `ParticleManager` bumps its buffer version (the particle passes' all-frames descriptor rewrite) only when the particle buffer is reallocated; both passes rebind bindings 0/1 every frame.

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

During each animation update, nodes with the same skeleton, clip table, base clip/time, override clip/time/bone mask and crossfade (previous pose, elapsed, duration; a finished fade counts as none) reuse an exact evaluated pose, including non-adjacent instances. A rig's mesh nodes therefore share one evaluation during ABI 24 crossfades too (`AnimationSystem.cpp` PoseKey; ~140-creep ATH horde: 7.4 to 0.3 separate fade evaluations per frame, verified 2026-10-01). Starting a fade copies the pose the node shows (`FadeSnapshot`, once Update has posed the state; a base-only fade with a layer up, a procedural strip or an unposed state still evaluates), and a fading `PlayLayer` skips its immediate `EvaluateState`: the shown pose is the fade's weight-0 frame, so a hit react costs a pose copy per mesh instead of two evaluations on the script thread (2026-10-02). Every node still advances its own clocks/root motion and updates its own bounds. Procedural strips retain their individual evaluation. The cache ends with the update, so edited clips or reloaded scenes cannot retain an old pose. A controlled 1,024-instance crowd with 22 playback speeds reduced animation CPU time from 1.186 ms to 0.303 ms; independent attack clocks in live combat share fewer poses. Normal clip switches no longer write success logs on the frame path; rejected requests retain diagnostics.

Unique crowd poses are evaluated in at most four chunks on the existing Update pool, including one chunk on the calling thread. A chunk takes at least 32 poses, so fewer than 64 unique poses stay serial (was 128 / 256 before 2026-10-01; a ~115-pose ATH horde now runs parallel, pose evaluation 0.56 to 0.34 ms). Workers only write distinct joint-matrix vectors; root motion, procedural strips, hierarchy dirtiness and exact-pose copies stay ordered on the caller. Every job completes before the scene publishes the poses. `Animation Evaluate Poses` isolates this work. CPU validation compared 6,144 parallel poses bit-for-bit with serial base/overlay composition, including sparse curves, all interpolation modes, reversed bone order and planar splines.

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
