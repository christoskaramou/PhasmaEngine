#include "RenderBundle.h"
#include "RenderPipeline.h"
#include "PipelineBinding.h"
#include "PipelineLayout.h"
#include "BindGroup.h"
#include "Buffer.h"
#include "Device.h"
#include "FormatMap.h"
#include "Utils.h"
#include "API/Queue.h"
#include "API/RHI.h"
#include "API/Vulkan/VulkanBufferImpl.h"
#include "API/Vulkan/VulkanDescriptorImpl.h"
#include "API/Vulkan/RHI_Vulkan.h"
#include "API/Vulkan/VulkanRHITypeUtils.h"

extern "C" void wgpuDeviceRelease(WGPUDevice);
extern "C" void wgpuRenderPipelineAddRef(WGPURenderPipeline);
extern "C" void wgpuRenderPipelineRelease(WGPURenderPipeline);
extern "C" void wgpuBindGroupAddRef(WGPUBindGroup);
extern "C" void wgpuBindGroupRelease(WGPUBindGroup);
extern "C" void wgpuBufferAddRef(WGPUBuffer);
extern "C" void wgpuBufferRelease(WGPUBuffer);

namespace
{
    // §22: bundle errors surface at executeBundles → parent finish (first wins).
    void ReportEncoderValidation(WGPURenderBundleEncoder rbe, const char *msg)
    {
        if (!rbe)
            return;
        if (rbe->deferredErrorMessage.empty())
            rbe->deferredErrorMessage = msg ? msg : "";
    }

    bool ValidateBindGroupCompat(WGPURenderBundleEncoder rbe)
    {
        if (!rbe || !rbe->pipeline || !rbe->pipeline->layout)
            return true;
        auto &bgls = rbe->pipeline->layout->bindGroupLayouts;
        for (size_t i = 0; i < bgls.size(); ++i)
        {
            auto *plBgl = bgls[i];
            if (!plBgl)
                continue;
            if (i >= rbe->currentBindGroups.size())
            {
                rbe->invalid = true;
                return false;
            }
            auto *bg = rbe->currentBindGroups[i];
            if (!bg || bg->invalid || !bg->layout)
            {
                rbe->invalid = true;
                return false;
            }
            if (!BglGroupEquivalent(bg->layout, plBgl))
            {
                rbe->invalid = true;
                return false;
            }
        }
        return true;
    }

    bool EncoderOpen(WGPURenderBundleEncoder rbe, const char *apiName)
    {
        if (!rbe)
            return false;
        if (rbe->finished)
        {
            if (rbe->device)
            {
                std::string msg = std::string(apiName) + ": render bundle encoder is already finished";
                rbe->device->reportError(WGPUErrorType_Validation, pwgpu::ToStringView(msg));
            }
            return false;
        }
        if (rbe->invalid)
            return false;
        return true;
    }

    vk::Format ResolveBundleVkFormat(WGPUDeviceImpl *device, WGPUTextureFormat format)
    {
        if (format == WGPUTextureFormat_Undefined)
            return vk::Format::eUndefined;
        const VkFormat resolved = device
                                      ? pwgpu::ResolveVkTextureFormat(format,
                                                                      device->resolvedDepth24Plus,
                                                                      device->resolvedDepth24PlusStencil8,
                                                                      device->resolvedStencil8)
                                      : pwgpu::ToVkFormat(format);
        return static_cast<vk::Format>(resolved);
    }

    bool NativeBundleCacheKeyMatches(const WGPUVulkanNativeRenderBundle &cached,
                                     uint32_t renderWidth,
                                     uint32_t renderHeight,
                                     const std::vector<WGPUTextureFormat> &colorFormats,
                                     WGPUTextureFormat depthStencilFormat,
                                     uint32_t sampleCount)
    {
        return cached.width == renderWidth &&
               cached.height == renderHeight &&
               cached.sampleCount == sampleCount &&
               cached.depthStencilFormat == depthStencilFormat &&
               cached.colorFormats == colorFormats;
    }

    bool ActiveNativeBundleCacheKeyMatches(const WGPURenderBundleImpl *rb,
                                           uint32_t renderWidth,
                                           uint32_t renderHeight,
                                           const std::vector<WGPUTextureFormat> &colorFormats,
                                           WGPUTextureFormat depthStencilFormat,
                                           uint32_t sampleCount)
    {
        return rb && rb->vulkanSecondaryCommandBuffer != 0 &&
               rb->vulkanSecondaryWidth == renderWidth &&
               rb->vulkanSecondaryHeight == renderHeight &&
               rb->vulkanSecondarySampleCount == sampleCount &&
               rb->vulkanSecondaryDepthStencilFormat == depthStencilFormat &&
               rb->vulkanSecondaryColorFormats == colorFormats;
    }

    void DestroyVulkanCommandPool(PeBackendHandle commandPool)
    {
        if (commandPool == 0 || pe::GetRHI().GetApi() != PE_GRAPHICS_API_VULKAN)
            return;
        pe::VulkanRhi::Device().destroyCommandPool(
            vk::CommandPool{PeFromBackendHandle<VkCommandPool>(commandPool)});
    }

    void ClearActiveVulkanNativeBundle(WGPURenderBundleImpl *rb)
    {
        rb->vulkanSecondaryCommandBuffer = 0;
        rb->vulkanSecondaryWidth = 0;
        rb->vulkanSecondaryHeight = 0;
        rb->vulkanSecondaryColorFormats.clear();
        rb->vulkanSecondaryDepthStencilFormat = WGPUTextureFormat_Undefined;
        rb->vulkanSecondarySampleCount = 1;
    }

