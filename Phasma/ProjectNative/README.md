# C++ scripts and live reload

Desktop hosts can load an opt-in `PhasmaGame.dll` / `libPhasmaGame.so` module.
PhasmaRuntime remains static. Game modules include only [ProjectNative.h](Api/ProjectNative.h);
they do not link engine libraries, include engine headers, or use Lua/sol2.

## Build

Configure the engine with `-DPE_PROJECT_NATIVE=ON`. The default module is the
[Orbit](Sample/Orbit.cpp) and [PlayerController](Sample/PlayerController.cpp) samples. Build independently:

```sh
cmake --build build-review-native --config Release --target PhasmaProjectNative
```

For an external project, also set `-DPE_PROJECT_NATIVE_DIR=/path/to/native`.
The directory's `.cpp` files are discovered automatically, including new files on rebuild.
It may optionally supply `project_native.cmake` for additional sources/include directories:

```cmake
set(PE_PROJECT_NATIVE_SOURCES "${CMAKE_CURRENT_LIST_DIR}/Game.cpp")
set(PE_PROJECT_NATIVE_INCLUDE_DIRS "${CMAKE_CURRENT_LIST_DIR}")
```

The game target has no engine PCH, feature flags, or engine link dependencies.
Native scripting defaults off. Android builds the same sources as a static library linked
into the player. The player validates the linked module at startup and uses the same
script lifecycle; it does not poll, copy, or reload game libraries. Rebuild the APK to change code.
For Gradle, pass `-PPE_PROJECT_NATIVE=ON` and optionally
`-PPE_PROJECT_NATIVE_DIR=/absolute/path/to/native` (also accepted in `local.properties`).

## Script contract

Each source file includes `ScriptModule.h`, defines a script class, and ends with
`PHASMA_NODE_SCRIPT(ClassName)`. The build supplies the module entry point; project
sources must not define another `PhasmaGetScriptModule`. Registration stays inside
the game module. Static initializers must not touch the engine or start threads.
Descriptor names are unique across global and node scripts. Use `phasma::Script<T>`
to generate exception-containing create/update/destroy callbacks for a C++ class.
The class constructor receives `(const phasma::ScriptApi &, phasma::Node)`, and
implements `Update(double)`. Its destructor must not throw.

Global scripts use Always / Editor / Play modes. Node scripts attach by bare source filename
(`Orbit.cpp`) or legacy `cpp:Orbit` identifiers, using the node's Player / Editor / Both mode.
Scenes store only the filename, so they stay machine-independent; older scenes holding absolute
paths (Windows or POSIX) still match by filename on every host.
Every attachment gets its own module-owned C++ object. Play-only instances are destroyed
before Stop restores the scene snapshot; they are recreated on the next Play.
Paused play does not update or create instances.

The API supports logging, finding nodes, creating cubes, validated node handles,
local position reads/writes, held-key input, prefab instantiation, node (subtree) deletion,
rotation/scale writes, and animation playback. `phasma::World::KeyDown("W")` uses
the same UI-capture-aware input helper as Lua `input.is_key_down`; unknown/null key names
return false. `phasma::World` provides C++ convenience methods. `SetRotation`/`SetScale`
keep zero-scale axes finite. Only the node matrix is stored, so a reflection is always decomposed as
a negative X scale, whichever axis was mirrored. For a Y- or Z-mirrored node the decomposed rotation
carries a 180-degree turn, so `SetRotation` can change its orientation while keeping the reflection:
setting rotation (0, 0, 0) on `diag(1, -3, 4)` yields `diag(-1, 3, 4)`. A later all-positive
`SetScale` removes the reflection.
All calls are synchronous and on the main thread.
A handle is opaque and must be validated; handles of deleted nodes (whole subtrees) become
invalid and are pruned. Never retain engine pointers or schedule work that can outlive the module.
Native code remains trusted process code, not a sandbox.

ABI 6 adds runtime UI, mirroring Lua `runtime_ui`: `ShowScreen(screen, visible, overlay)`
(`show` + `set_screen_overlay`), `SetQuad(screen, id, UiQuad)` (`set_quad`; `UiQuad` carries the
same options and defaults, so a default `UiQuad` draws what an empty options table draws),
`RemoveWidget` (`remove`), `SurfaceSize` (`get_surface_size`) and `WidgetState` (`get_state`;
`Clicked` wraps it). `clicked` holds for the frame of the click and works on quads, unlike Lua
`consume_click`, which only sees Button widgets. Screens are created on first use
and start hidden; Play Stop clears them. Quad coordinates are surface pixels. A quad anchored to an
invalid node handle, or with an empty screen or id, is rejected; out-of-range style/alignment values
fall back to the defaults. The calls return false without logging when no runtime UI is active, so
they are safe every frame; `SetQuad` reloads an image only when its path changes.

