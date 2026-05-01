# PhasmaEngine

PhasmaEngine is a Vulkan 3D engine for learning graphics techniques. Runs on Windows and Linux

![Screenshot](PhasmaEditor/Images/ABeautifulGame.png)

## Features

### Rendering
* Deferred Rendering
* Ray Tracing
* Hybrid RT (Transparency/Transmission/Refraction)
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

**CMake:**
Download [CMake](https://cmake.org/download/) and install it.
See [CMake documentation](https://cmake.org/runningcmake/) for more information.
