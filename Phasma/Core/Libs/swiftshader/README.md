# SwiftShader (bundled CPU / software Vulkan ICD)

This directory vendors a prebuilt **SwiftShader** Vulkan ICD so the launcher's
`CPU / Software` GPU-adapter preference works out of the box, without scavenging
another application's copy at runtime.

- `vk_swiftshader.dll` — SwiftShader's CPU Vulkan driver.
- `vk_swiftshader_icd.json` — ICD manifest. `library_path` is `.\vk_swiftshader.dll`
  (relative to this manifest), so the loader finds the DLL beside it.

## How it is used

CMake copies both files into `<exe-dir>/swiftshader/` next to `PhasmaEditor` /
`PhasmaPlayer` / `PhasmaLauncher` (Windows only; see the `PhasmaSwiftShader`
target in the root `CMakeLists.txt`). At startup, when the resolved GPU adapter
preference is `cpu`, `RHI::ConfigureVulkanSoftwareIcd` adds this manifest via
`VK_ADD_DRIVER_FILES` before the Vulkan instance is created. The discovery order
also honors `PHASMA_VULKAN_CPU_ICD` (explicit override) and a copy placed directly
beside the executable.

Only the four shader capabilities SwiftShader lacks (`shaderInt16`, `shaderInt64`,
`shaderFloat16`, `shaderStorageBufferArrayNonUniformIndexing`) are softened to
warnings for CPU adapters; every hard-required engine feature
(`bufferDeviceAddress`, descriptor indexing, `multiDrawIndirect`, etc.) is
supported by this build.

## Provenance & license

This binary is the SwiftShader build redistributed inside Google Chrome
(version 149). SwiftShader is open source under the **Apache License 2.0**
(https://github.com/google/swiftshader). It may be freely replaced with an
official SwiftShader build; keep the file names and the relative `library_path`
in the manifest unchanged.

This is a development / CI / conformance aid — CPU rendering is 10–100× slower
than a real GPU and is **not** intended to ship in a released player.