ABI 7 adds `LeftMouseDown()`, the same UI-capture-aware helper as Lua `input.is_left_mouse_down`:
false while runtime/editor UI captures the mouse. It is a held state, not a click edge; track the
previous frame for press/release. It also adds `SetSpeed(node, speed)` (playback rate on the node
and every animated descendant; false when none animates) and `GetClipDuration(node, clip, seconds)` (clip length in seconds from the
first node in the subtree that has the clip). Scale an attack clip to a cooldown with
`SetSpeed(node, seconds / interval)`. `SetVisible(node, visible)` mirrors `node:set_visible` (this node only), `BonePosition(node, bone, out)`
mirrors `animation.get_bone_position` (model space, last posed frame), and `FindChild(root, name)` returns
the root or its first depth-first descendant with that exact name, as a Lua `get_children` walk finds it.

The ABI is append-only: new `ScriptApi` function pointers go at the end with a
`ScriptAbiVersion` bump, and the `ScriptDesc` / `ScriptModule` / `UiQuad` / `UiSurface` layouts are
frozen. A module runs on any same-or-newer host; hosts accept modules from `ScriptAbiMinVersion` (5)
up to the current version. Modules built against ABI 4 or older need one rebuild.

Descriptors carry only the source filename: `ProjectNative.cmake` defines `PHASMA_SOURCE_NAME`
per source, so binaries (including APKs) contain no build-machine paths. Builds outside
`ProjectNative.cmake` fall back to `__FILE__`; pass `PHASMA_SOURCE_NAME` rather than `__FILE__`
when calling `phasma::RegisterScript` by hand.

## Editor authoring

Use node Properties → Add Component → C++ Script → New C++ Script or Browse C++ Source.
New scripts are saved into the configured native source directory (the sample directory
when `PE_PROJECT_NATIVE_DIR` is empty). Browsing an external source imports a copy there;
existing files are never overwritten by import. Existing compiled scripts appear by filename.
Edit Script finds the source by filename under the `sources` folder recorded in
`NativeScripts.json` and opens it with C++ highlighting. Save & Build saves in place and
runs only the game-module target in the background (quitting the editor never waits for it);
compiler output appears in the editor, followed by the live-reload result for the artifact that
build produced: reloaded with the script count, already running (no-op build), rejected with the
loader's reason (previous code stays active), or not observed within 15 s. The verdict is matched
by artifact identity (build output size and write time), so a delayed earlier build is never
credited to a later one. `engine.native_scripts_status()` returns `{reloads, attempts, active,
live_reload, scripts, error, artifact, attempted}` to Lua and MCP agents; `attempts` counts every
artifact tried, so a repeated rejection with an identical error is still visible, and `live_reload`
is true only while this host polls and hot-swaps the module (false for exported and in-place
loads, statically linked Android modules, and after the fallback below).
The generated executable-adjacent `NativeScripts.json` records the local CMake build and
source directory. This is a development-machine configuration, not a distributable asset.
Files must have unique basenames and contain one registered node script each.
The template uses the chosen filename as its class name. Legacy `cpp:` references still work.

## Desktop reload

The editor and Players run from a build folder (`NativeScripts.json` beside the executable)
live-reload. Exported games, which Export ships without that file, load the installed module in
place once at startup: no polling, no shadow copy, and nothing is written next to the
executable, so read-only install folders work. A missing or rejected module is reported once.
Lua reloads (script saves, `reload_scripts()`) never unload the module or reset C++ script state.

