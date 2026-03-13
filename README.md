# PhasmaEngine

PhasmaEngine is a Vulkan 3D engine for learning graphics techniques. Runs on Windows and Linux.

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
* C++ scripting
* Event system

### AI Agent (PhasmaAgent)
* Standalone, provider-agnostic AI agent library (C++20, no engine dependencies)
* Supports Anthropic, OpenAI, Gemini, and Ollama (local)
* 35+ tools for scene manipulation, camera control, lighting, materials, shaders, and rendering
* Persistent agent workspace (`Assets/Agent/`) with automatic startup instructions
* Streaming responses with thinking/reasoning display
* Agentic tool-use loop with configurable round limits

See [PhasmaAgent README](PhasmaAgent/README.md) for setup and provider configuration.

## Building and Compiling

PhasmaEngine uses CMake to configure and generate project files. The CMakeLists.txt is in the root folder.

**CMake:**
Download [CMake](https://cmake.org/download/) and install it.
See [CMake documentation](https://cmake.org/runningcmake/) for more information.