    bool RecordVulkanNativeOp(vk::CommandBuffer cmd,
                              WGPUDeviceImpl *device,
                              const WGPURenderBundleOp &op,
                              bool &descriptorBufferBound)
    {
        switch (op.kind)
        {
        case WGPURenderBundleOpKind::SetPipeline:
            if (!op.pipeline || op.pipeline->backendPipeline == 0)
                return false;
            cmd.bindPipeline(
                vk::PipelineBindPoint::eGraphics,
                vk::Pipeline{PeFromBackendHandle<VkPipeline>(op.pipeline->backendPipeline)});
            return true;

        case WGPURenderBundleOpKind::SetBindGroup:
        {
            if (!op.layout || !op.bindGroup || op.layout->backendLayout == 0)
                return false;

            vk::PipelineLayout vkLayout{
                PeFromBackendHandle<VkPipelineLayout>(op.layout->backendLayout)};
            if (op.layout->bindingModel == WGPUBindingModel::DescriptorBuffer)
            {
                if (!op.bindGroup->descriptorBufferValid || !op.dynamicOffsets.empty() ||
                    !device || !device->descriptorBuffer.enabled ||
                    !device->descriptorBuffer.buffer)
                    return false;

                if (!descriptorBufferBound)
                {
                    vk::DescriptorBufferBindingInfoEXT bindingInfo{};
                    bindingInfo.address =
                        device->descriptorBuffer.buffer->GetDeviceAddress();
                    bindingInfo.usage =
                        vk::BufferUsageFlagBits::eResourceDescriptorBufferEXT |
                        vk::BufferUsageFlagBits::eSamplerDescriptorBufferEXT;
                    cmd.bindDescriptorBuffersEXT(1, &bindingInfo);
                    descriptorBufferBound = true;
                }

                const uint32_t bufferIndex = 0;
                const vk::DeviceSize offset =
                    static_cast<vk::DeviceSize>(op.bindGroup->descriptorBufferOffset);
                cmd.setDescriptorBufferOffsetsEXT(
                    vk::PipelineBindPoint::eGraphics, vkLayout, op.slotOrGroup, 1,
                    &bufferIndex, &offset);
                return true;
            }

            if (!op.bindGroup->descriptor)
                return false;
            vk::DescriptorSet ds = pe::GetVulkanDescriptorSet(op.bindGroup->descriptor);
            cmd.bindDescriptorSets(
                vk::PipelineBindPoint::eGraphics, vkLayout, op.slotOrGroup, 1, &ds,
                static_cast<uint32_t>(op.dynamicOffsets.size()),
                op.dynamicOffsets.empty() ? nullptr : op.dynamicOffsets.data());
            return true;
        }

        case WGPURenderBundleOpKind::SetVertexBuffer:
        {
            if (!op.buffer)
                return false;
            vk::Buffer vkBuffer = pe::GetVulkanBuffer(op.buffer);
            vk::DeviceSize vkOffset = static_cast<vk::DeviceSize>(op.offset);
            cmd.bindVertexBuffers(op.slotOrGroup, 1, &vkBuffer, &vkOffset);
            return true;
        }

        case WGPURenderBundleOpKind::SetIndexBuffer:
            if (!op.buffer)
                return false;
            cmd.bindIndexBuffer(
                pe::GetVulkanBuffer(op.buffer), op.offset, pe::ToVkIndexType(op.indexType));
            return true;

        case WGPURenderBundleOpKind::Draw:
            cmd.draw(op.first, op.second, op.third, op.fourth);
            return true;

        case WGPURenderBundleOpKind::DrawIndexed:
            cmd.drawIndexed(op.first, op.second, op.third, op.signedValue, op.fourth);
            return true;
        }

        return false;
    }

    void DestroyVulkanNativeRenderBundle(WGPURenderBundleImpl *rb)
    {
        if (!rb || pe::GetRHI().GetApi() != PE_GRAPHICS_API_VULKAN)
            return;

        const uint64_t serial = rb->lastUsageSerial.load(std::memory_order_acquire);
        const bool defer = pwgpu::IsQueueSerialPending(rb->device, serial);
        for (const WGPUVulkanNativeRenderBundle &cached : rb->vulkanNativeBundles)
        {
            if (cached.commandPool == 0)
                continue;
            if (defer)
            {
                std::lock_guard<std::mutex> lock(rb->device->pendingResourceDeletionsMutex);
                rb->device->pendingVulkanCommandPoolDeletions.push_back(
                    {cached.commandPool, serial});
            }
            else
            {
                DestroyVulkanCommandPool(cached.commandPool);
            }
        }
        rb->vulkanNativeBundles.clear();
        ClearActiveVulkanNativeBundle(rb);
    }

