# PhasmaCore — Instructions

Supplements the root `INSTRUCTIONS.md`. PhasmaCore is the **shared library** (`PhasmaCore.dll`) — Vulkan RHI, ECS, base utilities. Editor UI, scene-asset tooling, and host-specific logic do not belong here.

For Vulkan resource creation, ECS access patterns, boot sequence — read the actual headers under `Code/API/`, `Code/ECS/`, `Code/Base/`. They're the source of truth and don't rot.

## Rules specific to PhasmaCore

- **No ImGui** includes or calls — PhasmaCore is platform-agnostic.
- **No editor-specific logic** — that belongs in host/editor layers.
- **No implicit asset roots or sibling-project assumptions** — hosts prepare/set runtime assets; core APIs take explicit paths, memory, or bytecode.
- **All GPU resource creation goes through the wrappers** in `Code/API/` (`Buffer`, `Image`, `Sampler`) — never raw `VkBuffer` / `VkImage` / `VkDeviceMemory`.
