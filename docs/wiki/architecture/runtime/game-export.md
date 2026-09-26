# Game export

`PhasmaExport` creates a standalone desktop game directory from any manifest project:

```powershell
PhasmaExport.exe --project C:\path\to\MyProject --output C:\path\to\MyGame [--force]
```

The export directory contains only the player binary, its runtime DLLs, `phasma_project.json`, `phasma_settings.json`, `game.pepak`, and two empty anchor directories (`Assets/`, `RuntimeAssets/`) that `Path` resolution and runtime saves require. Every project asset and the whole engine `RuntimeAssets/` tree are written into the pack; Lua files are compiled to bytecode by the same vendored LuaJIT 2.1 VM used by the editor and player, so the export contains no loose `.lua` source. `Assets/Save`, `Assets/Agent`, `RuntimeAssets/Scripts/tests`, editor scripts, test scripts, and scripts marked `phasma: editor-only` are excluded by default. Saves remain ordinary writable files created below `Assets/Save` at runtime, and the shader bytecode cache is built beside the executable on first run.

A project can add `.phasmaexportignore` at its root. Each non-comment line is a project-relative file or directory prefix such as `Assets/Skyboxes`; absolute paths and parent traversal are rejected. The exporter builds in a temporary sibling directory, verifies every packed entry after writing, and refuses to replace an existing output unless `--force` is present.

When `PhasmaPlayer` finds `game.pepak` beside the executable, it verifies the pack before startup, serves all managed asset reads from it, rejects loose overrides and writes into packed namespaces, and disables development file watchers. The read path is `FileSystem` in `Base/` (plus `AssetFileExists` for existence probes): a read-only open of a pack-managed path is served from pack memory, everything else falls through to disk, so the editor and a pack-less player behave exactly as before. Shader compilation reads HLSL and its includes through the same seam (`ShaderCache::ParseShader` inlines includes), audio decodes through a custom miniaudio VFS, and UI fonts load via `AddFontFromMemoryTTF`. Game code that re-reads packed `.lua` through `fs.read` + `load` must pass chunk mode `"bt"` (packed scripts are bytecode); keep `"t"` for loading runtime-written saves so a tampered save cannot inject bytecode. Voxel column-chunk stores stay loose on disk — they are runtime-mutable world state, not shipped assets. The pack checksum detects corruption and casual edits; it is not cryptographic signing or DRM.

