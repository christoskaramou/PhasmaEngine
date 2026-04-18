#include "RenderPass.h"
#include "CommandEncoder.h"
#include "RenderPipeline.h"
#include "PipelineLayout.h"
#include "BindGroup.h"
#include "Buffer.h"
#include "Texture.h"
#include "RenderBundle.h"
#include "QuerySet.h"
#include "Device.h"
#include "Utils.h"

extern "C" void wgpuRenderPipelineAddRef(WGPURenderPipeline);
extern "C" void wgpuRenderPipelineRelease(WGPURenderPipeline);
extern "C" void wgpuBindGroupAddRef(WGPUBindGroup);
extern "C" void wgpuBindGroupRelease(WGPUBindGroup);
extern "C" void wgpuTextureViewRelease(WGPUTextureView);
extern "C" void wgpuQuerySetRelease(WGPUQuerySet);
extern "C" void wgpuRenderBundleAddRef(WGPURenderBundle);
extern "C" void wgpuRenderBundleRelease(WGPURenderBundle);

namespace
{
    void ReportPassValidation(WGPURenderPassEncoder rpe, const char *msg)
    {
        if (rpe && rpe->device)
            rpe->device->reportError(WGPUErrorType_Validation, pwgpu::ToStringView(msg));
    }

    bool ValidateBindGroupCompat(WGPURenderPassEncoder rpe)
    {
        if (!rpe || !rpe->pipeline || !rpe->pipeline->layout)
            return true;
        auto &bgls = rpe->pipeline->layout->bindGroupLayouts;
        for (size_t i = 0; i < bgls.size(); ++i)
        {
            auto *plBgl = bgls[i];
            if (!plBgl)
                continue;
            if (i >= rpe->currentBindGroups.size())
            {
                rpe->invalid = true;
                return false;
            }
            auto *bg = rpe->currentBindGroups[i];
            if (!bg || bg->invalid || !bg->layout)
            {
                rpe->invalid = true;
                return false;
            }
            if (!BglGroupEquivalent(bg->layout, plBgl))
            {
                rpe->invalid = true;
                return false;
            }
        }
        return true;
    }

    bool PassOpen(WGPURenderPassEncoder rpe, const char *apiName)
    {
        if (!rpe)
            return false;
        if (rpe->ended)
        {
            std::string msg = std::string(apiName) + ": render pass encoder is already ended";
            ReportPassValidation(rpe, msg.c_str());
            return false;
        }
        if (rpe->parent && rpe->parent->finished)
        {
            std::string msg = std::string(apiName) + ": parent command encoder is already finished";
            ReportPassValidation(rpe, msg.c_str());
            return false;
        }
        if (rpe->invalid)
            return false;
        return true;
    }

    bool RenderingActive(WGPURenderPassEncoder rpe, const char *apiName)
    {
        if (!PassOpen(rpe, apiName))
            return false;
        if (!rpe->renderingActive)
        {
            PE_WARN("[WebGPU] %s: no active Vulkan render pass (beginRenderPass not fully wired yet)", apiName);
            return false;
        }
        return true;
    }

    bool LayoutsEqual(const std::vector<WGPUTextureFormat> &a, WGPUTextureFormat aDsFormat, uint32_t aSc,
                      const std::vector<WGPUTextureFormat> &b, WGPUTextureFormat bDsFormat, uint32_t bSc)
    {
        if (aDsFormat != bDsFormat || aSc != bSc)
            return false;
        size_t aLen = a.size(), bLen = b.size();
        while (aLen > 0 && a[aLen - 1] == WGPUTextureFormat_Undefined)
            --aLen;
        while (bLen > 0 && b[bLen - 1] == WGPUTextureFormat_Undefined)
            --bLen;
        if (aLen != bLen)
            return false;
        for (size_t i = 0; i < aLen; i++)
        {
            if (a[i] != b[i])
                return false;
        }
        return true;
    }
} // namespace

