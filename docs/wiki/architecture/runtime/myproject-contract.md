# MyProject Contract

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

Startup scene precedence is explicit launch setting first, then editor restore, then project manifest fallback. **PhasmaPlayer skips the first two** and always loads `phasma_project.json:startup_scene`, so a play session cannot resume last night's map instead of the title scene.

Editor / launcher still use:

- `phasma_settings.json:startup_scene` wins when it is non-empty, so a launcher-selected scene cannot be overwritten by stale editor restore state;
- an existing `phasma_settings.json:startup_scene` key with an empty value is an explicit "no startup scene" selection and suppresses editor/manifest fallback;
- `Assets/editor_config.json:last_scene` is the editor restore fallback;
- `Assets/editor_config.json:gui_style` and `font_scale` restore the editor Layout menu (Style, Font Size) on startup;
- `phasma_project.json:startup_scene` is used when neither runtime settings nor editor restore selected a scene.

Successful editor scene loads update both `editor_config.json:last_scene` and `phasma_settings.json:startup_scene` so the launcher, editor restore, and runtime host do not drift. Creating a new scene clears both values.

Present-mode startup uses the same runtime startup surface. `PE_PRESENT_MODE` wins first, then `Assets/editor_config.json:present_mode`, then the startup scene's saved `settings.present_mode`, then FIFO. PhasmaLauncher can write that editor-config override or clear it with `Default`; the player applies the effective mode before `RuntimeSceneRenderer` creates frame resources. If a backend has already created a provisional swapchain during RHI init, the player recreates it through the normal present-mode path before renderer initialization. DX12 always requests three backbuffers so FIFO can absorb a missed presentation boundary and live present-mode changes cannot alter the cardinality of per-frame scene/pass resources; Vulkan keeps the two-buffer request (subject to the surface's actual image count). Scene `settings.render_scale` is clamped to 0.1-1.0 and applied before startup render targets are created, and editor/player renderers track the render scale used for their current scene targets so a manual scene load or Lua scale change can rebuild targets before the same frame records postprocess image copies. The standalone player treats this user-facing Quality value as whole-frame resolution: scene passes, post-processing, particles, Runtime UI, and screenshots share the scaled display target before one final blit to the native swapchain. Runtime UI retains native logical coordinates and maps its draw data to that target, so layout and input do not change with Quality. A live Quality change first reaches a device-idle safe point, then rebuilds only scene targets and pass bindings; it leaves the window surface, swapchain, and presentation frame resources intact. The editor keeps its own GUI output native while its scene targets follow the same setting.

The launcher stores `project_path` as the project root when a manifest project is detected. It uses the project's configured assets root for startup-scene discovery and browsing. Legacy projects without a manifest may still use the assets directory itself as `project_path`.

The launcher UI lives in `Phasma/Launcher/main.cpp`. It is a host/app layer executable that may link `PhasmaCore` plus the runtime's project/startup/settings surface, but it should not include editor/player headers or depend on editor implementation directories. On WSL only, before `SDL_Init`, the launcher prefers X11 + GLX + the OpenGL renderer (`SDL_VIDEODRIVER=x11`, `SDL_VIDEO_X11_FORCE_EGL=0`, `SDL_RENDER_DRIVER=opengl`) so Mesa does not take the DRI3/EGL path that needs `/dev/dri`. Vulkan hosts (`RuntimeSdlSession`: editor, player, animator) do not take those hints — Mesa Dozen on WSLg presents through the Wayland `wl_surface`. Shown windows are not created `HIDDEN` on WSL, and WSL creation size is clamped (no maximize on a spanned 5120-wide display). If `/mnt/shared_memory` is missing, WSLg is in COPY MODE (`[WARN:COPY MODE]` on the Windows title, taskbar ghost, blank preview); that is a Weston/shared-memory failure, not a Phasma window-flag bug — from Windows run `wsl --shutdown`. Native Linux, Windows, and Android are unchanged; already-set env vars still win. It has global backend/settings/validation/display/present-mode/GPU-adapter controls plus Editor and Player tabs. Both tabs can pick a project and startup scene; the Player tab can launch `PhasmaPlayer` or discovered `WebGPU*` sample executables. The inline settings editor loads JSON into the executable-local `phasma_settings.json` contract before launch. Linux builds provide an in-launcher file browser for project, startup-scene, and settings selection so packaged builds do not require `zenity` for the Open buttons. The GPU adapter selector writes `gpu_adapter_preference` (`auto`, `integrated`, `discrete`, or `cpu`) and sets `PHASMA_GPU_ADAPTER` for the launched child process; Vulkan and DX12 honor that class preference when choosing a physical device or adapter, then fall back to automatic selection if no matching GPU is usable. The `cpu` class uses a software renderer: DX12 selects the built-in WARP adapter, while Vulkan loads a SwiftShader ICD bundled next to the executable under `swiftshader/` (vendored from `Phasma/Core/Libs/swiftshader/` by the `PhasmaSwiftShader` CMake target, with `PHASMA_VULKAN_CPU_ICD` as an override). Software rendering is a development/CI/conformance aid, not a shipping path. The validation row follows the selected backend and is a single PhasmaCore checkbox. It sets child-process validation env vars for the selected backend (`PE_VULKAN_VALIDATION` for Vulkan, `PE_DX12_DEBUG`/`PE_DX12_GBV`/`PE_DX12_DRED` for DX12).

Path resolution rules:

- absolute paths are preserved and normalized;
- project paths resolve against the project root;
- asset paths resolve against the project assets root;
- startup scene paths resolve against the project root, so `Assets/Scenes/foo.pescene` is valid and explicit.
- runtime/editor startup-scene settings first check the path as written, then next to the executable, then under the runtime assets root.

After resolving the active project, editor and player hosts apply the selected assets root to `Path::Assets` before registering file watchers, resolving startup scenes, loading scripts, or creating render resources. Manifest projects use the manifest `assets` directory; legacy no-manifest projects keep treating the selected `project_path` as the assets root.

