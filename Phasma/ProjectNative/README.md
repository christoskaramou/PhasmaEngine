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

Global scripts use Always / Editor / Play modes. Node scripts attach through
source-file paths (`Orbit.cpp`) or legacy `cpp:Orbit` identifiers, using the node's
Player / Editor / Both mode. Existing scene serialization stores this string unchanged.
Every attachment gets its own module-owned C++ object. Play-only instances are destroyed
before Stop restores the scene snapshot; they are recreated on the next Play.
Paused play does not update or create instances.

The API supports logging, finding nodes, creating cubes, validated node handles,
local position reads/writes, and held-key input. `phasma::World::KeyDown("W")` uses
the same UI-capture-aware input helper as Lua `input.is_key_down`; unknown/null key names
return false. `phasma::World` provides C++ convenience methods. ABI version 3 adds source-file
metadata; rebuild existing game modules against this header when updating the host.
Grow this API through shared engine operations as gameplay needs arise; API layout changes
require an ABI version bump. All calls are synchronous and on the main thread.
A handle is opaque and must be validated; never retain engine pointers or schedule work that
can outlive the module. Native code remains trusted process code, not a sandbox.

## Editor authoring

Use node Properties → Add Component → C++ Script → New C++ Script or Browse C++ Source.
New scripts are saved into the configured native source directory (the sample directory
when `PE_PROJECT_NATIVE_DIR` is empty). Browsing an external source imports a copy there;
existing files are never overwritten by import. Existing compiled scripts appear by filename.
Edit Script opens the actual source with C++ highlighting. Save & Build saves in place and
runs only the game-module target asynchronously; compiler output appears in the editor.
The generated executable-adjacent `NativeScripts.json` records the local CMake build and
source directory. This is a development-machine configuration, not a distributable asset.
Files must have unique basenames and contain one registered node script each; serialized
source references match by filename so an Android player does not need desktop source paths.
The template uses the chosen filename as its class name. Legacy `cpp:` references still work.

## Desktop reload

Only the editor live-reloads. Other desktop hosts (Player, exported games) load the installed
module in place once at startup: no polling, no shadow copy, and nothing is written next to the
executable, so read-only install folders work. A missing or rejected module is reported once.
Lua reloads (script saves, `reload_scripts()`) never unload the module or reset C++ script state.

The editor polls the executable-adjacent module every 500 ms and requires its timestamp/size
to remain unchanged across two observations. It loads a uniquely named copy so the build
output remains writable on Windows. Copies left by killed processes are swept on the next load.
`PhasmaExport` ships `PhasmaGame.dll` / `libPhasmaGame.so` beside the exported player. It checks the entry point, ABI, descriptor kinds/modes,
callbacks, and duplicate names before replacing anything.

A failed build or rejected module leaves the active code and objects running. A changed
artifact is retried. After validation succeeds, between frame dispatches the host destroys
all old instances, unloads their DLL, switches descriptors, and recreates eligible instances.
Private C++ state resets; the scene stays in place. Constructor failures after acceptance
disable that instance; this is not transactional rollback of gameplay side effects.

Instances that fail create/update are disabled but still destroyed. No STL objects,
exceptions, allocations requiring cross-module deletion, or mutable callback registries
cross the boundary. Cleanup receives only the instance: cached handles can already be
invalid after node deletion or scene replacement, so destructors must tolerate that.

The sample orbits a node named `ProjectNativeDemo`, or any node assigned `cpp:Orbit`.
It never auto-spawns a crowd into an existing project.

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
ABI/descriptor/load rejection, writing over the build output while loaded, repeated reload,
exception containment, and object/copy cleanup. It also exercises statically linked module
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
a Lua reload, and ABI rejection.
It uses unsaved test nodes and quits without saving the scene.