extern "C"
{

    void wgpuRenderPassEncoderAddRef(WGPURenderPassEncoder rpe)
    {
        if (rpe)
            rpe->refCount.fetch_add(1, std::memory_order_relaxed);
    }

    void wgpuRenderPassEncoderRelease(WGPURenderPassEncoder rpe)
    {
        if (!rpe)
            return;
        if (rpe->refCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
        {
            for (auto *p : rpe->retainedPipelines)
                wgpuRenderPipelineRelease(p);
            for (auto *bg : rpe->retainedBindGroups)
                wgpuBindGroupRelease(bg);
            for (auto *rb : rpe->retainedBundles)
                wgpuRenderBundleRelease(rb);
            for (auto *v : rpe->retainedViews)
                wgpuTextureViewRelease(v);
            for (auto *sv : rpe->ownedSliceViews)
                pe::ImageView::Destroy(sv);
            if (rpe->timestampQuerySet)
                wgpuQuerySetRelease(rpe->timestampQuerySet);
            if (rpe->occlusionQuerySet)
                wgpuQuerySetRelease(rpe->occlusionQuerySet);
            delete rpe;
        }
    }

    void wgpuRenderPassEncoderSetPipeline(WGPURenderPassEncoder rpe, WGPURenderPipeline pipeline)
    {
        if (!PassOpen(rpe, "wgpuRenderPassEncoderSetPipeline"))
            return;
        if (!pipeline || pipeline->invalid || pipeline->vkPipeline == VK_NULL_HANDLE)
        {
            rpe->invalid = true;
            return;
        }

        if (pipeline->device != rpe->device)
        {
            rpe->invalid = true;
            return;
        }
        // §17.1.1.4: pipeline's render targets layout must equal the pass's.
        if (!LayoutsEqual(rpe->colorFormats, rpe->depthStencilFormat, rpe->sampleCount,
                          pipeline->colorFormats, pipeline->depthStencilFormat, pipeline->sampleCount))
        {
            rpe->invalid = true;
            return;
        }
        // Pipeline must not write to a read-only depth/stencil attachment.
        if (pipeline->writesDepth && rpe->depthReadOnly)
        {
            rpe->invalid = true;
            return;
        }
        if (pipeline->writesStencil && rpe->stencilReadOnly)
        {
            rpe->invalid = true;
            return;
        }

        rpe->pipeline = pipeline;
        rpe->bindingStateInvalidated = false;

        wgpuRenderPipelineAddRef(pipeline);
        rpe->retainedPipelines.push_back(pipeline);

        rpe->cmd->ApiHandle().bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline->vkPipeline);
    }

    void wgpuRenderPassEncoderSetBindGroup(WGPURenderPassEncoder rpe, uint32_t groupIndex,
                                           WGPUBindGroup group,
                                           size_t dynamicOffsetCount, uint32_t const *dynamicOffsets)
    {
        if (!PassOpen(rpe, "wgpuRenderPassEncoderSetBindGroup"))
            return;

        if (rpe->device && groupIndex >= rpe->device->limits.maxBindGroups)
        {
            rpe->invalid = true;
            return;
        }

        if (group)
        {
            if (group->device != rpe->device)
            {
                rpe->invalid = true;
                return;
            }
            if (group->invalid)
            {
                rpe->invalid = true;
                return;
            }
        }

        if (group)
        {
            for (auto &use : group->textureUses)
            {
                std::string err;
                if (!rpe->usageScope.AddView(use.view, use.kind, err))
                    rpe->usageScopeValid = false;
            }
            wgpuBindGroupAddRef(group);
            rpe->retainedBindGroups.push_back(group);
        }

        if (rpe->device && groupIndex < rpe->device->limits.maxBindGroups)
        {
            if (rpe->currentBindGroups.size() <= groupIndex)
                rpe->currentBindGroups.resize(groupIndex + 1, nullptr);
            rpe->currentBindGroups[groupIndex] = group;
        }

        if (group && group->layout)
        {
            if (static_cast<uint32_t>(dynamicOffsetCount) != group->layout->dynamicOffsetCount)
            {
                rpe->invalid = true;
                return;
            }
            if (dynamicOffsetCount > 0 && !dynamicOffsets)
            {
                rpe->invalid = true;
                return;
            }
            if (rpe->device && dynamicOffsetCount > 0 && dynamicOffsets)
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
                    uint32_t align = isUniform ? rpe->device->limits.minUniformBufferOffsetAlignment
                                               : rpe->device->limits.minStorageBufferOffsetAlignment;
                    if (align > 0 && offset % align != 0)
                    {
                        rpe->invalid = true;
                        return;
                    }
                    if (dyn.buffer)
                    {
                        uint64_t bufSize = dyn.buffer->size;
                        uint64_t effOffset = dyn.baseOffset + static_cast<uint64_t>(offset);
                        if (effOffset > bufSize || dyn.bindingSize > bufSize - effOffset)
                        {
                            rpe->invalid = true;
                            return;
                        }
                    }
                }
            }
        }

        if (!rpe->pipeline || !rpe->pipeline->layout)
            return;

        if (!group || !group->descriptor)
            return;

        auto &bgls = rpe->pipeline->layout->bindGroupLayouts;
        if (groupIndex >= bgls.size() || !bgls[groupIndex])
            return;
        if (!BglGroupEquivalent(group->layout, bgls[groupIndex]))
            return;

        vk::PipelineLayout vkLayout(rpe->pipeline->layout->vkLayout);
        vk::DescriptorSet ds = group->descriptor->ApiHandle();

        rpe->cmd->ApiHandle().bindDescriptorSets(
            vk::PipelineBindPoint::eGraphics,
            vkLayout, groupIndex,
            1, &ds,
            static_cast<uint32_t>(dynamicOffsetCount), dynamicOffsets);
    }

    void wgpuRenderPassEncoderSetVertexBuffer(WGPURenderPassEncoder rpe, uint32_t slot,
                                              WGPUBuffer buffer, uint64_t offset, uint64_t size)
    {
        if (!RenderingActive(rpe, "wgpuRenderPassEncoderSetVertexBuffer"))
            return;

        if (rpe->device && slot >= rpe->device->limits.maxVertexBuffers)
        {
            rpe->invalid = true;
            return;
        }
        if (offset % 4u != 0u)
        {
            rpe->invalid = true;
            return;
        }
        uint64_t bufferSize = buffer ? buffer->size : 0u;
        if (size == WGPU_WHOLE_SIZE)
            size = (offset > bufferSize) ? 0u : (bufferSize - offset);
        if (offset > bufferSize || size > bufferSize - offset)
        {
            rpe->invalid = true;
            return;
        }

        if (!buffer)
        {
            if (slot < rpe->boundVertexBuffers.size())
                rpe->boundVertexBuffers[slot] = {};
            return;
        }

        if (buffer->device != rpe->device || buffer->invalid)
        {
            rpe->invalid = true;
            return;
        }
        if (!(buffer->usage & WGPUBufferUsage_Vertex))
        {
            rpe->invalid = true;
            return;
        }

        if (rpe->boundVertexBuffers.size() <= slot)
            rpe->boundVertexBuffers.resize(slot + 1);
        rpe->boundVertexBuffers[slot].bound = true;
        rpe->boundVertexBuffers[slot].size = size;

        // Destroyed buffer defers to queue.submit() per spec.
        if (buffer->internalState == BufferInternalState::Destroyed || !buffer->peBuffer)
        {
            rpe->usedBuffers.push_back(buffer);
            return;
        }

        vk::Buffer vkBuf = buffer->peBuffer->ApiHandle();
        vk::DeviceSize vkOffset = static_cast<vk::DeviceSize>(offset);
        rpe->cmd->ApiHandle().bindVertexBuffers(slot, 1, &vkBuf, &vkOffset);
        rpe->usedBuffers.push_back(buffer);
    }

    void wgpuRenderPassEncoderSetIndexBuffer(WGPURenderPassEncoder rpe, WGPUBuffer buffer,
                                             WGPUIndexFormat format, uint64_t offset, uint64_t size)
    {
        if (!RenderingActive(rpe, "wgpuRenderPassEncoderSetIndexBuffer"))
            return;
        auto fail = [&]()
        { rpe->invalid = true; };
        if (!buffer)
        {
            fail();
            return;
        }
        if (buffer->device != rpe->device)
        {
            fail();
            return;
        }
        if (buffer->invalid)
        {
            fail();
            return;
        }
        if (!(buffer->usage & WGPUBufferUsage_Index))
        {
            fail();
            return;
        }
        uint32_t indexSize = (format == WGPUIndexFormat_Uint16) ? 2u : 4u;
        if (offset % indexSize != 0)
        {
            fail();
            return;
        }
        uint64_t boundSize = size;
        if (boundSize == WGPU_WHOLE_SIZE)
        {
            if (offset > buffer->size)
            {
                fail();
                return;
            }
            boundSize = buffer->size - offset;
        }
        else
        {
            if (boundSize > buffer->size || offset > buffer->size - boundSize)
            {
                fail();
                return;
            }
        }

        rpe->indexBuffer = buffer;
        rpe->indexFormat = format;
        rpe->indexBufferSize = boundSize;

        if (buffer->internalState == BufferInternalState::Destroyed || !buffer->peBuffer)
        {
            rpe->usedBuffers.push_back(buffer);
            return;
        }

        vk::IndexType indexType = (format == WGPUIndexFormat_Uint16) ? vk::IndexType::eUint16 : vk::IndexType::eUint32;
        rpe->cmd->ApiHandle().bindIndexBuffer(buffer->peBuffer->ApiHandle(), offset, indexType);
        rpe->usedBuffers.push_back(buffer);
    }

    void wgpuRenderPassEncoderDraw(WGPURenderPassEncoder rpe, uint32_t vertexCount,
                                   uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance)
    {
        if (!RenderingActive(rpe, "wgpuRenderPassEncoderDraw"))
            return;
        if (!rpe->pipeline || rpe->bindingStateInvalidated)
            return;
        if (!ValidateBindGroupCompat(rpe))
            return;
        if (!ValidateDrawVertexState(rpe->pipeline->vertexBufferLayouts, rpe->boundVertexBuffers,
                                     firstVertex, vertexCount, firstInstance, instanceCount, false))
        {
            rpe->invalid = true;
            return;
        }
        rpe->drawCount++;
        rpe->cmd->ApiHandle().draw(vertexCount, instanceCount, firstVertex, firstInstance);
    }

    void wgpuRenderPassEncoderDrawIndexed(WGPURenderPassEncoder rpe, uint32_t indexCount,
                                          uint32_t instanceCount, uint32_t firstIndex,
                                          int32_t baseVertex, uint32_t firstInstance)
    {
        if (!RenderingActive(rpe, "wgpuRenderPassEncoderDrawIndexed"))
            return;
        if (!rpe->indexBuffer || rpe->indexFormat == WGPUIndexFormat_Undefined)
        {
            rpe->invalid = true;
            return;
        }
        uint32_t indexSize = (rpe->indexFormat == WGPUIndexFormat_Uint16) ? 2u : 4u;
        uint64_t maxIndices = rpe->indexBufferSize / indexSize;
        uint64_t requiredEnd = static_cast<uint64_t>(firstIndex) + static_cast<uint64_t>(indexCount);
        if (requiredEnd > maxIndices)
        {
            rpe->invalid = true;
            return;
        }
        if (!rpe->pipeline || rpe->bindingStateInvalidated)
            return;
        if (!ValidateBindGroupCompat(rpe))
            return;
        if (!ValidateDrawVertexState(rpe->pipeline->vertexBufferLayouts, rpe->boundVertexBuffers,
                                     0, 0, firstInstance, instanceCount, true))
        {
            rpe->invalid = true;
            return;
        }
        rpe->drawCount++;
        rpe->cmd->ApiHandle().drawIndexed(indexCount, instanceCount, firstIndex, baseVertex, firstInstance);
    }

    void wgpuRenderPassEncoderDrawIndirect(WGPURenderPassEncoder rpe, WGPUBuffer buffer, uint64_t offset)
    {
        if (!RenderingActive(rpe, "wgpuRenderPassEncoderDrawIndirect"))
            return;

        if (!buffer)
        {
            rpe->invalid = true;
            return;
        }
        if (buffer->device != rpe->device || buffer->invalid)
        {
            rpe->invalid = true;
            return;
        }
        if (!(buffer->usage & WGPUBufferUsage_Indirect))
        {
            rpe->invalid = true;
            return;
        }
        if (offset % 4u != 0u)
        {
            rpe->invalid = true;
            return;
        }
        constexpr uint64_t kDrawArgsSize = sizeof(VkDrawIndirectCommand);
        if (offset > buffer->size || kDrawArgsSize > buffer->size - offset)
        {
            rpe->invalid = true;
            return;
        }

        if (!rpe->pipeline || rpe->bindingStateInvalidated)
            return;
        if (!ValidateBindGroupCompat(rpe))
            return;
        if (!ValidateDrawBindPresence(rpe->pipeline->vertexBufferLayouts, rpe->boundVertexBuffers))
        {
            rpe->invalid = true;
            return;
        }
        if (buffer->internalState == BufferInternalState::Destroyed || !buffer->peBuffer)
        {
            rpe->usedBuffers.push_back(buffer);
            return;
        }

        rpe->drawCount++;
        rpe->cmd->ApiHandle().drawIndirect(buffer->peBuffer->ApiHandle(), offset, 1, sizeof(VkDrawIndirectCommand));
        rpe->usedBuffers.push_back(buffer);
    }

    void wgpuRenderPassEncoderDrawIndexedIndirect(WGPURenderPassEncoder rpe, WGPUBuffer buffer, uint64_t offset)
    {
        if (!RenderingActive(rpe, "wgpuRenderPassEncoderDrawIndexedIndirect"))
            return;

        if (!buffer)
        {
            rpe->invalid = true;
            return;
        }
        if (buffer->device != rpe->device || buffer->invalid)
        {
            rpe->invalid = true;
            return;
        }
        if (!(buffer->usage & WGPUBufferUsage_Indirect))
        {
            rpe->invalid = true;
            return;
        }
        if (offset % 4u != 0u)
        {
            rpe->invalid = true;
            return;
        }
        constexpr uint64_t kDrawArgsSize = sizeof(VkDrawIndexedIndirectCommand);
        if (offset > buffer->size || kDrawArgsSize > buffer->size - offset)
        {
            rpe->invalid = true;
            return;
        }

        if (!rpe->pipeline || rpe->bindingStateInvalidated)
            return;
        if (!ValidateBindGroupCompat(rpe))
            return;
        if (!ValidateDrawBindPresence(rpe->pipeline->vertexBufferLayouts, rpe->boundVertexBuffers))
        {
            rpe->invalid = true;
            return;
        }
        if (buffer->internalState == BufferInternalState::Destroyed || !buffer->peBuffer)
        {
            rpe->usedBuffers.push_back(buffer);
            return;
        }

        rpe->drawCount++;
        rpe->cmd->ApiHandle().drawIndexedIndirect(buffer->peBuffer->ApiHandle(), offset, 1, sizeof(VkDrawIndexedIndirectCommand));
        rpe->usedBuffers.push_back(buffer);
    }

    void wgpuRenderPassEncoderSetViewport(WGPURenderPassEncoder rpe,
                                          float x, float y, float width, float height,
                                          float minDepth, float maxDepth)
    {
        if (!RenderingActive(rpe, "wgpuRenderPassEncoderSetViewport"))
            return;

        // §17.2.2: maxViewportRange = maxTextureDimension2D * 2.
        const float maxDim = rpe->device ? static_cast<float>(rpe->device->limits.maxTextureDimension2D) : 0.0f;
        const float maxRange = maxDim * 2.0f;
        if (!(width >= 0.0f && width <= maxDim) || !(height >= 0.0f && height <= maxDim) ||
            !(x >= -maxRange) || !(y >= -maxRange) ||
            !(x + width <= maxRange - 1.0f) || !(y + height <= maxRange - 1.0f) ||
            !(minDepth >= 0.0f && minDepth <= 1.0f) ||
            !(maxDepth >= 0.0f && maxDepth <= 1.0f) ||
            !(minDepth <= maxDepth))
        {
            rpe->invalid = true;
            return;
        }

        vk::Viewport vp{x, y, width, height, minDepth, maxDepth};
        rpe->cmd->ApiHandle().setViewport(0, 1, &vp);
    }

    void wgpuRenderPassEncoderSetScissorRect(WGPURenderPassEncoder rpe,
                                             uint32_t x, uint32_t y, uint32_t width, uint32_t height)
    {
        if (!RenderingActive(rpe, "wgpuRenderPassEncoderSetScissorRect"))
            return;

        if (x > rpe->attachmentWidth || width > rpe->attachmentWidth - x ||
            y > rpe->attachmentHeight || height > rpe->attachmentHeight - y)
        {
            rpe->invalid = true;
            return;
        }

        vk::Rect2D scissor{{static_cast<int32_t>(x), static_cast<int32_t>(y)}, {width, height}};
        rpe->cmd->ApiHandle().setScissor(0, 1, &scissor);
    }

    void wgpuRenderPassEncoderSetBlendConstant(WGPURenderPassEncoder rpe, WGPUColor const *color)
    {
        if (!RenderingActive(rpe, "wgpuRenderPassEncoderSetBlendConstant"))
            return;
        if (!color)
            return;

        float constants[4] = {static_cast<float>(color->r), static_cast<float>(color->g),
                              static_cast<float>(color->b), static_cast<float>(color->a)};
        rpe->cmd->ApiHandle().setBlendConstants(constants);
    }

    void wgpuRenderPassEncoderSetStencilReference(WGPURenderPassEncoder rpe, uint32_t reference)
    {
        if (!RenderingActive(rpe, "wgpuRenderPassEncoderSetStencilReference"))
            return;

        rpe->cmd->ApiHandle().setStencilReference(vk::StencilFaceFlagBits::eFrontAndBack, reference);
    }

    void wgpuRenderPassEncoderBeginOcclusionQuery(WGPURenderPassEncoder rpe, uint32_t queryIndex)
    {
        if (!RenderingActive(rpe, "wgpuRenderPassEncoderBeginOcclusionQuery"))
            return;

        if (!rpe->occlusionQuerySet)
        {
            rpe->invalid = true;
            return;
        }
        if (rpe->occlusionQueryActive)
        {
            rpe->invalid = true;
            return;
        }
        if (queryIndex >= rpe->occlusionQuerySet->count)
        {
            rpe->invalid = true;
            return;
        }
        for (uint32_t used : rpe->usedOcclusionIndices)
        {
            if (used == queryIndex)
            {
                rpe->invalid = true;
                return;
            }
        }

        rpe->occlusionQueryActive = true;
        rpe->usedOcclusionIndices.push_back(queryIndex);

        if (rpe->occlusionQuerySet->queryPool)
            rpe->cmd->ApiHandle().beginQuery(
                rpe->occlusionQuerySet->queryPool, queryIndex, vk::QueryControlFlags{});
    }

    void wgpuRenderPassEncoderEndOcclusionQuery(WGPURenderPassEncoder rpe)
    {
        if (!RenderingActive(rpe, "wgpuRenderPassEncoderEndOcclusionQuery"))
            return;

        if (!rpe->occlusionQueryActive)
        {
            rpe->invalid = true;
            return;
        }

        rpe->occlusionQueryActive = false;

        uint32_t lastIndex = rpe->usedOcclusionIndices.back();
        if (rpe->occlusionQuerySet && rpe->occlusionQuerySet->queryPool)
            rpe->cmd->ApiHandle().endQuery(
                rpe->occlusionQuerySet->queryPool, lastIndex);
    }

    void wgpuRenderPassEncoderExecuteBundles(WGPURenderPassEncoder rpe,
                                             size_t bundleCount, WGPURenderBundle const *bundles)
    {
        if (!RenderingActive(rpe, "wgpuRenderPassEncoderExecuteBundles"))
            return;

        for (size_t i = 0; i < bundleCount; i++)
        {
            auto *bundle = bundles[i];
            if (!bundle || bundle->invalid)
            {
                PE_WARN("[WebGPU] executeBundles: bundle %zu is invalid", i);
                rpe->invalid = true;
                return;
            }
            if (bundle->device != rpe->device)
            {
                rpe->invalid = true;
                return;
            }

            if (!LayoutsEqual(rpe->colorFormats, rpe->depthStencilFormat, rpe->sampleCount,
                              bundle->colorFormats, bundle->depthStencilFormat, bundle->sampleCount))
            {
                rpe->invalid = true;
                return;
            }

            if (rpe->depthReadOnly && !bundle->depthReadOnly)
            {
                rpe->invalid = true;
                return;
            }

            if (rpe->stencilReadOnly && !bundle->stencilReadOnly)
            {
                rpe->invalid = true;
                return;
            }
        }

        for (size_t i = 0; i < bundleCount; i++)
        {
            auto *bundle = bundles[i];

            wgpuRenderBundleAddRef(bundle);
            rpe->retainedBundles.push_back(bundle);

            if (!bundle->usageScopeValid)
                rpe->usageScopeValid = false;
            std::string err;
            if (!rpe->usageScope.MergeFrom(bundle->usageScope, err))
                rpe->usageScopeValid = false;

            rpe->drawCount += bundle->drawCount;

            vk::CommandBuffer vkCmd = rpe->cmd->ApiHandle();
            for (auto &command : bundle->commands)
                command(vkCmd);
        }

        rpe->pipeline = nullptr;
        rpe->bindingStateInvalidated = true;
    }

    void wgpuRenderPassEncoderInsertDebugMarker(WGPURenderPassEncoder rpe, WGPUStringView label)
    {
        if (!PassOpen(rpe, "wgpuRenderPassEncoderInsertDebugMarker"))
            return;
        rpe->cmd->InsertDebugLabel(pwgpu::ToString(label));
    }

    void wgpuRenderPassEncoderPushDebugGroup(WGPURenderPassEncoder rpe, WGPUStringView groupLabel)
    {
        if (!PassOpen(rpe, "wgpuRenderPassEncoderPushDebugGroup"))
            return;
        rpe->debugGroupDepth++;
        rpe->cmd->BeginDebugRegion(pwgpu::ToString(groupLabel));
    }

    void wgpuRenderPassEncoderPopDebugGroup(WGPURenderPassEncoder rpe)
    {
        if (!PassOpen(rpe, "wgpuRenderPassEncoderPopDebugGroup"))
            return;
        if (rpe->debugGroupDepth == 0)
        {
            rpe->invalid = true;
            return;
        }
        rpe->debugGroupDepth--;
        rpe->cmd->EndDebugRegion();
    }

    void wgpuRenderPassEncoderEnd(WGPURenderPassEncoder rpe)
    {
        if (!rpe)
            return;
        if (rpe->ended)
        {
            ReportPassValidation(rpe, "RenderPassEncoder.end(): pass is already ended");
            return;
        }
        if (rpe->parent && rpe->parent->finished)
        {
            ReportPassValidation(rpe, "RenderPassEncoder.end(): parent command encoder is already finished");
            rpe->ended = true;
            return;
        }
        if (!rpe->wasOpened)
        {
            ReportPassValidation(rpe, "RenderPassEncoder.end(): pass was never opened (invalid begin)");
            if (rpe->parent)
                rpe->parent->invalid = true;
            rpe->ended = true;
            return;
        }

        if (rpe->drawCount > rpe->maxDrawCount)
            rpe->invalid = true;

        {
            const bool passInvalidForEncoder =
                (rpe->invalid && !rpe->deferredResourceError) ||
                !rpe->usageScopeValid || rpe->debugGroupDepth != 0 || rpe->occlusionQueryActive;
            if (passInvalidForEncoder && rpe->parent)
                rpe->parent->invalid = true;
        }

        if (rpe->debugGroupDepth != 0)
            PE_WARN("[WebGPU] wgpuRenderPassEncoderEnd: %u debug group(s) still open", rpe->debugGroupDepth);

        if (rpe->occlusionQueryActive)
        {
            PE_WARN("[WebGPU] wgpuRenderPassEncoderEnd: auto-closing active occlusion query");
            uint32_t lastIndex = rpe->usedOcclusionIndices.back();
            rpe->cmd->ApiHandle().endQuery(rpe->occlusionQuerySet->queryPool, lastIndex);
            rpe->occlusionQueryActive = false;
        }

        if (rpe->renderingActive)
        {
            rpe->cmd->ApiHandle().endRendering();
            rpe->renderingActive = false;
        }

        if (rpe->timestampQuerySet && rpe->endTimestampIndex != UINT32_MAX &&
            rpe->endTimestampIndex < rpe->timestampQuerySet->count)
        {
            rpe->cmd->ApiHandle().writeTimestamp2(
                vk::PipelineStageFlagBits2::eAllCommands,
                rpe->timestampQuerySet->queryPool, rpe->endTimestampIndex);
        }

        if (rpe->parent)
        {
            rpe->parent->retained.renderPipelines.insert(
                rpe->parent->retained.renderPipelines.end(),
                rpe->retainedPipelines.begin(), rpe->retainedPipelines.end());
            rpe->retainedPipelines.clear();

            rpe->parent->retained.bindGroups.insert(
                rpe->parent->retained.bindGroups.end(),
                rpe->retainedBindGroups.begin(), rpe->retainedBindGroups.end());
            rpe->retainedBindGroups.clear();

            rpe->parent->retained.renderBundles.insert(
                rpe->parent->retained.renderBundles.end(),
                rpe->retainedBundles.begin(), rpe->retainedBundles.end());
            rpe->retainedBundles.clear();

            rpe->parent->retained.usedBuffers.insert(
                rpe->parent->retained.usedBuffers.end(),
                rpe->usedBuffers.begin(), rpe->usedBuffers.end());
            rpe->usedBuffers.clear();

            rpe->parent->retained.textureViews.insert(
                rpe->parent->retained.textureViews.end(),
                rpe->retainedViews.begin(), rpe->retainedViews.end());
            rpe->retainedViews.clear();

            if (rpe->timestampQuerySet)
            {
                rpe->parent->retained.querySets.push_back(rpe->timestampQuerySet);
                rpe->timestampQuerySet = nullptr;
            }

            if (rpe->occlusionQuerySet)
            {
                rpe->parent->retained.querySets.push_back(rpe->occlusionQuerySet);
                rpe->occlusionQuerySet = nullptr;
            }

            rpe->parent->hasOpenPass = false;
        }

        rpe->ended = true;
    }

    void wgpuRenderPassEncoderSetLabel(WGPURenderPassEncoder rpe, WGPUStringView label)
    {
        if (rpe)
            rpe->label = pwgpu::ToString(label);
    }

} // extern "C"
