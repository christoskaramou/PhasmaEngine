# Runtime And Project Boundary

PhasmaRuntime is the future shared runtime layer between PhasmaCore and PhasmaEditor. It owns project/runtime contracts that should be identical for the editor and a standalone player. It should not own editor UI, hot-reload UI, launcher UI, ImGui panels, or module-reload mechanics.

## Layer Shape

- `PhasmaCore` remains the low-level engine foundation: RHI, ECS, platform-adjacent services, paths, settings, and shared primitives.
- `PhasmaRuntime` sits above PhasmaCore and below hosts. It defines how a project is described, how a runtime session resolves project-relative paths, and later where play/player lifecycle code lives.
- `PhasmaEditor` remains the desktop editor concept. `PhasmaEditorModule` is the current hot-reload DLL implementation detail, not the product/layer name.
- Future player executables should depend on PhasmaRuntime instead of copying editor startup logic.

## MyProject Contract

The project contract starts as a small descriptor:

- project name, usually `MyProject` while local project generation is still simple;
- project root directory;
- manifest path, defaulting to `phasma_project.json` under the project root;
- assets directory relative to the project root, defaulting to `Assets`;
- startup scene path relative to the project root.

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

Startup scene precedence is explicit launch setting first, then editor restore, then project manifest fallback:

- `phasma_settings.json:startup_scene` wins when it is non-empty, so a launcher-selected scene cannot be overwritten by stale editor restore state;
- an existing `phasma_settings.json:startup_scene` key with an empty value is an explicit "no startup scene" selection and suppresses editor/manifest fallback;
- `Assets/editor_config.json:last_scene` is the editor restore fallback;
- `phasma_project.json:startup_scene` is used when neither runtime settings nor editor restore selected a scene.

Successful editor scene loads update both `editor_config.json:last_scene` and `phasma_settings.json:startup_scene` so the launcher, editor restore, and runtime host do not drift. Creating a new scene clears both values.

Path resolution rules:

- absolute paths are preserved and normalized;
- project paths resolve against the project root;
- asset paths resolve against the project assets root;
- startup scene paths resolve against the project root, so `Assets/Scenes/foo.pescene` is valid and explicit.

## First Implementation Slice

The first code slice is intentionally boring:

- add a `PhasmaRuntime` static library target;
- add `ProjectConfig` and `RuntimeContext` types;
- load and write `ProjectConfig` from the `phasma_project.json` manifest;
- resolve active projects from `phasma_settings.json`;
- read and write `startup_scene` in `phasma_settings.json`;
- persist `project_manifest` from the launcher when a selected project has a manifest;
- let the editor resolve the active project at startup and use the manifest startup scene only when neither explicit runtime settings nor editor restore selected a scene;
- add the first standalone `PhasmaPlayer` host over PhasmaRuntime. `PhasmaPlayer` is now a thin executable entrypoint; PhasmaRuntime owns SDL/window/RHI lifetime, resolves the active project, validates the manifest startup scene path, initializes the swapchain, and runs a continuous clear/present frame pump. It does not yet own scene loading or renderer/system startup;
- link the editor module to `PhasmaRuntime`;
- do not move editor startup, renderer, GUI, or script behavior yet.

The next slice should move enough non-editor scene/runtime startup behind PhasmaRuntime for `PhasmaPlayer` to replace the clear/present frame with manifest startup-scene loading and rendering without depending on editor GUI, launcher, or hot-reload behavior.
