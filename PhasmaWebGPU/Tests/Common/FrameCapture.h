#pragma once

#include <webgpu/webgpu.h>

namespace pwgpu::test
{
    uint32_t AlignTo(uint32_t value, uint32_t alignment);

    bool ReadTextureRgba8(WGPUInstance instance,
                          WGPUDevice device,
                          WGPUQueue queue,
                          WGPUTexture texture,
                          uint32_t width,
                          uint32_t height,
                          std::vector<uint8_t> &outPixels);

    bool DumpTextureToBmp(WGPUInstance instance,
                          WGPUDevice device,
                          WGPUQueue queue,
                          WGPUTexture texture,
                          WGPUTextureFormat format,
                          uint32_t width,
                          uint32_t height,
                          const std::string &outputPath);
} // namespace pwgpu::test
