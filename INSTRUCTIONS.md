# PhasmaEngine — Instructions

Subdirectory `INSTRUCTIONS.md` files cascade on top of this root.

## Where to find what

- **Code is the source of truth.** Derive from the actual files — do not paste these into docs:
  - Build commands → `CMakeLists.txt`
  - CMake targets, options, dependencies → `CMakeLists.txt` (root + per-subdirectory)
  - Boot sequence → `PhasmaEditor/Code/App/App.cpp`
  - Active render passes + `AddPass` signature → `PhasmaEditor/Code/Systems/RendererSystem.cpp` (`BuildRenderGraph`)
  - Singletons (`RHII`, `Context`, `EventSystem`) → headers under `PhasmaCore/Code/`
  - GPU material layout → `PhasmaEditor/Assets/Shaders/Common/Structures.hlsl`
  - Physics timestep / API → `PhasmaEditor/Code/Systems/PhysicsSystem.cpp`
  - PE_API export sites → `grep PE_API PhasmaCore/Code/`

- **Non-obvious rules and gotchas live in MemPalace.** Search with `mempalace_search` for things like: PCH `<vector>` trap, GPU resource wrappers (`Buffer`/`Image`/`Sampler`), perf regression thresholds, hot-reload safe-window / ImGui forwarding, scripting MCP caveat (`ScriptSystem*` caching), `MaterialGpuData` 80-byte layout + IOR/transmission flush rule.

- **Recent state and session history** → `mempalace_kg_timeline`, `mempalace_diary_read`.

- **Synthesis pages** (architecture overviews, testing playbooks, pitfalls) → `docs/wiki/index.md`. Find with `bash docs/wiki/tools/search.sh "query"`.

## Wiki maintenance

Before finishing a session that changed code a wiki page describes, update the page and run `bash docs/wiki/tools/lint.sh`. Update `docs/wiki/index.md` and `docs/wiki/log.md` when pages are added or renamed.
