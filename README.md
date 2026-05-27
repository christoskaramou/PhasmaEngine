# PhasmaEngine

PhasmaEngine is a Vulkan and DirectX 12 3D engine for learning graphics techniques, with an experimental WebGPU C API layer. Vulkan runs on Windows and Linux; DX12 is Windows-only.

![Screenshot](PhasmaEditor/EditorAssets/Images/ABeautifulGame.png)

## Features

### Rendering
* Deferred Rendering
* Ray Tracing (Vulkan backend)
* Hybrid RT (Transparency/Transmission/Refraction, Vulkan backend)
* Physically Based Rendering (PBR)
* Image Based Lighting (IBL)
* Screen Space Ambient Occlusion (SSAO)
* Screen Space Reflections (SSR)
* Cascaded Shadow Maps
* Bloom
* Depth of Field
* Motion Blur
* Tone Mapping
* FXAA
* Temporal Anti-Aliasing with RCAS Upscale
* Frustum Culling
* Render Graph
* Day/Night Skybox with HDR environment maps

### Shaders
* HLSL shaders compiled via DXC/shaderc
* Experimental WGSL support through PhasmaWebGPU
* Shader hot-reload with file watching

### Scene
* Multiple model formats loading (via Assimp)
* Mesh optimization (via meshoptimizer)
* Directional, Point, Spot, and Area lights
* Scene save/load (.pescene)
* Particle system
* Per-axis transforms and material editing

### Editor (PhasmaEditor)
* ImGui-based editor
* CPU/GPU metrics
* Debug console
* Lua scripting
* Event system

### MCP Integration

PhasmaEngine has no in-engine LLM. Instead, PhasmaEditor runs an in-process MCP (Model Context Protocol) HTTP server at `http://127.0.0.1:8765/mcp`, letting **external** AI clients control the engine directly.

**Supported clients:**
- **Claude Code** — add `.claude/settings.json`:
  ```json
  { "mcpServers": { "phasmaeditor": { "type": "http", "url": "http://127.0.0.1:8765/mcp" } } }
  ```
- **Claude Desktop** — use `mcp-remote` bridge in `claude_desktop_config.json`:
  ```json
  "phasmaeditor": { "command": "npx", "args": ["-y", "mcp-remote", "http://127.0.0.1:8765/mcp"] }
  ```

Toggle the server from **Connection → MCP Server** in the editor. The **Connection → RAG** submenu controls codebase indexing. Status bar shows live MCP and indexing state.

**Available MCP tools:** scene/model management, camera, lights, materials, shaders, Lua execution, screenshots, mouse injection, codebase search (BM25), file read/write, and more. See [PhasmaMCP README](PhasmaMCP/README.md) for the underlying library.

## Building and Compiling

PhasmaEngine uses CMake to configure and generate project files. The CMakeLists.txt is in the root folder.

## Graphics API

PhasmaEditor supports runtime graphics API selection:

- Vulkan (default, Windows/Linux): `PhasmaEditor --api vulkan`
- DirectX 12 (Windows-only): `PhasmaEditor.exe --api dx12`

You can also set `PHASMA_API` to `vulkan` or `dx12`, or write `graphics_api` / `api` in `phasma_settings.json` next to the executable.

WebGPU support is experimental and lives in `PhasmaWebGPU`, a WebGPU C API layer over PhasmaCore. It builds with the `PE_WEBGPU` CMake option and runs through dedicated WebGPU sample/test executables rather than PhasmaEditor `--api` selection.

## GitHub Release Builds

The `latest` GitHub prerelease publishes prebuilt packages from the same commit:

- `PhasmaEngine-Windows-Vulkan.zip` launches `PhasmaEditor.exe --api vulkan`
- `PhasmaEngine-Windows-DX12.zip` launches `PhasmaEditor.exe --api dx12`
- `PhasmaEngine-Linux-Vulkan.tar.gz` includes `Launch-PhasmaEditor-Vulkan.sh`, which runs `./PhasmaEditor --api vulkan`

The Windows packages contain the same runtime files; the included launcher script selects the graphics backend. DX12 is Windows-only, so the Linux package ships the Vulkan backend.

**CMake:**
Download [CMake](https://cmake.org/download/) and install it.
See [CMake documentation](https://cmake.org/runningcmake/) for more information.
