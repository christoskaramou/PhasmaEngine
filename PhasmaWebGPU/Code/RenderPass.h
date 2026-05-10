#pragma once

#include <webgpu/webgpu.h>
#include "API/Command.h"
#include "API/Image.h"
#include "API/RHITypes.h"
#include "RenderPipeline.h"
#include "UsageTracker.h"

struct WGPUCommandEncoderImpl;
struct WGPUDeviceImpl;
struct WGPUBindGroupImpl;
struct WGPUTextureViewImpl;
struct WGPUQuerySetImpl;
struct WGPURenderBundleImpl;

enum class WGPURenderPassAttachmentStoreOp : uint8_t
{
    Store,
    DontCare,
    None
};

struct WGPURenderPassAttachmentInfo
{
    WGPUTextureViewImpl *textureView = nullptr;
    PeBackendHandle imageView = 0;
    pe::ImageView *attachmentView = nullptr;
    PeImageLayout imageLayout = PE_IMAGE_LAYOUT_UNDEFINED;
    PeLoadOp loadOp = PE_LOAD_OP_DONT_CARE;
    WGPURenderPassAttachmentStoreOp storeOp = WGPURenderPassAttachmentStoreOp::DontCare;

    WGPUTextureViewImpl *resolveTextureView = nullptr;
    PeBackendHandle resolveImageView = 0;
    PeImageLayout resolveImageLayout = PE_IMAGE_LAYOUT_UNDEFINED;
    bool resolveAverage = true;

    bool hasClearColor = false;
    WGPUTextureFormat clearColorFormat = WGPUTextureFormat_Undefined;
    WGPUColor clearColor{};

    bool hasClearDepth = false;
    float clearDepth = 0.0f;
    bool hasClearStencil = false;
    uint32_t clearStencil = 0;
};

struct WGPURenderPassEncoderImpl
{
    std::atomic<uint32_t> refCount{1};
    std::string label;
    pe::CommandBuffer *cmd = nullptr;
    WGPUCommandEncoderImpl *parent = nullptr;
    WGPUDeviceImpl *device = nullptr;
    bool ended = false;
    bool invalid = false;
    bool wasOpened = true;
    // Invalid pass whose error must surface at queue.submit(), not finish().
    bool deferredResourceError = false;
    std::string deferredErrorMessage;

    WGPURenderPipelineImpl *pipeline = nullptr;
    uint32_t debugGroupDepth = 0;
    bool renderingActive = false;
    bool bindingStateInvalidated = false;

    // beginRendering is deferred until the first draw-scope command so bind-group
    // barriers (including layout transitions) can be emitted outside the Vulkan
    // dynamic-rendering scope, where they are forbidden.
    std::vector<WGPURenderPassAttachmentInfo> deferredColorAttachments;
    WGPURenderPassAttachmentInfo deferredDepthAtt{};
    WGPURenderPassAttachmentInfo deferredStencilAtt{};
    bool deferredHasDepth = false;
    bool deferredHasStencil = false;
    uint32_t deferredRenderWidth = 0;
    uint32_t deferredRenderHeight = 0;
    bool bindGroupBarriersEmitted = false;
    // Attachment barriers (color/depth/stencil/resolve) computed at BeginRenderPass
    // but emitted lazily alongside bind-group barriers just before beginRendering.
    std::vector<pe::ImageBarrierInfo> deferredAttachmentBarriers;

    uint32_t attachmentWidth = 0;
    uint32_t attachmentHeight = 0;
    bool depthReadOnly = false;
    bool stencilReadOnly = false;

    std::vector<WGPUTextureFormat> colorFormats;
    WGPUTextureFormat depthStencilFormat = WGPUTextureFormat_Undefined;
    uint32_t sampleCount = 1;

    WGPUQuerySetImpl *timestampQuerySet = nullptr;
    uint32_t beginTimestampIndex = UINT32_MAX;
    uint32_t endTimestampIndex = UINT32_MAX;

    WGPUQuerySetImpl *occlusionQuerySet = nullptr;
    bool occlusionQueryActive = false;
    uint32_t activeOcclusionIndex = UINT32_MAX;
    std::unordered_set<uint32_t> usedOcclusionIndices;

    std::vector<WGPUTextureViewImpl *> retainedViews;
    std::vector<pe::ImageView *> ownedSliceViews;
    std::vector<pe::Attachment> dx12OpenAttachments;

    std::vector<WGPURenderPipelineImpl *> retainedPipelines;
    std::vector<WGPUBindGroupImpl *> retainedBindGroups;
    std::vector<WGPURenderBundleImpl *> retainedBundles;
    std::vector<WGPUBufferImpl *> usedBuffers;

    pwgpu::UsageScope usageScope;
    bool usageScopeValid = true;

    WGPUBufferImpl *indexBuffer = nullptr;
    WGPUIndexFormat indexFormat = WGPUIndexFormat_Undefined;
    uint64_t indexBufferSize = 0;

    uint64_t drawCount = 0;
    uint64_t maxDrawCount = 50000000;

    std::vector<WGPUBindGroupImpl *> currentBindGroups;
    std::vector<std::vector<uint32_t>> currentDynamicOffsets;

    std::vector<VertexBufferBinding> boundVertexBuffers;
};
