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
* Lua scripting API for scene, camera, lights, materials, shaders, particles, and more
* Codebase RAG indexing with semantic vector search (local or Google/OpenAI embeddings)
* Screenshot capture — agent can take and view engine screenshots
* Persistent agent workspace (`Assets/Agent/`) with automatic startup instructions
* Streaming responses with thinking/reasoning display
* Agentic tool-use loop with configurable round limits

See [PhasmaAgent README](PhasmaAgent/README.md) for setup and provider configuration.

### External AI (File-based IPC)

The editor includes a file-based IPC system that allows any external AI tool (Claude Code, Cursor, Copilot, custom scripts, etc.) to control the engine without using the built-in agent library. External is the default provider.

#### Chat Communication

The editor communicates through plain text files in `Assets/Agent/`:

| File | Purpose |
|------|---------|
| `chat_input.txt` | Editor writes the user's message here |
| `chat_input_response.txt` | External tool writes its response here |
| `chat_history.txt` | Full conversation history (updated after each turn) |

The input filename is editable in the UI. Renaming it (e.g. to `my_agent.txt`) will automatically derive the response file (`my_agent_response.txt`) and history file. Subdirectories are supported (e.g. `project/chat.txt`).

The response file is monitored via file watcher -- writing to it immediately displays the response in the editor chat.

#### Script Execution

External tools can execute Lua scripts in the engine via a second set of IPC files:

| File | Purpose |
|------|---------|
| `command.lua` | Write Lua code here |
| `command.run` | Write anything to this file to trigger execution |
| `result.txt` | Engine writes the script output/errors here |

The full Lua API is documented in `Assets/Agent/START.md` and includes: scene management, camera control, model loading/manipulation, lighting, materials, shaders, particles, settings, and filesystem access.

#### Automatic Responses

When using the External provider through the editor chat, an automated polling mechanism is required for the external tool to detect and respond to messages. Without automation, the external tool needs manual notification. Options:

- **Claude Code**: use the `/loop` command to poll for new input (e.g. `/loop 2s read Assets/Agent/chat_input.txt -- if it has new content, read chat_history.txt for context, then write your response to chat_input_response.txt`)
- **Custom script**: use `inotifywait` (Linux) or a polling loop (1-2 seconds) to watch `chat_input.txt` for changes, then write the response to the corresponding response file
- **Manual**: simply read `chat_input.txt` and write to the response file on demand

#### Autonomous Agent Example

The chat UI is optional. An external agent can operate autonomously using only the script execution files -- no editor interaction needed. The agent writes Lua code, triggers execution, and reads results:

1. Write Lua code to `command.lua`
2. Write anything to `command.run` to trigger execution
3. Read the output or errors from `result.txt`
4. Iterate or chain further operations

```bash
# Example: query the scene from an external script
echo 'return scene.get_model_count()' > Assets/Agent/command.lua
echo 1 > Assets/Agent/command.run
sleep 0.1
cat Assets/Agent/result.txt

# Example: load a model and position it
cat > Assets/Agent/command.lua << 'LUA'
local m = load_model("Objects/glTF-Sample-Models/Duck/glTF/Duck.gltf")
m:set_position(vec3.new(0, 1, 0))
return "loaded: " .. m:get_label()
LUA
echo 1 > Assets/Agent/command.run
```

The full Lua API is documented in `Assets/Agent/START.md`.

## Building and Compiling

PhasmaEngine uses CMake to configure and generate project files. The CMakeLists.txt is in the root folder.

**CMake:**
Download [CMake](https://cmake.org/download/) and install it.
See [CMake documentation](https://cmake.org/runningcmake/) for more information.
