#pragma once

#include <webgpu/webgpu.h>
#include "API/Image.h"
#include "API/Vulkan/VulkanHeaders.h"

namespace pe
{
    class CommandBuffer;
}

struct WGPUDeviceImpl;
struct WGPUTextureViewImpl;

struct WGPUTextureImpl
{
    std::atomic<uint32_t> refCount{1};
    std::string label;
    pe::Image *image = nullptr;
    WGPUDeviceImpl *device = nullptr;
    WGPUTextureFormat format = WGPUTextureFormat_Undefined;
    WGPUTextureUsage usage = WGPUTextureUsage_None;
    WGPUTextureDimension dimension = WGPUTextureDimension_2D;
    WGPUTextureViewDimension textureBindingViewDimension = WGPUTextureViewDimension_Undefined;
    WGPUExtent3D size = {1, 1, 1};
    uint32_t mipLevelCount = 1;
    uint32_t sampleCount = 1;
    std::vector<WGPUTextureFormat> viewFormats;
    std::vector<WGPUTextureViewImpl *> childViews;
    std::mutex childViewsMutex;
    std::atomic<uint64_t> lastUsageSerial{0};
    bool destroyed = false;
    bool invalid = false;
    bool isSwapchain = false;

    // §23.x lazy initialization tracking. Subresources are eager zero-init at create
    // (see Device.cpp wgpuDeviceCreateTexture), so the empty set means "all initialized".
    // storeOp=Discard adds entries; reads/writes that fully cover a subresource clear them.
    // Aspect-bit packed into the high byte so depth/stencil aspects track independently.
    // Key layout: ((aspect & 0xFF) << 56) | ((mip & 0xFFFFFF) << 32) | (layer & 0xFFFFFFFF)
    std::unordered_set<uint64_t> uninitializedSubresources;
    std::mutex stateMutex;
};

namespace pwgpu
{
    static constexpr uint8_t kAspectColor = 0;
    static constexpr uint8_t kAspectDepth = 1;
    static constexpr uint8_t kAspectStencil = 2;

    std::vector<uint8_t> AspectsForView(WGPUTextureFormat fmt, WGPUTextureAspect viewAspect);

    void MarkRangeInitialized(WGPUTextureImpl *tex, uint32_t baseMip, uint32_t mipCount,
                              uint32_t baseLayer, uint32_t layerCount,
                              const std::vector<uint8_t> &aspects);
    void MarkRangeUninitialized(WGPUTextureImpl *tex, uint32_t baseMip, uint32_t mipCount,
                                uint32_t baseLayer, uint32_t layerCount,
                                const std::vector<uint8_t> &aspects);

    // Returns true if any subresource in the range is currently uninitialized.
    bool RangeHasAnyUninitialized(WGPUTextureImpl *tex,
                                  uint32_t baseMip, uint32_t mipCount,
                                  uint32_t baseLayer, uint32_t layerCount,
                                  const std::vector<uint8_t> &aspects);

    // Records clear-to-zero into an existing encoder command buffer; marks the range
    // initialized. Caller must already hold no texture state lock.
    bool LazyInitViewRangeOnEncoder(pe::CommandBuffer *cmd, WGPUTextureImpl *tex,
                                    uint32_t baseMip, uint32_t mipCount,
                                    uint32_t baseLayer, uint32_t layerCount,
                                    const std::vector<uint8_t> &aspects);

    // Records zero initialization for exact (mip, layer) subresources. Compressed
    // color formats are initialized by copying zero blocks because Vulkan forbids
    // vkCmdClearColorImage on compressed images.
    void RecordZeroInitTextureSubresources(pe::CommandBuffer *cmd, WGPUTextureImpl *tex,
                                           const std::vector<std::pair<uint32_t, uint32_t>> &mipLayerPairs,
                                           vk::ImageAspectFlags aspectMask);
} // namespace pwgpu

struct WGPUTextureViewImpl
{
    std::atomic<uint32_t> refCount{1};
    std::string label;
    pe::ImageView *view = nullptr;
    WGPUTextureImpl *texture = nullptr;
    WGPUTextureFormat format = WGPUTextureFormat_Undefined;
    WGPUTextureViewDimension dimension = WGPUTextureViewDimension_2D;
    WGPUTextureUsage usage = WGPUTextureUsage_None;
    uint32_t baseMipLevel = 0;
    uint32_t mipLevelCount = 1;
    uint32_t baseArrayLayer = 0;
    uint32_t arrayLayerCount = 1;
    WGPUTextureAspect aspect = WGPUTextureAspect_All;
    WGPUTextureComponentSwizzle swizzle = {WGPUComponentSwizzle_R,
                                           WGPUComponentSwizzle_G,
                                           WGPUComponentSwizzle_B,
                                           WGPUComponentSwizzle_A};
    bool hasNonIdentitySwizzle = false;
    WGPUExtent3D renderExtent = {0, 0, 0};
};
