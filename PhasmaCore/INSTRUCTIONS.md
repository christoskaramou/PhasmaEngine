# PhasmaCore — Instructions

Supplements the root `INSTRUCTIONS.md`. Read that file first.

PhasmaCore is a **static library** — no `main()`, no ImGui, no editor-specific code.
Every `.cpp` here automatically gets `PhasmaCore/pch/PhasmaPch.h` as a precompiled header.

---

## Directory Map

```
PhasmaCore/Code/
├── API/
│   ├── RHI.h / RHI.cpp          # RHII singleton — Vulkan instance, device, queues, VMA
│   ├── Buffer.h/.cpp             # GPU buffer wrapper (VMA)
│   ├── Image.h/.cpp              # GPU image + view wrapper (VMA)
│   ├── Sampler.h/.cpp            # Vulkan sampler wrapper
│   ├── Command.h/.cpp            # CommandBuffer recording API
│   ├── Queue.h/.cpp              # Queue submit / present
│   ├── Pipeline.h/.cpp           # Graphics / compute pipeline state
│   ├── Vertex.h                  # 88-byte vertex struct + FillVertex* helpers
│   ├── Shader.h/.cpp             # SPIR-V shader module wrapper
│   └── Descriptor.h/.cpp         # Descriptor set layout + pool management
├── ECS/
│   ├── Context.h/.cpp            # Global system registry + entity factory
│   ├── System.h                  # ISystem / IDrawSystem base classes
│   ├── Component.h               # IComponent + IRenderPassComponent
│   ├── EventSystem.h/.cpp        # Pub-sub event bus (std::any payloads)
│   └── OrderedMap.h              # Deterministic-iteration map
├── Base/
│   ├── Path.h/.cpp               # Path::Assets
│   ├── Log.h/.cpp                # PE_INFO / PE_WARN / PE_ERROR macros
│   ├── FileWatcher.h/.cpp        # File change detection → EventSystem
│   └── Timer.h                   # FrameTimer, high-resolution timing
└── Physics/
    ├── PhysicsSystem.h/.cpp      # Jolt Physics integration (ISystem)
    ├── PhysicsBodyComponent.h/.cpp
    ├── PhysicsLayers.h           # Object layers + broadphase mapping
    └── PhysicsUtils.h            # GLM ↔ Jolt coordinate conversion
```

---

## RHII — Vulkan Core

`RHII` is the global Vulkan interface. Access it anywhere via the `RHII` macro.

```cpp
// Allocating GPU resources
Buffer* buf = Buffer::Create(size, usage, memoryUsage);
Image*  img = Image::Create(width, height, format, usage);

// Command recording
Queue* queue = RHII.GetMainQueue();
CommandBuffer* cmd = queue->AcquireCommandBuffer();
cmd->Begin();
// ... record commands ...
cmd->End();
queue->Submit(1, &cmd, nullptr, nullptr);
cmd->Wait();
cmd->Return();
```

Memory management is VMA-backed. Never call `vkAllocateMemory` directly.

---

## ECS Patterns

```cpp
// Creating a system (in App.cpp)
auto* sys = Context::CreateGlobalSystem<MySystem>();

// Getting a component
auto* comp = entity->GetComponent<MyComponent>();

// Publishing an event
EventSystem::Push(EventType::CompileShaders, {});

// Subscribing to an event (in system Init)
EventSystem::Subscribe(EventType::CompileShaders, [this](const std::any&) {
    // handle event
});
```

---

## Physics (Jolt v5.2.0)

`PhysicsSystem` runs fixed-timestep simulation. Bodies are managed per-node:

```cpp
PhysicsSystem::AddBodyToNode(model, nodeIndex, bodyType);
PhysicsSystem::RemoveBodyFromNode(model, nodeIndex);
```

Coordinate conversion: Jolt uses right-handed Y-up; the engine uses left-handed Y-up.
Always go through `PhysicsUtils` conversion helpers — never convert manually.

---

## PCH — What Is Already Included

Do NOT add includes for any of these in `.cpp` files under PhasmaCore/:

```
<vector> <string> <map> <unordered_map> <set> <unordered_set>
<memory> <functional> <optional> <variant> <any>
<mutex> <shared_mutex> <thread> <atomic> <condition_variable>
<future> <chrono> <filesystem> <fstream> <sstream> <iostream>
<algorithm> <numeric> <cmath> <cstring> <cassert> <regex>
<span> <array> <queue> <deque> <bitset> <tuple>
```

Only add includes for project headers or third-party libraries not covered by the PCH.

---

## Adding a New System

1. Create `MySystem.h` / `MySystem.cpp` in the appropriate `Code/` subdirectory
2. Derive from `ISystem` or `IDrawSystem` in `ECS/System.h`
3. Implement `Init()`, `Update(float dt)`, `Destroy()` (and `Draw()` for `IDrawSystem`)
4. Register in `App.cpp`: `Context::CreateGlobalSystem<MySystem>()`
5. Systems are called automatically via `UpdateGlobalSystems()` / `DrawGlobalSystems()`

---

## Rules Specific to PhasmaCore

- No ImGui includes or calls — PhasmaCore is platform-agnostic
- No editor-specific logic — that belongs in PhasmaEditor/
- No direct file I/O for assets outside of `Path::Assets` and `FileWatcher`
- All GPU resource creation must go through the wrappers, not raw Vulkan handles