Live-reloading hosts poll the executable-adjacent module every 500 ms and require its
timestamp/size to remain unchanged across two observations. They load a copy named
`PhasmaGame_live_<pid>_<seq>` so the build output remains writable on Windows. When
`PhasmaGame.pdb` exists, each copy gets its own short `PG~<pid>.<seq>.pdb` and the copy's CodeView
path is patched to it, so a debugger attached to the editor no longer locks the build PDB
(LNK1201); if the name cannot fit the module's CodeView path, a warning is logged and the original
PDB stays referenced. Shadow files are tagged with the owning process id: an editor and a Player
sharing a build folder keep each other's files, and only fully validated shadow names (this format, or
the older clock-tagged `PhasmaGame_live_<clock>_<seq>` format) are ever deleted; any other file, even
one sharing the prefix such as `PhasmaGame_live_notes.txt`, is left alone. Directory entries are
compared in the platform's native encoding, so files whose names the ANSI code page cannot represent
are safe too. Files of a process that is gone are swept on
the next load. If the initial load's shadow copy is refused for permissions (access denied, read-only), the
module is loaded in place and live reload is disabled for that session, with the reason logged.
Other copy failures (disk full, sharing violations, over-long paths) stay ordinary failures, because
loading in place would lock the build output on Windows; they are retried for the same artifact with
backoff (0.5 s doubling to 30 s), so a lock such as an antivirus scan of the new DLL clears without
another build. A file that changes between the poll that observed it and the copy is not loaded under
the old record; the next poll picks up the new artifact. A
running module is never replaced through the fallback. The loader checks the entry point, ABI, descriptor kinds/modes, callbacks, and
duplicate names before replacing anything. `PhasmaExport` ships `PhasmaGame.dll` /
`libPhasmaGame.so` beside the exported player.

A failed build or rejected module leaves the active code and objects running. A changed
artifact is retried. After validation succeeds, between frame dispatches the host destroys
all old instances, unloads their DLL, switches descriptors, and recreates eligible instances.
Private C++ state resets; the scene stays in place. Constructor failures after acceptance
disable that instance; this is not transactional rollback of gameplay side effects.

Instances that fail create/update are disabled but still destroyed. On Windows (MSVC builds) a
hardware fault in a script callback, such as an access violation or stack overflow, is contained:
the instance is disabled, the fault code logged, and its state leaked rather than destroyed.
Other platforms still crash. Game modules must use `/EHsc` (the CMake default) for this. No STL objects,
exceptions, allocations requiring cross-module deletion, or mutable callback registries
cross the boundary. Cleanup receives only the instance: cached handles can already be
invalid after node deletion or scene replacement, so destructors must tolerate that.

The sample registers node scripts only (`Orbit.cpp`, `PlayerController.cpp`); attach them from
Add Component → C++ Script. It registers no global script and never auto-spawns into a project.

The same sample module registers `PlayerController`. Attach it to a node with
`node:set_script("cpp:PlayerController", "player")`, enter Play, and use WASD.
W/S move along local -Z/+Z; A/D along -X/+X, at 4 local units per second. Diagonal
movement is normalized and Y is preserved. Edit the sample's `speed` and rebuild
`PhasmaProjectNative` to change speed during desktop Play. Input is keyboard-only
(Android requires a hardware keyboard); touch/gamepad input is not yet exposed.
This is direct transform movement without gravity, collision response, or automatic facing.

## Verification

```sh
cmake -S tools/tests/native-scripts -B build-native-script-check -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-native-script-check
ctest --test-dir build-native-script-check --output-on-failure
```

The standalone test uses the production loader and actual DLLs. It covers replacement,
ABI/descriptor/load rejection, append-only ABI compatibility, writing over the build output while
loaded, repeated reload, exception and (on MSVC) hardware-fault containment, PDB path patching,
the live-reload policy, script path/filename helpers, handle pruning, TRS math, background build
tasks, and object/copy cleanup. It also exercises statically linked module
validation, execution, cleanup, and reinitialization. It does not require the renderer.
It checks that non-editor hosts load in place once without copying or polling.
It also loads the real sample module against mock input/scene callbacks to check controller
movement, diagonal speed, opposing keys, invalid time steps, stale nodes, and old-API rejection.

The optional Windows editor smoke uses a disposable editor process and restores the game DLL:

```sh
python tools/tests/native-scripts/editor_smoke.py --binary-dir build-review-native/Release --probe-dir build-native-script-check
```

Enable `mcp` in that build's project `Assets/Agent/agent_config.json` first and leave port 8765 free.
The smoke verifies real editor Play/Stop/re-Play, scene retention, live replacement, C++ state surviving
a Lua reload, ABI rejection and recovery through `engine.native_scripts_status()`, the scene API
(prefab, rotation, scale, destroy, animation) and UI API (screen, quads, fallback and rejection,
widget state, removal, clearing on Stop) against real engine state, and that a faulting
script (`probe7`) leaves the editor and MCP session running.
It uses unsaved test nodes and quits without saving the scene.
