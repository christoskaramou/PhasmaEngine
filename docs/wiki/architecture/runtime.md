# Runtime And Project Boundary

PhasmaRuntime is the shared runtime layer between PhasmaCore and the host products. It owns project/runtime contracts that should be identical for the editor and a standalone player. It should not own editor UI, hot-reload UI, launcher UI, ImGui panels, or module-reload mechanics.

## Layer Shape

- `PhasmaCore` remains the low-level engine foundation: RHI, ECS, platform-adjacent services, paths, settings, and shared primitives.
- `PhasmaRuntime` sits above PhasmaCore and below hosts. It defines how a project is described, how a runtime session resolves project-relative paths, and the shared SDL/window/RHI boot primitives that editor and player hosts use before handing off to their own loops.
- `PhasmaEditor` remains the desktop editor concept. `PhasmaEditorModule` is the current hot-reload DLL implementation detail, not the product/layer name.
- `PhasmaPlayer` is the first standalone host over PhasmaRuntime instead of a copy of editor startup logic.

## Runtime UI

Runtime/game UI is a backend-neutral service in `PhasmaRuntime` with concrete renderer implementations outside PhasmaCore. The runtime-facing API is intentionally small: screens can be shown/hidden and populated with prototype text, number, bool, and button widgets. Public runtime and Lua call sites do not include ImGui types.

`PhasmaRuntimeUI` provides the first concrete backend, Dear ImGui. The player creates that backend at the host boundary, sends SDL events to runtime UI before gameplay input, exposes UI capture state to runtime input bindings, and renders the UI after the render graph has produced the display target but before `BlitToSwapchain`. RmlUi is reserved as a future backend that can plug into the same lifecycle/event/render surface without rewriting runtime/game call sites.

## MyProject Contract

The project contract starts as a small descriptor:

- project name, usually `MyProject` while local project generation is still simple;
- project root directory;
- manifest path, defaulting to `phasma_project.json` under the project root;
- assets directory relative to the project root, defaulting to `Assets`;
- optional startup scene path relative to the project root.

The manifest format is JSON:

```json
{
    "version": 1,
    "name": "MyProject",
    "assets": "Assets",
    "startup_scene": "Assets/Scenes/sponza.pescene"
}
```

The active project selection is stored in executable-local `phasma_settings.json`:

```json
{
    "project_path": "C:/path/to/MyProject/",
    "project_manifest": "C:/path/to/MyProject/phasma_project.json",
    "startup_scene": "Assets/Scenes/sponza.pescene"
}
```

`project_manifest` is preferred when present. If it is missing, runtime helpers fall back to `project_path`; if `project_path/phasma_project.json` exists, it is loaded, otherwise the project root is treated as a legacy project without manifest data. When neither field exists, the built-in executable assets root is used so the current editor flow still starts.

`startup_scene` may be omitted from the manifest, or set to an empty string, when a project does not have a startup scene yet.

Startup scene precedence is explicit launch setting first, then editor restore, then project manifest fallback:

- `phasma_settings.json:startup_scene` wins when it is non-empty, so a launcher-selected scene cannot be overwritten by stale editor restore state;
- an existing `phasma_settings.json:startup_scene` key with an empty value is an explicit "no startup scene" selection and suppresses editor/manifest fallback;
- `Assets/editor_config.json:last_scene` is the editor restore fallback;
- `phasma_project.json:startup_scene` is used when neither runtime settings nor editor restore selected a scene.

Successful editor scene loads update both `editor_config.json:last_scene` and `phasma_settings.json:startup_scene` so the launcher, editor restore, and runtime host do not drift. Creating a new scene clears both values.

The launcher stores `project_path` as the project root when a manifest project is detected. It uses the project's configured assets root for startup-scene discovery and browsing. Legacy projects without a manifest may still use the assets directory itself as `project_path`.

The launcher UI has global backend/settings/validation controls plus Editor and Player tabs. Both tabs can pick a project and startup scene; the Player tab can launch `PhasmaPlayer` or discovered `WebGPU*` sample executables. The inline settings editor loads JSON into the executable-local `phasma_settings.json` contract before launch. The validation row follows the selected backend and is a single PhasmaCore checkbox. It sets child-process validation env vars for the selected backend (`PE_VULKAN_VALIDATION` for Vulkan, `PE_DX12_DEBUG`/`PE_DX12_GBV`/`PE_DX12_DRED` for DX12).

Path resolution rules:

