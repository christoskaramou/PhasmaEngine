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

### AI Agent (PhasmaAgent)
* Standalone, provider-agnostic AI agent library (C++20, no engine dependencies)
* Supports Anthropic, OpenAI, Gemini, Ollama, and LM Studio (local)
* Codebase RAG indexing with hybrid BM25 + semantic vector search
* Streaming responses with thinking/reasoning display
* Agentic tool-use loop with configurable round limits

See [PhasmaAgent README](PhasmaAgent/README.md) for setup and provider configuration.

### MCP Integration

PhasmaEditor runs an in-process MCP (Model Context Protocol) HTTP server at `http://127.0.0.1:8765/mcp`, letting external AI clients control the engine directly.

**Supported clients:**
- **Claude Code** — add `.claude/settings.json`:
  ```json
  { "mcpServers": { "phasmaeditor": { "type": "http", "url": "http://127.0.0.1:8765/mcp" } } }
  ```
- **Claude Desktop** — use `mcp-remote` bridge in `claude_desktop_config.json`:
  ```json
  "phasmaeditor": { "command": "npx", "args": ["-y", "mcp-remote", "http://127.0.0.1:8765/mcp"] }
  ```

Toggle the server from **Connection → MCP Server** in the editor. The **Connection → RAG** submenu controls codebase indexing and embedding provider selection. Status bar shows live MCP and RAG state.

**Available MCP tools:** scene/model management, camera, lights, materials, shaders, Lua execution, screenshots, mouse injection, codebase search (BM25 + vector), file read/write, and more. See [PhasmaAgent README](PhasmaAgent/README.md) for the full tool list.

## Building and Compiling

PhasmaEngine uses CMake to configure and generate project files. The CMakeLists.txt is in the root folder.

**CMake:**
Download [CMake](https://cmake.org/download/) and install it.
See [CMake documentation](https://cmake.org/runningcmake/) for more information.