    bool CreateVulkanNativeRenderBundle(WGPURenderBundleImpl *rb,
                                        uint32_t renderWidth,
                                        uint32_t renderHeight,
                                        const std::vector<WGPUTextureFormat> &passColorFormats,
                                        WGPUTextureFormat passDepthStencilFormat,
                                        uint32_t passSampleCount)
    {
        if (!rb || !rb->vulkanNativeEligible || rb->nativeOps.empty() ||
            !rb->device || !rb->device->peQueue ||
            pe::GetRHI().GetApi() != PE_GRAPHICS_API_VULKAN ||
            !pe::Settings::Get<pe::GlobalSettings>().dynamic_rendering ||
            renderWidth == 0 || renderHeight == 0)
            return false;

        if (ActiveNativeBundleCacheKeyMatches(rb, renderWidth, renderHeight,
                                              passColorFormats,
                                              passDepthStencilFormat,
                                              passSampleCount))
            return true;

        for (const WGPUVulkanNativeRenderBundle &cached : rb->vulkanNativeBundles)
        {
            if (cached.commandBuffer != 0 &&
                NativeBundleCacheKeyMatches(cached, renderWidth, renderHeight,
                                            passColorFormats,
                                            passDepthStencilFormat,
                                            passSampleCount))
            {
                rb->vulkanSecondaryCommandBuffer = cached.commandBuffer;
                rb->vulkanSecondaryWidth = renderWidth;
                rb->vulkanSecondaryHeight = renderHeight;
                rb->vulkanSecondaryColorFormats = cached.colorFormats;
                rb->vulkanSecondaryDepthStencilFormat = cached.depthStencilFormat;
                rb->vulkanSecondarySampleCount = cached.sampleCount;
                return true;
            }
        }

        std::vector<vk::Format> colorFormats;
        colorFormats.reserve(passColorFormats.size());
        for (WGPUTextureFormat format : passColorFormats)
            colorFormats.push_back(ResolveBundleVkFormat(rb->device, format));

        vk::Format depthFormat = vk::Format::eUndefined;
        vk::Format stencilFormat = vk::Format::eUndefined;
        if (passDepthStencilFormat != WGPUTextureFormat_Undefined)
        {
            const vk::Format dsFormat =
                ResolveBundleVkFormat(rb->device, passDepthStencilFormat);
            if (pwgpu::HasDepthAspect(passDepthStencilFormat))
                depthFormat = dsFormat;
            if (pwgpu::HasStencilAspect(passDepthStencilFormat))
                stencilFormat = dsFormat;
        }

        vk::CommandPoolCreateInfo poolInfo{};
        poolInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
        poolInfo.queueFamilyIndex = rb->device->peQueue->GetFamilyId();

        vk::CommandPool pool{};
        try
        {
            pool = pe::VulkanRhi::Device().createCommandPool(poolInfo);

            vk::CommandBufferAllocateInfo allocInfo{};
            allocInfo.commandPool = pool;
            allocInfo.level = vk::CommandBufferLevel::eSecondary;
            allocInfo.commandBufferCount = 1;
            vk::CommandBuffer cmd =
                pe::VulkanRhi::Device().allocateCommandBuffers(allocInfo)[0];

            vk::CommandBufferInheritanceRenderingInfo renderingInfo{};
            renderingInfo.colorAttachmentCount = static_cast<uint32_t>(colorFormats.size());
            renderingInfo.pColorAttachmentFormats =
                colorFormats.empty() ? nullptr : colorFormats.data();
            renderingInfo.depthAttachmentFormat = depthFormat;
            renderingInfo.stencilAttachmentFormat = stencilFormat;
            renderingInfo.rasterizationSamples = pwgpu::ToVkSampleCount(passSampleCount);

            vk::CommandBufferInheritanceInfo inheritanceInfo{};
            inheritanceInfo.pNext = &renderingInfo;

            vk::CommandBufferBeginInfo beginInfo{};
            beginInfo.flags = vk::CommandBufferUsageFlagBits::eRenderPassContinue |
                              vk::CommandBufferUsageFlagBits::eSimultaneousUse;
            beginInfo.pInheritanceInfo = &inheritanceInfo;

            cmd.begin(beginInfo);

            vk::Viewport viewport{};
            viewport.x = 0.0f;
            viewport.y = 0.0f;
            viewport.width = static_cast<float>(renderWidth);
            viewport.height = static_cast<float>(renderHeight);
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;
            cmd.setViewport(0, 1, &viewport);

            vk::Rect2D scissor{};
            scissor.offset = vk::Offset2D{0, 0};
            scissor.extent = vk::Extent2D{renderWidth, renderHeight};
            cmd.setScissor(0, 1, &scissor);

            // WebGPU resets render-pass encoder dynamic state after executeBundles.
            // Vulkan does not promise primary state restoration after secondary
            // execution, so these defaults are intentionally recorded in the
            // secondary command buffer for later inline commands to observe.
            float blendConstants[4] = {};
            cmd.setBlendConstants(blendConstants);
            cmd.setStencilReference(vk::StencilFaceFlagBits::eFrontAndBack, 0);

            bool descriptorBufferBound = false;
            for (const WGPURenderBundleOp &op : rb->nativeOps)
            {
                if (!RecordVulkanNativeOp(cmd, rb->device, op, descriptorBufferBound))
                {
                    cmd.end();
                    pe::VulkanRhi::Device().destroyCommandPool(pool);
                    return false;
                }
            }
            cmd.end();

            WGPUVulkanNativeRenderBundle cached{};
            cached.commandPool = PeToBackendHandle(static_cast<VkCommandPool>(pool));
            cached.commandBuffer = PeToBackendHandle(static_cast<VkCommandBuffer>(cmd));
            cached.width = renderWidth;
            cached.height = renderHeight;
            cached.colorFormats = passColorFormats;
            cached.depthStencilFormat = passDepthStencilFormat;
            cached.sampleCount = passSampleCount;
            rb->vulkanNativeBundles.push_back(cached);
            rb->vulkanSecondaryCommandBuffer = cached.commandBuffer;
            rb->vulkanSecondaryWidth = renderWidth;
            rb->vulkanSecondaryHeight = renderHeight;
            rb->vulkanSecondaryColorFormats = cached.colorFormats;
            rb->vulkanSecondaryDepthStencilFormat = cached.depthStencilFormat;
            rb->vulkanSecondarySampleCount = cached.sampleCount;
            return true;
        }
        catch (...)
        {
            if (pool)
                DestroyVulkanCommandPool(PeToBackendHandle(static_cast<VkCommandPool>(pool)));
            ClearActiveVulkanNativeBundle(rb);
            return false;
        }
    }
} // namespace

namespace pwgpu
{
    bool EnsureVulkanNativeRenderBundle(WGPURenderBundleImpl *bundle,
                                        uint32_t renderWidth,
                                        uint32_t renderHeight,
                                        const std::vector<WGPUTextureFormat> &colorFormats,
                                        WGPUTextureFormat depthStencilFormat,
                                        uint32_t sampleCount)
    {
        return CreateVulkanNativeRenderBundle(bundle, renderWidth, renderHeight,
                                              colorFormats, depthStencilFormat,
                                              sampleCount);
    }
} // namespace pwgpu