- absolute paths are preserved and normalized;
- project paths resolve against the project root;
- asset paths resolve against the project assets root;
- startup scene paths resolve against the project root, so `Assets/Scenes/foo.pescene` is valid and explicit.
- runtime/editor startup-scene settings first check the path as written, then next to the executable, then under the runtime assets root.

After resolving the active project, editor and player hosts apply the selected assets root to `Path::Assets` before registering file watchers, resolving startup scenes, loading scripts, or creating render resources. Manifest projects use the manifest `assets` directory; legacy no-manifest projects keep treating the selected `project_path` as the assets root.

## First Implementation Slice

The first code slice is intentionally boring:

- add a `PhasmaRuntime` static library target;
- add `ProjectConfig`;
- load and write `ProjectConfig` from the `phasma_project.json` manifest;
- resolve active projects from `phasma_settings.json`;
- read and write `startup_scene` in `phasma_settings.json`;
- resolve startup-scene precedence and scene startup settings (`present_mode`, `render_scale`) in PhasmaRuntime so editor, launcher, and player host use one contract;
- persist `project_manifest` from the launcher when a selected project has a manifest;
- let the editor resolve the active project at startup and use the manifest startup scene only when neither explicit runtime settings nor editor restore selected a scene;
- add the first standalone `PhasmaPlayer` host over PhasmaRuntime. `PhasmaPlayer` is now a thin executable entrypoint; PhasmaRuntime owns SDL/window/RHI lifetime, resolves the active project, validates the manifest startup scene path, initializes the swapchain, and runs the player frame loop without editor GUI, launcher, MCP, or hot-reload module loading;
- link the editor host and editor module to `PhasmaRuntime`;
- move shared display parsing, SDL video lifetime, runtime window creation, and RHI session lifetime into `RuntimeHost`, with the editor host still owning hot-reload/module mechanics;
- add `SceneAccess` as the first scene seam. Today the editor registers the active scene from `RendererSystem`; direct render-pass/widget/script access now calls `GetActiveScene()` so future ownership can move into runtime without preserving the renderer-as-scene-owner shape;
- add `SceneHost` as the scene mutation funnel. Scene load/new/save/preload/apply calls route through PhasmaRuntime, which can now run the default operations from the registered active scene plus a host-specific pre-mutation sync callback. The editor sync drains in-flight frame commands; the player sync waits for the device before destructive scene mutations;
- keep `SceneAccess`, `SceneHost`, and the other runtime host callback registries registered inside the image that uses them while PhasmaRuntime is a static library linked into both the editor executable and hot-reload module;
- move `Camera` into PhasmaRuntime after replacing its direct `RendererSystem`/`TAAPass` dependencies with camera runtime callbacks. PhasmaRuntime now provides the default callback table by reading the active `SceneRendererHost` display target for aspect and the runtime `TAAPass` for projection jitter;
- move `SkyBox` into PhasmaRuntime as a leaf rendering asset helper over PhasmaCore RHI/resource APIs;
- move material runtime data/reflection into PhasmaRuntime: `Material`, `MaterialInstance`, `MaterialAsset`, `PassInfoAsset`, material annotation parsing, and backend material reflection now live outside the editor tree. `PassInfoAsset` uses RapidJSON so PhasmaRuntime does not depend on MCP's bundled `nlohmann` header;
- move runtime-owned data/model helpers into PhasmaRuntime: `AudioTypes`, `PhysicsTypes`, animation clip/import/evaluation code, model asset loading/primitive generation, and particle buffers/manager now live outside the editor tree;
- move scene vocabulary headers (`NodeComponents`, `SceneNode`, and `SceneNodeHandle`) plus runtime-neutral SDL window helpers into PhasmaRuntime. `WindowEvents` centralizes drawable-size, resize-event, and minimized-window checks shared by the editor host and player frame pump;
- move `Scene` implementation into PhasmaRuntime after replacing editor-only selection, animation, physics, audio, and render-descriptor calls with `SceneRuntimeHooks`. PhasmaRuntime now builds the default service hooks for animation, physics, audio, and render-descriptor refresh; the editor only layers selection hooks on top;
- rename runtime scene light metadata records from editor-specific names to `Scene*Light` types so the moved `Scene` surface reads as runtime/player data rather than editor-only state;
- add `SceneRendererHost` as the render-pass resource seam. Runtime-owned raster passes now ask the active renderer host for render targets, skyboxes, IBL LUTs, and the active `Scene` instead of reaching directly for the editor `RendererSystem`;
- move the full built-in render-pass set into PhasmaRuntime: raster passes, postprocess passes, particle passes, ray-tracing pass, editor overlay passes, and the CACAO backend now live beside the runtime renderer. Editor overlays (`GridPass`, `AabbsPass`) remain disabled by the player settings path;
- add `RuntimeSceneRenderer`, a player renderer over the moved pass set. It owns runtime render targets, skybox/IBL resources, the render graph, frame semaphores, resize handling, pass-state updates, and swapchain blits. It supports the runtime render modes/postprocess settings without editor GUI, launcher, MCP, or hot-reload module loading;
- move screenshot PNG writing into PhasmaRuntime and wire `RuntimeSceneRenderer` to handle player screenshot requests from runtime Lua. The editor renderer uses the same runtime PNG writer, so screenshot encoding no longer depends on `PhasmaMCP`;
- add runtime shader-reload handling for render-pass components. Editor `RendererSystem` and `RuntimeSceneRenderer` share the same shader refresh helper, including secondary pass infos and ray-tracing shader groups, and `PhasmaPlayer` handles runtime Lua `CompileShaders` events by refreshing matching pass shaders before clearing pipeline command caches;
- keep the CACAO vendor sources unmodified while routing CACAO Vulkan shader-module creation through a PhasmaRuntime wrapper. The wrapper patches CACAO's precompiled float storage-image SPIR-V declarations to formatless storage images so Vulkan validation accepts CACAO's compact internal image views, while integer/atomic storage images stay typed;
- remove the editor `PostProcessSystem` adapter. The editor `RendererSystem` now creates the full built-in pass set, owns pass-state updates, shader polling, resize handling, and render-graph compilation directly over runtime pass components;
- move `AnimationSystem`, `PhysicsSystem`, and `AudioSystem` into PhasmaRuntime. The shared default `SceneRuntimeHooks` let scene load/delete/component code drive animation, physics, audio, and render-descriptor refresh without duplicating system callback tables in editor and player hosts;
- move `ScriptSystem` and runtime-safe Lua bindings into PhasmaRuntime. `ScriptRuntimeHooks` provides host state and mutation callbacks for play/pause, viewport focus, and model-loading indicators; the player registers runtime play/pause state, while the editor registers GUI-backed overrides. The editor keeps GUI/profiler/selection Lua extensions in `PhasmaEditor`, while player scripts get scene/node/model/material/camera/light/animation/physics/audio/input/settings/filesystem/shader/RHI helper bindings from runtime;
- filter editor-only global Lua scripts in PhasmaRuntime before executing them. Global scripts marked `phasma: editor-only` load only when the editor host opts in through `ScriptRuntimeHooks`, so PhasmaPlayer does not execute editor shortcut scripts that depend on GUI/selection bindings;
- keep the legacy Lua `update_editor` hook name for script compatibility, but treat it internally as an edit-mode hook rather than an editor-owned runtime path;
- add runtime/game UI behind a backend-neutral `RuntimeUiSystem` in PhasmaRuntime and a separate `PhasmaRuntimeUI` Dear ImGui backend. PhasmaPlayer now routes SDL events through runtime UI before gameplay input, masks Lua input polling when UI captures mouse/keyboard, renders the UI after the runtime render graph and before swapchain blit, and exposes a small `runtime_ui` Lua surface for screen visibility plus simple prototype widgets;
- add `RuntimePlaySession` as the shared play-service lifecycle surface. The editor play button still owns snapshots, toolbar state, and undo reset, while runtime owns starting/stopping physics/audio play services, optional script init for player startup, pause propagation, and animation-state cleanup;
- wire `PhasmaPlayer` to load the selected startup scene, initialize `RuntimeSceneRenderer`, initialize Lua scripts and per-node script instances, start runtime physics/audio play mode, pump SDL input into runtime input state, tick `FrameTimer`, process runtime quit/compile/resize events, and render the scene frame loop.
- keep PhasmaPlayer shutdown ordered so runtime play services, file watchers, renderer resources, and global systems stop while the scene/context are still valid; the scene is then destroyed before `Context::Remove()`.
- rename the top-level CMake project to `PhasmaEngine`, route shared Lua/CACAO/sol2/miniaudio include paths through `PE_SHARED_THIRD_PARTY_DIR`, and make the executable-local asset copy target `PhasmaRuntimeAssets`. The shared vendors and built-in assets still physically live in their historical editor tree until a larger repository-layout move is worth the churn.

The remaining editor-owned surfaces are host/product concerns rather than player runtime behavior: editor GUI, launcher UI, MCP/editor-agent tooling, hot-reload/module mechanics, `Window`, `SelectionManager`, and editor-only Lua GUI/profiler/selection bindings.
