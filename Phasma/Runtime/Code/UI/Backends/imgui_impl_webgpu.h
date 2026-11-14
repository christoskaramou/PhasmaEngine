#pragma once

#if defined(PE_WEB)

#include "imgui.h"

#include <webgpu/webgpu.h>

// Dear ImGui renderer for the browser backend. Its shader is inline WGSL, so it needs no baked module.
bool ImGui_ImplWebGpu_Init(WGPUDevice device, WGPUQueue queue, WGPUTextureFormat targetFormat);
void ImGui_ImplWebGpu_Shutdown();
void ImGui_ImplWebGpu_CreateFontsTexture();
ImTextureID ImGui_ImplWebGpu_AddTexture(WGPUTextureView view);
void ImGui_ImplWebGpu_RemoveTexture(ImTextureID texture);
void ImGui_ImplWebGpu_RenderDrawData(ImDrawData *drawData, WGPURenderPassEncoder pass, uint32_t width, uint32_t height);

#endif
