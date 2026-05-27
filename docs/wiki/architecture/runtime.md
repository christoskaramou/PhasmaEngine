# Runtime And Project Boundary

PhasmaRuntime is the shared runtime layer between PhasmaCore and the host products. It owns project/runtime contracts that should be identical for the editor and a standalone player. It should not own editor UI, hot-reload UI, launcher UI, ImGui panels, or module-reload mechanics.

## Layer Shape

- `PhasmaCore` remains the low-level engine foundation: RHI, ECS, platform-adjacent services, paths, settings, and shared primitives.
- `PhasmaRuntime` sits above PhasmaCore and below hosts. It defines how a project is described, how a runtime session resolves project-relative paths, and the shared SDL/window/RHI boot primitives that editor and player hosts use before handing off to their own loops.
- `PhasmaEditor` remains the desktop editor concept. `PhasmaEditorModule` is the current hot-reload DLL implementation detail, not the product/layer name.
- `PhasmaPlayer` is the first standalone host over PhasmaRuntime instead of a copy of editor startup logic.

## Runtime UI

Runtime/game UI is a backend-neutral service in `PhasmaRuntime` with concrete renderer implementations outside PhasmaCore. The runtime-facing API is intentionally small: screens can be shown/hidden and populated with prototype text, number, bool, button, image, and positioned quad widgets. Public runtime and Lua call sites do not include ImGui types.

The first concrete player backend is Dear ImGui. Its implementation lives under `PhasmaRuntime/Code/UI/Backends`, but it is compiled into `PhasmaPlayer` at the host boundary so PhasmaRuntime still exposes only backend-neutral UI APIs and the editor does not link a second ImGui renderer. The player creates that backend, sends SDL events to runtime UI before gameplay input, exposes UI capture state to runtime input bindings, and renders the UI after the render graph has produced the display target but before `BlitToSwapchain`.

The editor also hosts the same `RuntimeUiSystem` and Lua `runtime_ui.*` surface with the shared Dear ImGui runtime backend. The backend owns a separate ImGui context and renders into the editor display surface before the editor GUI pass submits its draw data, so runtime UI becomes part of the Viewport image rather than an editor overlay. Editor input maps the Viewport image rectangle back into the runtime surface coordinates before reaching runtime UI; when editor chrome is hidden, the mapping falls back to the full window. RmlUi is reserved as a future backend that can plug into the same lifecycle/event/render surface without rewriting runtime/game call sites.

## Runtime UI Helper Surface

Runtime UI is the current 2D-facing helper path. It stays backend-neutral in `PhasmaRuntime`: callers create or update named screens and generic widgets, then the active host backend draws them. The runtime UI layer does not create game-specific overlays, debug panels, or sample content by itself.

The Lua `runtime_ui` table exposes helper operations for visibility, titles, clearing/removing widgets, text, numbers, booleans, buttons, images, bool reads, button click consumption, positioned quads, frame surface size, and generic widget state reads. Image widgets are generic path-backed runtime UI resources: `runtime_ui.set_image(...)` resolves asset-relative path/resource strings, caches loaded `Image` resources in the runtime UI system, and asks the active backend to draw an engine `Image*` through its own descriptor/texture mapping. `runtime_ui.set_quad(...)` draws a positioned generic quad with optional text/image styling and can mark it draggable; `runtime_ui.get_surface_size(...)` reports the render-surface coordinate space used by quad positions and widget mouse state; `runtime_ui.get_state(...)` returns hover, click, drag, release, mouse, and drag-delta state for gameplay scripts to interpret. Editor hosts pass the scene-view image rect in window-client coordinates into the runtime UI input mapper so overlay hit tests line up with the image copied into the viewport. Runtime UI quads can also be backed by scene nodes tagged with the generic Runtime UI component; the editor can select those nodes by clicking the rendered quad and manipulate their screen-space rect with the transform gizmo. Runtime UI nodes may be hierarchy groups too: selecting a group uses the bounds of descendant runtime UI quads, so moving a parent group moves all child UI widgets through normal scene transforms. This is intentionally functional UI plumbing, not project/game logic. Dear ImGui is the active backend for now, and future UI renderers can attach behind the same lifecycle, input, and render hooks.

