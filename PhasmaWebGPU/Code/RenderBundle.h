#pragma once

#include <webgpu/webgpu.h>
#include "API/Command.h"
#include "PipelineBinding.h"
#include "RenderPipeline.h"
#include "UsageTracker.h"

namespace pe
{
    class Buffer;
} // namespace pe

struct WGPUDeviceImpl;
struct WGPUBindGroupImpl;
struct WGPUBufferImpl;

using BundleCommand = std::function<void(pe::CommandBuffer *, pwgpu::WebGPUBindingCache *)>;

enum class WGPURenderBundleOpKind
{
    SetPipeline,
    SetBindGroup,
    SetVertexBuffer,
    SetIndexBuffer,
    Draw,
    DrawIndexed,
};

struct WGPURenderBundleOp
{
    WGPURenderBundleOpKind kind = WGPURenderBundleOpKind::Draw;
    WGPURenderPipelineImpl *pipeline = nullptr;
    WGPUPipelineLayoutImpl *layout = nullptr;
    WGPUBindGroupImpl *bindGroup = nullptr;
    pe::Buffer *buffer = nullptr;
    std::vector<uint32_t> dynamicOffsets;
    uint32_t slotOrGroup = 0;
    size_t offset = 0;
    PeIndexType indexType = PE_INDEX_TYPE_UINT32;
    uint32_t first = 0;
    uint32_t second = 0;
    uint32_t third = 0;
    int32_t signedValue = 0;
    uint32_t fourth = 0;
};

struct WGPUVulkanNativeRenderBundle
{
    PeBackendHandle commandPool = 0;
    PeBackendHandle commandBuffer = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<WGPUTextureFormat> colorFormats;
    WGPUTextureFormat depthStencilFormat = WGPUTextureFormat_Undefined;
    uint32_t sampleCount = 1;
};

struct WGPURenderBundleImpl
{
    std::atomic<uint32_t> refCount{1};
    std::string label;
    WGPUDeviceImpl *device = nullptr;
    bool invalid = false;
    std::string deferredErrorMessage;
    bool deferredResourceError = false; // §3.3 destroyed resource — fires at submit
    std::atomic<uint64_t> lastUsageSerial{0};

    std::vector<WGPUTextureFormat> colorFormats;
    WGPUTextureFormat depthStencilFormat = WGPUTextureFormat_Undefined;
    uint32_t sampleCount = 1;
    bool depthReadOnly = false;
    bool stencilReadOnly = false;
    uint64_t drawCount = 0;

    std::vector<BundleCommand> commands;
    std::vector<WGPURenderBundleOp> nativeOps;
    bool vulkanNativeEligible = true;
    std::vector<WGPUVulkanNativeRenderBundle> vulkanNativeBundles;
    PeBackendHandle vulkanSecondaryCommandBuffer = 0;
    uint32_t vulkanSecondaryWidth = 0;
    uint32_t vulkanSecondaryHeight = 0;
    std::vector<WGPUTextureFormat> vulkanSecondaryColorFormats;
    WGPUTextureFormat vulkanSecondaryDepthStencilFormat = WGPUTextureFormat_Undefined;
    uint32_t vulkanSecondarySampleCount = 1;

    std::vector<WGPURenderPipelineImpl *> retainedPipelines;
    std::vector<WGPUBindGroupImpl *> retainedBindGroups;
    std::vector<WGPUBufferImpl *> retainedBuffers;

    pwgpu::UsageScope usageScope;
    bool usageScopeValid = true;
};

struct WGPURenderBundleEncoderImpl
{
    std::atomic<uint32_t> refCount{1};
    std::string label;
    WGPUDeviceImpl *device = nullptr;
    bool invalid = false;
    bool finished = false;
    std::string deferredErrorMessage;
    bool deferredResourceError = false;

    std::vector<WGPUTextureFormat> colorFormats;
    WGPUTextureFormat depthStencilFormat = WGPUTextureFormat_Undefined;
    uint32_t sampleCount = 1;
    bool depthReadOnly = false;
    bool stencilReadOnly = false;

    WGPURenderPipelineImpl *pipeline = nullptr;
    uint32_t debugGroupDepth = 0;
    uint64_t drawCount = 0;

    WGPUBufferImpl *indexBuffer = nullptr;
    WGPUIndexFormat indexFormat = WGPUIndexFormat_Undefined;
    uint64_t indexBufferSize = 0;

    std::vector<BundleCommand> commands;
    std::vector<WGPURenderBundleOp> nativeOps;
    bool vulkanNativeEligible = true;

    std::vector<WGPURenderPipelineImpl *> retainedPipelines;
    std::vector<WGPUBindGroupImpl *> retainedBindGroups;
    std::vector<WGPUBufferImpl *> retainedBuffers;

    pwgpu::UsageScope usageScope;
    bool usageScopeValid = true;

    std::vector<WGPUBindGroupImpl *> currentBindGroups;
    std::vector<std::vector<uint32_t>> currentDynamicOffsets;

    std::vector<VertexBufferBinding> boundVertexBuffers;
};

namespace pwgpu
{
    bool EnsureVulkanNativeRenderBundle(WGPURenderBundleImpl *bundle,
                                        uint32_t renderWidth,
                                        uint32_t renderHeight,
                                        const std::vector<WGPUTextureFormat> &colorFormats,
                                        WGPUTextureFormat depthStencilFormat,
                                        uint32_t sampleCount);
} // namespace pwgpu