extern "C"
{

    void wgpuRenderBundleAddRef(WGPURenderBundle rb)
    {
        if (rb)
            rb->refCount.fetch_add(1, std::memory_order_relaxed);
    }

    void wgpuRenderBundleRelease(WGPURenderBundle rb)
    {
        if (!rb)
            return;
        if (rb->refCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
        {
            DestroyVulkanNativeRenderBundle(rb);
            for (auto *p : rb->retainedPipelines)
                wgpuRenderPipelineRelease(p);
            for (auto *bg : rb->retainedBindGroups)
                wgpuBindGroupRelease(bg);
            for (auto *buf : rb->retainedBuffers)
                wgpuBufferRelease(buf);
            if (rb->device)
                wgpuDeviceRelease(rb->device);
            delete rb;
        }
    }

    void wgpuRenderBundleSetLabel(WGPURenderBundle rb, WGPUStringView label)
    {
        if (rb)
            rb->label = pwgpu::ToString(label);
    }

    void wgpuRenderBundleEncoderAddRef(WGPURenderBundleEncoder rbe)
    {
        if (rbe)
            rbe->refCount.fetch_add(1, std::memory_order_relaxed);
    }

    void wgpuRenderBundleEncoderRelease(WGPURenderBundleEncoder rbe)
    {
        if (!rbe)
            return;
        if (rbe->refCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
        {
            for (auto *p : rbe->retainedPipelines)
                wgpuRenderPipelineRelease(p);
            for (auto *bg : rbe->retainedBindGroups)
                wgpuBindGroupRelease(bg);
            for (auto *buf : rbe->retainedBuffers)
                wgpuBufferRelease(buf);
            if (rbe->device)
                wgpuDeviceRelease(rbe->device);
            delete rbe;
        }
    }

    void wgpuRenderBundleEncoderSetPipeline(WGPURenderBundleEncoder rbe, WGPURenderPipeline pipeline)
    {
        if (!EncoderOpen(rbe, "wgpuRenderBundleEncoderSetPipeline"))
            return;
        if (!pipeline || pipeline->invalid || pipeline->backendPipeline == 0)
        {
            ReportEncoderValidation(rbe,
                                    "wgpuRenderBundleEncoderSetPipeline: pipeline is null or invalid");
            rbe->invalid = true;
            return;
        }
        if (pipeline->device != rbe->device)
        {
            ReportEncoderValidation(rbe,
                                    "wgpuRenderBundleEncoderSetPipeline: pipeline was created by a "
                                    "different device than the render bundle encoder");
            rbe->invalid = true;
            return;
        }

        if (pipeline->sampleCount != rbe->sampleCount)
        {
            char buf[192];
            std::snprintf(buf, sizeof(buf),
                          "wgpuRenderBundleEncoderSetPipeline: pipeline sampleCount (%u) does not "
                          "match render bundle encoder sampleCount (%u)",
                          pipeline->sampleCount, rbe->sampleCount);
            ReportEncoderValidation(rbe, buf);
            rbe->invalid = true;
            return;
        }
        if (pipeline->depthStencilFormat != rbe->depthStencilFormat)
        {
            ReportEncoderValidation(rbe,
                                    "wgpuRenderBundleEncoderSetPipeline: pipeline depthStencilFormat "
                                    "does not match render bundle encoder depthStencilFormat");
            rbe->invalid = true;
            return;
        }

        size_t pLen = pipeline->colorFormats.size(), eLen = rbe->colorFormats.size();
        while (pLen > 0 && pipeline->colorFormats[pLen - 1] == WGPUTextureFormat_Undefined)
            --pLen;
        while (eLen > 0 && rbe->colorFormats[eLen - 1] == WGPUTextureFormat_Undefined)
            --eLen;
        bool colorMismatch = (pLen != eLen);
        if (!colorMismatch)
        {
            for (size_t i = 0; i < pLen; i++)
            {
                if (pipeline->colorFormats[i] != rbe->colorFormats[i])
                {
                    colorMismatch = true;
                    break;
                }
            }
        }
        if (colorMismatch)
        {
            ReportEncoderValidation(rbe,
                                    "wgpuRenderBundleEncoderSetPipeline: pipeline colorFormats do "
                                    "not match render bundle encoder colorFormats");
            rbe->invalid = true;
            return;
        }

        if (pipeline->writesDepth && rbe->depthReadOnly)
        {
            ReportEncoderValidation(rbe,
                                    "wgpuRenderBundleEncoderSetPipeline: pipeline writesDepth is "
                                    "true but render bundle encoder depthReadOnly is true");
            rbe->invalid = true;
            return;
        }
        if (pipeline->writesStencil && rbe->stencilReadOnly)
        {
            ReportEncoderValidation(rbe,
                                    "wgpuRenderBundleEncoderSetPipeline: pipeline writesStencil is "
                                    "true but render bundle encoder stencilReadOnly is true");
            rbe->invalid = true;
            return;
        }

        rbe->pipeline = pipeline;

        wgpuRenderPipelineAddRef(pipeline);
        rbe->retainedPipelines.push_back(pipeline);

        rbe->commands.push_back([pipeline](pe::CommandBuffer *cmd, pwgpu::WebGPUBindingCache *cache)
                                { pwgpu::BindWebGPURenderPipeline(cmd, pipeline, cache); });
        WGPURenderBundleOp op{};
        op.kind = WGPURenderBundleOpKind::SetPipeline;
        op.pipeline = pipeline;
        rbe->nativeOps.push_back(std::move(op));

        if (pipeline->layout)
        {
            auto &bgls = pipeline->layout->bindGroupLayouts;
            for (size_t i = 0; i < rbe->currentBindGroups.size() && i < bgls.size(); ++i)
            {
                auto *bg = rbe->currentBindGroups[i];
                if (!bg || !bg->descriptor || !bgls[i])
                    continue;
                if (!BglGroupEquivalent(bg->layout, bgls[i]))
                    continue;

                const uint32_t groupIndex = static_cast<uint32_t>(i);
                std::vector<uint32_t> dynOffsets =
                    (i < rbe->currentDynamicOffsets.size())
                        ? rbe->currentDynamicOffsets[i]
                        : std::vector<uint32_t>{};
                auto *layout = pipeline->layout;
                rbe->commands.push_back([layout, groupIndex, bg, dynOffsets](pe::CommandBuffer *cmd,
                                                                             pwgpu::WebGPUBindingCache *cache)
                                        { pwgpu::BindWebGPUBindGroup(
                                              cmd, pwgpu::PipelineBindingPoint::Render,
                                              layout, groupIndex, bg,
                                              dynOffsets.size(),
                                              dynOffsets.empty() ? nullptr : dynOffsets.data(),
                                              cache); });
                WGPURenderBundleOp op{};
                op.kind = WGPURenderBundleOpKind::SetBindGroup;
                op.layout = layout;
                op.bindGroup = bg;
                op.slotOrGroup = groupIndex;
                op.dynamicOffsets = dynOffsets;
                rbe->nativeOps.push_back(std::move(op));
            }
        }
    }

    void wgpuRenderBundleEncoderSetBindGroup(WGPURenderBundleEncoder rbe, uint32_t groupIndex,
                                             WGPUBindGroup group,
                                             size_t dynamicOffsetCount, uint32_t const *dynamicOffsets)
    {
        if (!EncoderOpen(rbe, "wgpuRenderBundleEncoderSetBindGroup"))
            return;

        if (rbe->device && groupIndex >= rbe->device->limits.maxBindGroups)
        {
            char buf[160];
            std::snprintf(buf, sizeof(buf),
                          "wgpuRenderBundleEncoderSetBindGroup: index (%u) must be < "
                          "maxBindGroups (%u)",
                          groupIndex, rbe->device->limits.maxBindGroups);
            ReportEncoderValidation(rbe, buf);
            rbe->invalid = true;
            return;
        }

        if (group)
        {
            if (group->device != rbe->device)
            {
                ReportEncoderValidation(rbe,
                                        "wgpuRenderBundleEncoderSetBindGroup: bindGroup is not valid "
                                        "to use with this encoder (cross-device)");
                rbe->invalid = true;
                return;
            }
            if (group->invalid)
            {
                if (group->invalidFromDestroyedResource)
                {
                    rbe->deferredResourceError = true;
                }
                else
                {
                    ReportEncoderValidation(
                        rbe,
                        "wgpuRenderBundleEncoderSetBindGroup: bindGroup is not valid "
                        "to use with this encoder (invalid bindGroup)");
                    rbe->invalid = true;
                }
                return;
            }
        }

        if (group)
        {
            for (auto &use : group->textureUses)
            {
                std::string err;
                if (!rbe->usageScope.AddView(use.view, use.kind, err))
                    rbe->usageScopeValid = false;
            }
            for (auto &use : group->bufferUses)
            {
                std::string err;
                if (!rbe->usageScope.AddBuffer(use.buffer, use.kind, err))
                    rbe->usageScopeValid = false;
            }
            wgpuBindGroupAddRef(group);
            rbe->retainedBindGroups.push_back(group);
        }

        if (rbe->device && groupIndex < rbe->device->limits.maxBindGroups)
        {
            if (rbe->currentBindGroups.size() <= groupIndex)
                rbe->currentBindGroups.resize(groupIndex + 1, nullptr);
            rbe->currentBindGroups[groupIndex] = group;
            if (rbe->currentDynamicOffsets.size() <= groupIndex)
                rbe->currentDynamicOffsets.resize(groupIndex + 1);
            if (dynamicOffsetCount > 0 && dynamicOffsets)
                rbe->currentDynamicOffsets[groupIndex].assign(
                    dynamicOffsets, dynamicOffsets + dynamicOffsetCount);
            else
                rbe->currentDynamicOffsets[groupIndex].clear();
        }

        if (group && group->layout)
        {
            if (static_cast<uint32_t>(dynamicOffsetCount) != group->layout->dynamicOffsetCount)
            {
                char buf[160];
                std::snprintf(buf, sizeof(buf),
                              "wgpuRenderBundleEncoderSetBindGroup: dynamicOffsetCount (%zu) does not "
                              "match bind group layout dynamicOffsetCount (%u)",
                              dynamicOffsetCount, group->layout->dynamicOffsetCount);
                ReportEncoderValidation(rbe, buf);
                rbe->invalid = true;
                return;
            }
            if (dynamicOffsetCount > 0 && !dynamicOffsets)
            {
                ReportEncoderValidation(rbe,
                                        "wgpuRenderBundleEncoderSetBindGroup: dynamicOffsets is null but "
                                        "dynamicOffsetCount > 0");
                rbe->invalid = true;
                return;
            }
            if (rbe->device && dynamicOffsetCount > 0 && dynamicOffsets)
            {
                std::vector<const WGPUBindGroupLayoutEntryResolved *> dynLayoutEntries;
                for (auto &e : group->layout->entries)
                    if (e.buffer.hasDynamicOffset)
                        dynLayoutEntries.push_back(&e);
                std::sort(dynLayoutEntries.begin(), dynLayoutEntries.end(),
                          [](auto *a, auto *b)
                          { return a->binding < b->binding; });
                for (uint32_t i = 0; i < dynamicOffsetCount && i < dynLayoutEntries.size() &&
                                     i < group->dynamicBindings.size();
                     ++i)
                {
                    uint32_t offset = dynamicOffsets[i];
                    auto &dyn = group->dynamicBindings[i];
                    bool isUniform = dynLayoutEntries[i]->buffer.type == WGPUBufferBindingType_Uniform;
                    uint32_t align = isUniform ? rbe->device->limits.minUniformBufferOffsetAlignment
                                               : rbe->device->limits.minStorageBufferOffsetAlignment;
                    if (align > 0 && offset % align != 0)
                    {
                        char buf[192];
                        std::snprintf(buf, sizeof(buf),
                                      "wgpuRenderBundleEncoderSetBindGroup: dynamicOffsets[%u]=%u is not "
                                      "a multiple of %s (%u) for binding %u",
                                      i, offset,
                                      isUniform ? "minUniformBufferOffsetAlignment"
                                                : "minStorageBufferOffsetAlignment",
                                      align, dynLayoutEntries[i]->binding);
                        ReportEncoderValidation(rbe, buf);
                        rbe->invalid = true;
                        return;
                    }
                    if (dyn.buffer)
                    {
                        uint64_t bufSize = dyn.buffer->size;
                        uint64_t effOffset = dyn.baseOffset + static_cast<uint64_t>(offset);
                        if (effOffset > bufSize || dyn.bindingSize > bufSize - effOffset)
                        {
                            char buf[224];
                            std::snprintf(buf, sizeof(buf),
                                          "wgpuRenderBundleEncoderSetBindGroup: dynamicOffsets[%u]=%u "
                                          "exceeds buffer bounds for binding %u "
                                          "(baseOffset=%llu, bindingSize=%llu, bufferSize=%llu)",
                                          i, offset, dyn.binding,
                                          (unsigned long long)dyn.baseOffset,
                                          (unsigned long long)dyn.bindingSize,
                                          (unsigned long long)bufSize);
                            ReportEncoderValidation(rbe, buf);
                            rbe->invalid = true;
                            return;
                        }
                    }
                }
            }
        }

        if (!rbe->pipeline || !rbe->pipeline->layout)
            return;

        if (!group || !group->descriptor)
            return;

        auto &bgls = rbe->pipeline->layout->bindGroupLayouts;
        if (groupIndex >= bgls.size() || !bgls[groupIndex])
            return;
        if (!BglGroupEquivalent(group->layout, bgls[groupIndex]))
            return;

        std::vector<uint32_t> dynOffsets;
        if (dynamicOffsetCount > 0 && dynamicOffsets)
            dynOffsets.assign(dynamicOffsets, dynamicOffsets + dynamicOffsetCount);

        auto *layout = rbe->pipeline->layout;
        rbe->commands.push_back([layout, groupIndex, group, dynOffsets](pe::CommandBuffer *cmd,
                                                                        pwgpu::WebGPUBindingCache *cache)
                                { pwgpu::BindWebGPUBindGroup(
                                      cmd, pwgpu::PipelineBindingPoint::Render,
                                      layout, groupIndex, group,
                                      dynOffsets.size(),
                                      dynOffsets.empty() ? nullptr : dynOffsets.data(),
                                      cache); });
        WGPURenderBundleOp op{};
        op.kind = WGPURenderBundleOpKind::SetBindGroup;
        op.layout = layout;
        op.bindGroup = group;
        op.slotOrGroup = groupIndex;
        op.dynamicOffsets = std::move(dynOffsets);
        rbe->nativeOps.push_back(std::move(op));
    }

    void wgpuRenderBundleEncoderSetVertexBuffer(WGPURenderBundleEncoder rbe, uint32_t slot,
                                                WGPUBuffer buffer, uint64_t offset, uint64_t size)
    {
        if (!EncoderOpen(rbe, "wgpuRenderBundleEncoderSetVertexBuffer"))
            return;

        if (rbe->device && slot >= rbe->device->limits.maxVertexBuffers)
        {
            char buf[160];
            std::snprintf(buf, sizeof(buf),
                          "wgpuRenderBundleEncoderSetVertexBuffer: slot (%u) must be < "
                          "maxVertexBuffers (%u)",
                          slot, rbe->device->limits.maxVertexBuffers);
            ReportEncoderValidation(rbe, buf);
            rbe->invalid = true;
            return;
        }
        if (offset % 4u != 0u)
        {
            ReportEncoderValidation(rbe,
                                    "wgpuRenderBundleEncoderSetVertexBuffer: offset must be a "
                                    "multiple of 4");
            rbe->invalid = true;
            return;
        }
        uint64_t bufferSize = buffer ? buffer->size : 0u;
        if (size == WGPU_WHOLE_SIZE)
            size = (offset > bufferSize) ? 0u : (bufferSize - offset);
        if (offset > bufferSize || size > bufferSize - offset)
        {
            ReportEncoderValidation(rbe,
                                    "wgpuRenderBundleEncoderSetVertexBuffer: offset + size "
                                    "exceeds buffer size");
            rbe->invalid = true;
            return;
        }

        if (!buffer)
        {
            if (slot < rbe->boundVertexBuffers.size())
                rbe->boundVertexBuffers[slot] = {};
            return;
        }

        if (buffer->device != rbe->device || buffer->invalid)
        {
            ReportEncoderValidation(rbe,
                                    "wgpuRenderBundleEncoderSetVertexBuffer: buffer is not valid "
                                    "to use with this encoder (cross-device or invalid buffer)");
            rbe->invalid = true;
            return;
        }
        if (!(buffer->usage & WGPUBufferUsage_Vertex))
        {
            ReportEncoderValidation(rbe,
                                    "wgpuRenderBundleEncoderSetVertexBuffer: buffer usage must "
                                    "contain VERTEX");
            rbe->invalid = true;
            return;
        }

        if (rbe->boundVertexBuffers.size() <= slot)
            rbe->boundVertexBuffers.resize(slot + 1);
        rbe->boundVertexBuffers[slot].bound = true;
        rbe->boundVertexBuffers[slot].buffer = buffer;
        rbe->boundVertexBuffers[slot].offset = offset;
        rbe->boundVertexBuffers[slot].size = size;

        {
            std::string err;
            if (!rbe->usageScope.AddBuffer(buffer, pwgpu::BufferUsageKind::Input, err))
                rbe->usageScopeValid = false;
        }

        wgpuBufferAddRef(buffer);
        rbe->retainedBuffers.push_back(buffer);

        if (!buffer->peBuffer)
            return;

        rbe->commands.push_back([buffer, slot, offset](pe::CommandBuffer *cmd, pwgpu::WebGPUBindingCache *cache)
                                { pwgpu::BindWebGPUVertexBuffer(cmd, buffer->peBuffer, static_cast<size_t>(offset), slot, 1, cache); });
        WGPURenderBundleOp op{};
        op.kind = WGPURenderBundleOpKind::SetVertexBuffer;
        op.buffer = buffer->peBuffer;
        op.slotOrGroup = slot;
        op.offset = static_cast<size_t>(offset);
        rbe->nativeOps.push_back(std::move(op));
    }

    void wgpuRenderBundleEncoderSetIndexBuffer(WGPURenderBundleEncoder rbe, WGPUBuffer buffer,
                                               WGPUIndexFormat format, uint64_t offset, uint64_t size)
    {
        if (!EncoderOpen(rbe, "wgpuRenderBundleEncoderSetIndexBuffer"))
            return;

        if (!buffer)
        {
            ReportEncoderValidation(rbe,
                                    "wgpuRenderBundleEncoderSetIndexBuffer: buffer is null");
            rbe->invalid = true;
            return;
        }
        if (buffer->device != rbe->device || buffer->invalid)
        {
            ReportEncoderValidation(rbe,
                                    "wgpuRenderBundleEncoderSetIndexBuffer: buffer is not valid "
                                    "to use with this encoder (cross-device or invalid buffer)");
            rbe->invalid = true;
            return;
        }
        if (!(buffer->usage & WGPUBufferUsage_Index))
        {
            ReportEncoderValidation(rbe,
                                    "wgpuRenderBundleEncoderSetIndexBuffer: buffer usage must "
                                    "contain INDEX");
            rbe->invalid = true;
            return;
        }
        if (format != WGPUIndexFormat_Uint16 && format != WGPUIndexFormat_Uint32)
        {
            ReportEncoderValidation(rbe,
                                    "wgpuRenderBundleEncoderSetIndexBuffer: indexFormat must be "
                                    "Uint16 or Uint32");
            rbe->invalid = true;
            return;
        }
        uint32_t indexSize = (format == WGPUIndexFormat_Uint16) ? 2u : 4u;
        if (offset % indexSize != 0u)
        {
            ReportEncoderValidation(rbe,
                                    "wgpuRenderBundleEncoderSetIndexBuffer: offset must be a "
                                    "multiple of indexFormat byte size");
            rbe->invalid = true;
            return;
        }
        uint64_t boundSize = size;
        if (boundSize == WGPU_WHOLE_SIZE)
            boundSize = (offset > buffer->size) ? 0u : (buffer->size - offset);
        if (offset > buffer->size || boundSize > buffer->size - offset)
        {
            ReportEncoderValidation(rbe,
                                    "wgpuRenderBundleEncoderSetIndexBuffer: offset + size "
                                    "exceeds buffer size");
            rbe->invalid = true;
            return;
        }

        rbe->indexBuffer = buffer;
        rbe->indexFormat = format;
        rbe->indexBufferSize = boundSize;

        {
            std::string err;
            if (!rbe->usageScope.AddBuffer(buffer, pwgpu::BufferUsageKind::Input, err))
                rbe->usageScopeValid = false;
        }

        wgpuBufferAddRef(buffer);
        rbe->retainedBuffers.push_back(buffer);

        if (!buffer->peBuffer)
            return;

        pe::Buffer *peBuffer = buffer->peBuffer;
        PeIndexType indexType = pwgpu::ToPeIndexType(format);
        rbe->commands.push_back([peBuffer, offset, indexType](pe::CommandBuffer *cmd, pwgpu::WebGPUBindingCache *cache)
                                { pwgpu::BindWebGPUIndexBuffer(cmd, peBuffer, static_cast<size_t>(offset), indexType, cache); });
        WGPURenderBundleOp op{};
        op.kind = WGPURenderBundleOpKind::SetIndexBuffer;
        op.buffer = peBuffer;
        op.offset = static_cast<size_t>(offset);
        op.indexType = indexType;
        rbe->nativeOps.push_back(std::move(op));
    }

    void wgpuRenderBundleEncoderDraw(WGPURenderBundleEncoder rbe, uint32_t vertexCount,
                                     uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance)
    {
        if (!EncoderOpen(rbe, "wgpuRenderBundleEncoderDraw"))
            return;
        if (!rbe->pipeline)
            return;
        if (!ValidateBindGroupCompat(rbe))
            return;
        if (!ValidateDrawVertexState(rbe->pipeline->vertexBufferLayouts, rbe->boundVertexBuffers,
                                     firstVertex, vertexCount, firstInstance, instanceCount, false))
        {
            rbe->invalid = true;
            return;
        }

        rbe->drawCount++;
        rbe->commands.push_back([vertexCount, instanceCount, firstVertex, firstInstance](pe::CommandBuffer *cmd,
                                                                                         pwgpu::WebGPUBindingCache *)
                                { pwgpu::DrawWebGPU(cmd, vertexCount, instanceCount, firstVertex, firstInstance); });
        WGPURenderBundleOp op{};
        op.kind = WGPURenderBundleOpKind::Draw;
        op.first = vertexCount;
        op.second = instanceCount;
        op.third = firstVertex;
        op.fourth = firstInstance;
        rbe->nativeOps.push_back(std::move(op));
    }

    void wgpuRenderBundleEncoderDrawIndexed(WGPURenderBundleEncoder rbe, uint32_t indexCount,
                                            uint32_t instanceCount, uint32_t firstIndex,
                                            int32_t baseVertex, uint32_t firstInstance)
    {
        if (!EncoderOpen(rbe, "wgpuRenderBundleEncoderDrawIndexed"))
            return;
        if (!rbe->indexBuffer || rbe->indexFormat == WGPUIndexFormat_Undefined)
        {
            rbe->invalid = true;
            return;
        }
        uint32_t indexSize = (rbe->indexFormat == WGPUIndexFormat_Uint16) ? 2u : 4u;
        uint64_t maxIndices = rbe->indexBufferSize / indexSize;
        uint64_t requiredEnd = static_cast<uint64_t>(firstIndex) + static_cast<uint64_t>(indexCount);
        if (requiredEnd > maxIndices)
        {
            rbe->invalid = true;
            return;
        }
        if (!rbe->pipeline)
            return;
        if (!ValidateBindGroupCompat(rbe))
            return;
        if (!ValidateDrawVertexState(rbe->pipeline->vertexBufferLayouts, rbe->boundVertexBuffers,
                                     0, 0, firstInstance, instanceCount, true))
        {
            rbe->invalid = true;
            return;
        }

        rbe->drawCount++;
        rbe->commands.push_back([indexCount, instanceCount, firstIndex, baseVertex, firstInstance](pe::CommandBuffer *cmd,
                                                                                                   pwgpu::WebGPUBindingCache *)
                                { pwgpu::DrawIndexedWebGPU(cmd, indexCount, instanceCount, firstIndex, baseVertex, firstInstance); });
        WGPURenderBundleOp op{};
        op.kind = WGPURenderBundleOpKind::DrawIndexed;
        op.first = indexCount;
        op.second = instanceCount;
        op.third = firstIndex;
        op.signedValue = baseVertex;
        op.fourth = firstInstance;
        rbe->nativeOps.push_back(std::move(op));
    }

    void wgpuRenderBundleEncoderDrawIndirect(WGPURenderBundleEncoder rbe, WGPUBuffer buffer, uint64_t offset)
    {
        if (!EncoderOpen(rbe, "wgpuRenderBundleEncoderDrawIndirect"))
            return;

        if (!buffer)
        {
            ReportEncoderValidation(rbe,
                                    "wgpuRenderBundleEncoderDrawIndirect: indirectBuffer is null");
            rbe->invalid = true;
            return;
        }
        if (buffer->device != rbe->device || buffer->invalid)
        {
            ReportEncoderValidation(rbe,
                                    "wgpuRenderBundleEncoderDrawIndirect: indirectBuffer is not "
                                    "valid to use with this encoder (cross-device or invalid "
                                    "buffer)");
            rbe->invalid = true;
            return;
        }
        if (!(buffer->usage & WGPUBufferUsage_Indirect))
        {
            ReportEncoderValidation(rbe,
                                    "wgpuRenderBundleEncoderDrawIndirect: indirectBuffer usage "
                                    "must contain INDIRECT");
            rbe->invalid = true;
            return;
        }
        if (offset % 4u != 0u)
        {
            ReportEncoderValidation(rbe,
                                    "wgpuRenderBundleEncoderDrawIndirect: indirectOffset must be "
                                    "a multiple of 4");
            rbe->invalid = true;
            return;
        }
        constexpr uint64_t kDrawArgsSize = PE_DRAW_INDIRECT_COMMAND_SIZE;
        if (offset > buffer->size || kDrawArgsSize > buffer->size - offset)
        {
            ReportEncoderValidation(rbe,
                                    "wgpuRenderBundleEncoderDrawIndirect: indirectOffset + "
                                    "sizeof(indirect draw parameters) exceeds indirectBuffer "
                                    "size");
            rbe->invalid = true;
            return;
        }

        if (!rbe->pipeline)
            return;
        if (!ValidateBindGroupCompat(rbe))
            return;
        if (!ValidateDrawBindPresence(rbe->pipeline->vertexBufferLayouts, rbe->boundVertexBuffers))
        {
            rbe->invalid = true;
            return;
        }

        {
            std::string err;
            if (!rbe->usageScope.AddBuffer(buffer, pwgpu::BufferUsageKind::Input, err))
                rbe->usageScopeValid = false;
        }

        wgpuBufferAddRef(buffer);
        rbe->retainedBuffers.push_back(buffer);

        if (!buffer->peBuffer)
            return;

        rbe->drawCount++;
        rbe->vulkanNativeEligible = false;
        rbe->commands.push_back([buffer, offset](pe::CommandBuffer *cmd, pwgpu::WebGPUBindingCache *)
                                { cmd->DrawIndirect(buffer->peBuffer, offset, 1, PE_DRAW_INDIRECT_COMMAND_SIZE); });
    }

    void wgpuRenderBundleEncoderDrawIndexedIndirect(WGPURenderBundleEncoder rbe, WGPUBuffer buffer, uint64_t offset)
    {
        if (!EncoderOpen(rbe, "wgpuRenderBundleEncoderDrawIndexedIndirect"))
            return;

        if (!buffer)
        {
            ReportEncoderValidation(rbe,
                                    "wgpuRenderBundleEncoderDrawIndexedIndirect: indirectBuffer "
                                    "is null");
            rbe->invalid = true;
            return;
        }
        if (buffer->device != rbe->device || buffer->invalid)
        {
            ReportEncoderValidation(rbe,
                                    "wgpuRenderBundleEncoderDrawIndexedIndirect: indirectBuffer "
                                    "is not valid to use with this encoder (cross-device or "
                                    "invalid buffer)");
            rbe->invalid = true;
            return;
        }
        if (!(buffer->usage & WGPUBufferUsage_Indirect))
        {
            ReportEncoderValidation(rbe,
                                    "wgpuRenderBundleEncoderDrawIndexedIndirect: indirectBuffer "
                                    "usage must contain INDIRECT");
            rbe->invalid = true;
            return;
        }
        if (offset % 4u != 0u)
        {
            ReportEncoderValidation(rbe,
                                    "wgpuRenderBundleEncoderDrawIndexedIndirect: indirectOffset "
                                    "must be a multiple of 4");
            rbe->invalid = true;
            return;
        }
        constexpr uint64_t kDrawArgsSize = PE_DRAW_INDEXED_INDIRECT_COMMAND_SIZE;
        if (offset > buffer->size || kDrawArgsSize > buffer->size - offset)
        {
            ReportEncoderValidation(rbe,
                                    "wgpuRenderBundleEncoderDrawIndexedIndirect: indirectOffset "
                                    "+ sizeof(indirect drawIndexed parameters) exceeds "
                                    "indirectBuffer size");
            rbe->invalid = true;
            return;
        }

        if (!rbe->pipeline)
            return;
        if (!ValidateBindGroupCompat(rbe))
            return;
        if (!ValidateDrawBindPresence(rbe->pipeline->vertexBufferLayouts, rbe->boundVertexBuffers))
        {
            rbe->invalid = true;
            return;
        }

        {
            std::string err;
            if (!rbe->usageScope.AddBuffer(buffer, pwgpu::BufferUsageKind::Input, err))
                rbe->usageScopeValid = false;
        }

        wgpuBufferAddRef(buffer);
        rbe->retainedBuffers.push_back(buffer);

        if (!buffer->peBuffer)
            return;

        rbe->drawCount++;
        rbe->vulkanNativeEligible = false;
        rbe->commands.push_back([buffer, offset](pe::CommandBuffer *cmd, pwgpu::WebGPUBindingCache *)
                                { cmd->DrawIndexedIndirect(buffer->peBuffer, offset, 1, PE_DRAW_INDEXED_INDIRECT_COMMAND_SIZE); });
    }

    WGPURenderBundle wgpuRenderBundleEncoderFinish(WGPURenderBundleEncoder rbe,
                                                   WGPURenderBundleDescriptor const *descriptor)
    {
        if (!rbe)
            return nullptr;

        if (rbe->finished)
        {
            ReportEncoderValidation(
                rbe, "wgpuRenderBundleEncoderFinish: render bundle encoder is already finished");
            auto *rb = new WGPURenderBundleImpl();
            rb->invalid = true;
            if (rbe->device)
            {
                rb->device = rbe->device;
                rbe->device->refCount.fetch_add(1, std::memory_order_relaxed);
            }
            return rb;
        }

        rbe->finished = true;

        bool valid = !rbe->invalid && (rbe->debugGroupDepth == 0);

        auto *rb = new WGPURenderBundleImpl();
        rb->device = rbe->device;
        if (rbe->device)
            rbe->device->refCount.fetch_add(1, std::memory_order_relaxed);

        if (descriptor && descriptor->label.data)
            rb->label = pwgpu::ToString(descriptor->label);

        if (!valid)
        {
            rb->invalid = true;
            if (rbe->debugGroupDepth != 0)
            {
                char buf[160];
                std::snprintf(buf, sizeof(buf),
                              "wgpuRenderBundleEncoderFinish: debug group stack is not empty "
                              "(%u group(s) still open)",
                              rbe->debugGroupDepth);
                ReportEncoderValidation(rbe, buf);
            }

            if (rbe->device)
            {
                const std::string &msg = rbe->deferredErrorMessage.empty()
                                             ? std::string("wgpuRenderBundleEncoderFinish: render bundle encoder is invalid")
                                             : rbe->deferredErrorMessage;
                rbe->device->reportError(WGPUErrorType_Validation, pwgpu::ToStringView(msg.c_str()));
            }

            rb->deferredErrorMessage = std::move(rbe->deferredErrorMessage);
            return rb;
        }

        rb->colorFormats = std::move(rbe->colorFormats);
        rb->depthStencilFormat = rbe->depthStencilFormat;
        rb->sampleCount = rbe->sampleCount;
        rb->depthReadOnly = rbe->depthReadOnly;
        rb->stencilReadOnly = rbe->stencilReadOnly;

        rb->drawCount = rbe->drawCount;
        rb->commands = std::move(rbe->commands);
        rb->nativeOps = std::move(rbe->nativeOps);
        rb->vulkanNativeEligible = rbe->vulkanNativeEligible;

        rb->retainedPipelines = std::move(rbe->retainedPipelines);
        rb->retainedBindGroups = std::move(rbe->retainedBindGroups);
        rb->retainedBuffers = std::move(rbe->retainedBuffers);

        rb->usageScope = std::move(rbe->usageScope);
        rb->usageScopeValid = rbe->usageScopeValid;
        // Propagate submit-time validity (W3C §3.3): bundle keeps the flag,
        // executeBundles will bubble it onto the parent render pass.
        rb->deferredResourceError = rbe->deferredResourceError;

        return rb;
    }

    void wgpuRenderBundleEncoderInsertDebugMarker(WGPURenderBundleEncoder rbe, WGPUStringView label)
    {
        if (!EncoderOpen(rbe, "wgpuRenderBundleEncoderInsertDebugMarker"))
            return;

        std::string str = pwgpu::ToString(label);
        rbe->vulkanNativeEligible = false;
        rbe->commands.push_back([str](pe::CommandBuffer *cmd, pwgpu::WebGPUBindingCache *)
                                { cmd->InsertDebugLabel(str); });
    }

    void wgpuRenderBundleEncoderPushDebugGroup(WGPURenderBundleEncoder rbe, WGPUStringView groupLabel)
    {
        if (!EncoderOpen(rbe, "wgpuRenderBundleEncoderPushDebugGroup"))
            return;
        rbe->debugGroupDepth++;

        std::string str = pwgpu::ToString(groupLabel);
        rbe->vulkanNativeEligible = false;
        rbe->commands.push_back([str](pe::CommandBuffer *cmd, pwgpu::WebGPUBindingCache *)
                                { cmd->BeginDebugRegion(str); });
    }

    void wgpuRenderBundleEncoderPopDebugGroup(WGPURenderBundleEncoder rbe)
    {
        if (!EncoderOpen(rbe, "wgpuRenderBundleEncoderPopDebugGroup"))
            return;
        if (rbe->debugGroupDepth == 0)
        {
            ReportEncoderValidation(
                rbe, "wgpuRenderBundleEncoderPopDebugGroup: debug group stack is empty");
            rbe->invalid = true;
            return;
        }
        rbe->debugGroupDepth--;

        rbe->vulkanNativeEligible = false;
        rbe->commands.push_back([](pe::CommandBuffer *cmd, pwgpu::WebGPUBindingCache *)
                                { cmd->EndDebugRegion(); });
    }

    void wgpuRenderBundleEncoderSetLabel(WGPURenderBundleEncoder rbe, WGPUStringView label)
    {
        if (rbe)
            rbe->label = pwgpu::ToString(label);
    }

} // extern "C"