The engine no longer has a native sprite feature: no sprite scene component, Lua table, editor component panel, dedicated render pass, or sprite shader path is part of the runtime contract. Regular material helpers such as `material.set_texture(node, path)` remain for normal scene meshes.

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
    "project_path": "<project-root>/",
    "project_manifest": "<project-root>/phasma_project.json",
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

The launcher UI lives in `PhasmaLauncher/main.cpp`. It is a host/app layer executable that may link `PhasmaCore` plus the runtime's project/startup/settings surface, but it should not include editor/player headers or depend on editor implementation directories. It has global backend/settings/validation controls plus Editor and Player tabs. Both tabs can pick a project and startup scene; the Player tab can launch `PhasmaPlayer` or discovered `WebGPU*` sample executables. The inline settings editor loads JSON into the executable-local `phasma_settings.json` contract before launch. The validation row follows the selected backend and is a single PhasmaCore checkbox. It sets child-process validation env vars for the selected backend (`PE_VULKAN_VALIDATION` for Vulkan, `PE_DX12_DEBUG`/`PE_DX12_GBV`/`PE_DX12_DRED` for DX12).

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
- keep script-driven node deletion render-safe in editor play mode. `Scene::DeleteNode` invalidates moved-node tracking and marks raster instance/TLAS state dirty when mesh-bearing hierarchy nodes are removed, while editor `RendererSystem::LateCatchUpForScriptMutations()` performs a late scene/render-pass catch-up before command recording if scripts mutated the scene after the normal renderer system update;
- keep `SceneAccess`, `SceneHost`, and the other runtime host callback registries registered inside the image that uses them while PhasmaRuntime is a static library linked into both the editor executable and hot-reload module;
- move `Camera` into PhasmaRuntime after replacing its direct `RendererSystem`/`TAAPass` dependencies with camera runtime callbacks. PhasmaRuntime now provides the default callback table by reading the active `SceneRendererHost` display target for aspect and the runtime `TAAPass` for projection jitter;
- move `SkyBox` into PhasmaRuntime as a leaf rendering asset helper over PhasmaCore RHI/resource APIs;
- move material runtime data/reflection into PhasmaRuntime: `Material`, `MaterialInstance`, `MaterialAsset`, `PassInfoAsset`, material annotation parsing, and backend material reflection now live outside the editor tree. `PassInfoAsset` uses RapidJSON so PhasmaRuntime does not depend on MCP's bundled `nlohmann` header;
- keep Lua scalar/color material updates on the material-dirty path only. Texture assignment APIs still rebuild texture descriptors, but `material.set` for colors/floats avoids `SetTexturesDirty()` so prototype tint animation does not rebuild the material table or reflect Gbuffer shaders every frame;
- move runtime-owned data/model helpers into PhasmaRuntime: `AudioTypes`, `PhysicsTypes`, animation clip/import/evaluation code, model asset loading/primitive generation, and particle buffers/manager now live outside the editor tree;
- move scene vocabulary headers (`NodeComponents`, `SceneNode`, and `SceneNodeHandle`) plus runtime-neutral SDL window helpers into PhasmaRuntime. `WindowEvents` centralizes drawable-size, resize-event, and minimized-window checks shared by the editor host and player frame pump;
- move `Scene` implementation into PhasmaRuntime after replacing editor-only selection, animation, physics, audio, and render-descriptor calls with `SceneRuntimeHooks`. PhasmaRuntime now builds the default service hooks for animation, physics, audio, and render-descriptor refresh; the editor only layers selection hooks on top;
- rename runtime scene light metadata records from editor-specific names to `Scene*Light` types so the moved `Scene` surface reads as runtime/player data rather than editor-only state;
- add `SceneRendererHost` as the render-pass resource seam. Runtime-owned raster passes now ask the active renderer host for render targets, skyboxes, IBL LUTs, and the active `Scene` instead of reaching directly for the editor `RendererSystem`;
- move the full built-in render-pass set into PhasmaRuntime: raster passes, postprocess passes, particle passes, ray-tracing pass, editor overlay passes, and the CACAO backend now live beside the runtime renderer. Editor overlays (`GridPass`, `AabbsPass`) remain disabled by the player settings path;
- add `RuntimeSceneRenderer`, a player renderer over the moved pass set. It owns runtime render targets, skybox/IBL resources, the render graph, frame semaphores, resize handling, pass-state updates, and swapchain blits. It supports the runtime render modes/postprocess settings without editor GUI, launcher, MCP, or hot-reload module loading;
- move screenshot PNG writing and readback staging into PhasmaRuntime and wire `RuntimeSceneRenderer` to handle player screenshot requests from runtime Lua. The editor renderer uses the same runtime PNG writer and screenshot staging helper, so screenshot encoding/readback mechanics no longer depend on `PhasmaMCP` or duplicated host code;
- add runtime shader-reload handling for render-pass components. Editor `RendererSystem` and `RuntimeSceneRenderer` share the same shader refresh helper, including secondary pass infos and ray-tracing shader groups, and `PhasmaPlayer` handles runtime Lua `CompileShaders` events by refreshing matching pass shaders before clearing pipeline command caches;
- add shared scene render-graph pass creation, lifecycle init/destroy/resize loops, registration, scene-pass cache filling, scene-pass enablement, render-pass update gating, threaded render-pass updates, and scene-dependent pass binding in `PhasmaRuntime/Code/Render/SceneRenderGraph.*`. `RuntimeSceneRenderer` and editor `RendererSystem` both use it for the common Culling through Particle scene pass inventory, order, and state calculation, while the editor still appends and enables its GUI pass as an editor-owned final pass. Shared scene render target creation/destruction, default target layout, lookup, FSSampled image construction, and swapchain blitting live in `SceneRenderTargets.*`, with editor/player wrappers preserving their public host surface;
- add shared scene sky resource helpers in `SceneSky.*`. Editor and player both use the same configurable day/night skybox plus IBL BRDF LUT load/destroy path, while editor-only `m_skyBoxWhite` stays editor-owned. Day/night skybox asset paths are edited through a `Skybox` hierarchy node and serialize with that node in `.pescene`, with global scene settings kept as the renderer-facing compatibility mirror. If a selected skybox is empty or cannot load, the shared path creates a tiny solid-color cubemap so raster lighting, IBL, transmission fallback sampling, and ray tracing descriptors still have a valid environment resource;
- keep raster skybox sampling projection-aware. The light pass preserves the inverse-view-projection sky ray for perspective cameras, while orthographic cameras build a virtual FOV-scaled sky ray from the camera forward direction plus the screen right/up basis. That keeps board/prototype backgrounds stable without using orthographic camera translation as a false perspective ray or collapsing the cubemap to one flat sample;
- add shared frame command/semaphore primitives in `SceneFrameResources.*`. Editor and player both use the same helpers to wait/return frame command buffers, clean up previous/all frame command state, transition Vulkan swapchain images to present during setup, create/destroy acquire/submit semaphore vectors, and run the acquire/record/submit/present/screenshot-drain frame skeleton while each host still records its own passes and screenshot policy;
- compose the shared neutral renderer state into `SceneRendererCore`. Editor `RendererSystem` and player `RuntimeSceneRenderer` now own a core member for the render graph, scene targets, sky/IBL resources, pass components, frame command/semaphore storage, and screenshot staging, while the outer host classes still own scene lifetime, editor GUI, runtime UI placement, DX12 editor fallback, hot reload, and screenshot request/reporting policy;
- keep the CACAO vendor sources unmodified while routing CACAO Vulkan shader-module creation through a PhasmaRuntime wrapper. The wrapper patches CACAO's precompiled float storage-image SPIR-V declarations to formatless storage images so Vulkan validation accepts CACAO's compact internal image views, while integer/atomic storage images stay typed;
- remove the editor `PostProcessSystem` adapter. The editor `RendererSystem` now creates the full built-in pass set, owns pass-state updates, shader polling, resize handling, and render-graph compilation directly over runtime pass components;
- move `AnimationSystem`, `PhysicsSystem`, and `AudioSystem` into PhasmaRuntime. The shared default `SceneRuntimeHooks` let scene load/delete/component code drive animation, physics, audio, and render-descriptor refresh without duplicating system callback tables in editor and player hosts;
- add runtime physics trigger callbacks on top of Jolt sensors. `PhysicsBodyDesc::isTrigger` serializes as `is_trigger`, the editor exposes an Is Trigger checkbox, and Lua can use `physics.add_body(..., { is_trigger = true })`, `physics.on_trigger_enter`, `physics.on_trigger_exit`, and `physics.clear_trigger_callbacks`. Jolt contact callbacks only queue raw body ids from worker threads; `PhysicsSystem` drains them on the main thread, checks live `NodeId` revisions, dedupes active trigger pairs, and clears callback/overlap state on play stop or body destruction;
- move `ScriptSystem` and runtime-safe Lua bindings into PhasmaRuntime. `ScriptRuntimeHooks` provides host state and mutation callbacks for play/pause, viewport focus, and model-loading indicators; the player registers runtime play/pause state, while the editor registers GUI-backed overrides. The editor keeps GUI/profiler/selection Lua extensions in `PhasmaEditor`, while player scripts get scene/node/model/material/camera/light/animation/physics/audio/input/settings/filesystem/shader/RHI helper bindings from runtime;
- filter editor-only global Lua scripts in PhasmaRuntime before executing them. Global scripts marked `phasma: editor-only` load only when the editor host opts in through `ScriptRuntimeHooks`, so PhasmaPlayer does not execute editor shortcut scripts that depend on GUI/selection bindings. The editor also passes a stable editor assets root through the script hooks, and `ScriptSystem` loads editor-owned `Scripts/global` and `Scripts/Editor` files from that root before project assets so core editor controls do not depend on the selected project;
- keep the legacy Lua `update_editor` hook name for script compatibility, but treat it internally as an edit-mode hook rather than an editor-owned runtime path;
- add explicit Lua update registration through `script.on_update(id, fn, mode)`, `script.remove_update(id)`, and `script.clear_updates()`. Registered callbacks support play/editor/always modes, run before legacy `update`/`update_editor` hooks, and let scripts opt into runtime-owned scheduling without relying on global function discovery;
- add runtime/game UI behind a backend-neutral `RuntimeUiSystem` in PhasmaRuntime and a Dear ImGui player backend under `PhasmaRuntime/Code/UI/Backends`. PhasmaPlayer compiles that backend at the host boundary, routes SDL events through runtime UI before gameplay input, masks Lua input polling when UI captures mouse/keyboard, renders the UI after the runtime render graph and before swapchain blit, and exposes a small `runtime_ui` Lua surface for screen visibility plus simple prototype widgets;
- add `RuntimePlaySession` as the shared play-service lifecycle surface. The editor play button still owns snapshots, toolbar state, and undo reset, while runtime owns starting/stopping physics/audio play services, optional script init for player startup, pause propagation, and animation-state cleanup. Editor play enters script play mode before starting physics/audio so project `Scripts/Player` hooks can build script-driven gameplay scenes, temporarily hides editor UI so runtime UI receives full-window Player-like input, and stop exits script play mode before restoring the pre-play snapshot and editor UI. Snapshot restore re-ensures a skybox node from restored skybox settings so play-mode cleanup cannot leave the editor without its skybox;
- wire `PhasmaPlayer` to load the selected startup scene, initialize `RuntimeSceneRenderer`, initialize Lua scripts and per-node script instances, start runtime physics/audio play mode, pump SDL input into runtime input state, tick `FrameTimer`, process runtime quit/compile/resize events, and render the scene frame loop.
- keep PhasmaPlayer shutdown ordered so runtime play services, file watchers, renderer resources, and global systems stop while the scene/context are still valid; the scene is then destroyed before `Context::Remove()`.
- rename the top-level CMake project to `PhasmaEngine`, route shared Lua/CACAO/sol2/miniaudio include paths through `PE_SHARED_THIRD_PARTY_DIR`, move shared vendors under `PhasmaRuntime/third_party`, and make the executable-local asset copy target `PhasmaRuntimeAssets`. Built-in assets still physically live in their historical editor tree until a larger repository-layout move is worth the churn.

The remaining editor-owned surfaces are host/product concerns rather than player runtime behavior: editor GUI, MCP/editor-agent tooling, hot-reload/module mechanics, `Window`, `SelectionManager`, and editor-only Lua GUI/profiler/selection bindings. Launcher UI is its own host/product concern under `PhasmaLauncher/`, with only Core plus runtime config/project/startup reach.
