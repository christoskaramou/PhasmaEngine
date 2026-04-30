#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <webgpu/webgpu.h>

namespace pwgpu::test
{
    std::string GetExecutableDir(const char *argv0);
    std::string GetShaderDir(const std::string &exeDir, const char *sampleName);
    std::string GetAssetPath(const std::string &exeDir, const char *assetName);

    void DefaultUncapturedErrorCallback(WGPUDevice const *,
                                        WGPUErrorType type,
                                        WGPUStringView message,
                                        void *,
                                        void *);

    WGPUTextureFormat ChooseSurfaceFormat(const WGPUSurfaceCapabilities &caps);
    WGPUPresentMode ChoosePresentMode(const WGPUSurfaceCapabilities &caps);
    const char *PresentModeName(WGPUPresentMode mode);
    const char *SurfaceTextureStatusName(WGPUSurfaceGetCurrentTextureStatus status);
    bool IsSurfaceTextureStatusOk(WGPUSurfaceGetCurrentTextureStatus status);
    WGPUBuffer CreateBuffer(WGPUDevice device,
                            uint64_t size,
                            WGPUBufferUsage usage,
                            const char *label);
    void UploadRGBA8Texture(WGPUQueue queue,
                            WGPUTexture texture,
                            const uint8_t *data,
                            uint32_t width,
                            uint32_t height,
                            uint32_t mipLevel = 0,
                            uint32_t arrayLayer = 0);
    WGPUTextureView CreateTextureView(WGPUTexture texture, WGPUTextureFormat format);

    WGPUSurfaceConfiguration MakeSurfaceConfig(WGPUDevice device,
                                               WGPUTextureFormat format,
                                               WGPUTextureUsage usage,
                                               uint32_t width,
                                               uint32_t height,
                                               WGPUPresentMode presentMode,
                                               WGPUCompositeAlphaMode alphaMode,
                                               const WGPUTextureFormat *viewFormats = nullptr,
                                               size_t viewFormatCount = 0);
} // namespace pwgpu::test
