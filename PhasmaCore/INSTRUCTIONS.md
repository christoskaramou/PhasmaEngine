# PhasmaCore — Instructions

Supplements the root `INSTRUCTIONS.md`. PhasmaCore is the **shared library** (`PhasmaCore.dll`) — Vulkan RHI, ECS, base utilities. Editor / ImGui / scene logic does not belong here.

For Vulkan resource creation, ECS access patterns, boot sequence — read the actual headers under `Code/API/`, `Code/ECS/`, `Code/Base/`. They're the source of truth and don't rot.

## Rules specific to PhasmaCore

- **No ImGui** includes or calls — PhasmaCore is platform-agnostic.
- **No editor-specific logic** — that belongs in `PhasmaEditor/`.
- **No direct asset file I/O** outside of `Path::Assets` and `FileWatcher`.
- **All GPU resource creation goes through the wrappers** in `Code/API/` (`Buffer`, `Image`, `Sampler`) — never raw `VkBuffer` / `VkImage` / `VkDeviceMemory`.
