#include "RenderPass.h"
#include "CommandEncoder.h"
#include "RenderPipeline.h"
#include "PipelineLayout.h"
#include "BindGroup.h"
#include "Buffer.h"
#include "RenderBundle.h"
#include "QuerySet.h"
#include "Device.h"
#include "Utils.h"

extern "C" void wgpuRenderPipelineAddRef(WGPURenderPipeline);
extern "C" void wgpuRenderPipelineRelease(WGPURenderPipeline);
extern "C" void wgpuBindGroupAddRef(WGPUBindGroup);
extern "C" void wgpuBindGroupRelease(WGPUBindGroup);

namespace
{
    bool PassOpen(WGPURenderPassEncoder rpe, const char *apiName)
    {
        if (!rpe || rpe->ended)
        {
            PE_WARN("[WebGPU] %s: render pass encoder is already ended", apiName);
            return false;
        }
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
            delete rpe;
        }
    }

    // ---- §17.1 setPipeline (state stored for §14 bind group binding) ----

    void wgpuRenderPassEncoderSetPipeline(WGPURenderPassEncoder rpe, WGPURenderPipeline pipeline)
    {
        if (!PassOpen(rpe, "wgpuRenderPassEncoderSetPipeline"))
            return;
        if (!pipeline || pipeline->invalid || pipeline->vkPipeline == VK_NULL_HANDLE)
            return;

        rpe->pipeline = pipeline;

        wgpuRenderPipelineAddRef(pipeline);
        rpe->retainedPipelines.push_back(pipeline);

        rpe->cmd->ApiHandle().bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline->vkPipeline);
    }

    // ---- §14.1 setBindGroup ----

    void wgpuRenderPassEncoderSetBindGroup(WGPURenderPassEncoder rpe, uint32_t groupIndex,
                                           WGPUBindGroup group,
                                           size_t dynamicOffsetCount, uint32_t const *dynamicOffsets)
    {
        if (!PassOpen(rpe, "wgpuRenderPassEncoderSetBindGroup"))
            return;
        if (!rpe->pipeline || !rpe->pipeline->layout)
            return;

        if (rpe->device && groupIndex >= rpe->device->limits.maxBindGroups)
            return;

        if (!group || group->invalid || !group->descriptor)
            return;

        auto &bgls = rpe->pipeline->layout->bindGroupLayouts;
        if (groupIndex >= bgls.size() || !bgls[groupIndex])
            return;
        if (group->layout != bgls[groupIndex])
        {
            PE_WARN("[WebGPU] setBindGroup: bind group layout does not match pipeline layout at index %u", groupIndex);
            return;
        }

        if (static_cast<uint32_t>(dynamicOffsetCount) != group->layout->dynamicOffsetCount)
        {
            PE_WARN("[WebGPU] setBindGroup: dynamicOffsetCount %zu != expected %u",
                    dynamicOffsetCount, group->layout->dynamicOffsetCount);
            return;
        }

        if (rpe->device && dynamicOffsetCount > 0 && dynamicOffsets)
        {
            uint32_t dynIdx = 0;
            for (auto &entry : group->layout->entries)
            {
                if (entry.buffer.hasDynamicOffset)
                {
                    if (dynIdx >= dynamicOffsetCount)
                        break;
                    uint32_t offset = dynamicOffsets[dynIdx];
                    if (entry.buffer.type == WGPUBufferBindingType_Uniform)
                    {
                        if (offset % rpe->device->limits.minUniformBufferOffsetAlignment != 0)
                            return;
                    }
                    else
                    {
                        if (offset % rpe->device->limits.minStorageBufferOffsetAlignment != 0)
                            return;
                    }
                    dynIdx++;
                }
            }
        }

        wgpuBindGroupAddRef(group);
        rpe->retainedBindGroups.push_back(group);

        vk::PipelineLayout vkLayout(rpe->pipeline->layout->vkLayout);
        vk::DescriptorSet ds = group->descriptor->ApiHandle();

        rpe->cmd->ApiHandle().bindDescriptorSets(
            vk::PipelineBindPoint::eGraphics,
            vkLayout, groupIndex,
            1, &ds,
            static_cast<uint32_t>(dynamicOffsetCount), dynamicOffsets);
    }

    // ---- §17 vertex/index buffers ----

    void wgpuRenderPassEncoderSetVertexBuffer(WGPURenderPassEncoder rpe, uint32_t slot,
                                              WGPUBuffer buffer, uint64_t offset, uint64_t size)
    {
        if (!RenderingActive(rpe, "wgpuRenderPassEncoderSetVertexBuffer"))
            return;
        if (!buffer || !buffer->peBuffer || buffer->destroyed)
            return;
        if (!(buffer->usage & WGPUBufferUsage_Vertex))
            return;

        vk::Buffer vkBuf = buffer->peBuffer->ApiHandle();
        vk::DeviceSize vkOffset = static_cast<vk::DeviceSize>(offset);
        rpe->cmd->ApiHandle().bindVertexBuffers(slot, 1, &vkBuf, &vkOffset);
    }

    void wgpuRenderPassEncoderSetIndexBuffer(WGPURenderPassEncoder rpe, WGPUBuffer buffer,
                                             WGPUIndexFormat format, uint64_t offset, uint64_t size)
    {
        if (!RenderingActive(rpe, "wgpuRenderPassEncoderSetIndexBuffer"))
            return;
        if (!buffer || !buffer->peBuffer || buffer->destroyed)
            return;
        if (!(buffer->usage & WGPUBufferUsage_Index))
            return;

        vk::IndexType indexType = (format == WGPUIndexFormat_Uint16) ? vk::IndexType::eUint16 : vk::IndexType::eUint32;
        rpe->cmd->ApiHandle().bindIndexBuffer(buffer->peBuffer->ApiHandle(), offset, indexType);
    }

    // ---- §17 draw commands ----

    void wgpuRenderPassEncoderDraw(WGPURenderPassEncoder rpe, uint32_t vertexCount,
                                   uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance)
    {
        if (!RenderingActive(rpe, "wgpuRenderPassEncoderDraw"))
            return;
        if (!rpe->pipeline)
            return;
        rpe->cmd->ApiHandle().draw(vertexCount, instanceCount, firstVertex, firstInstance);
    }

    void wgpuRenderPassEncoderDrawIndexed(WGPURenderPassEncoder rpe, uint32_t indexCount,
                                          uint32_t instanceCount, uint32_t firstIndex,
                                          int32_t baseVertex, uint32_t firstInstance)
    {
        if (!RenderingActive(rpe, "wgpuRenderPassEncoderDrawIndexed"))
            return;
        if (!rpe->pipeline)
            return;
        rpe->cmd->ApiHandle().drawIndexed(indexCount, instanceCount, firstIndex, baseVertex, firstInstance);
    }

    void wgpuRenderPassEncoderDrawIndirect(WGPURenderPassEncoder rpe, WGPUBuffer buffer, uint64_t offset)
    {
        if (!RenderingActive(rpe, "wgpuRenderPassEncoderDrawIndirect"))
            return;
        if (!rpe->pipeline)
            return;
        if (!buffer || !buffer->peBuffer || buffer->destroyed)
            return;
        if (!(buffer->usage & WGPUBufferUsage_Indirect))
            return;
        if (offset + sizeof(VkDrawIndirectCommand) > buffer->size)
            return;
        if (offset % 4 != 0)
            return;

        rpe->cmd->ApiHandle().drawIndirect(buffer->peBuffer->ApiHandle(), offset, 1, sizeof(VkDrawIndirectCommand));
    }

    void wgpuRenderPassEncoderDrawIndexedIndirect(WGPURenderPassEncoder rpe, WGPUBuffer buffer, uint64_t offset)
    {
        if (!RenderingActive(rpe, "wgpuRenderPassEncoderDrawIndexedIndirect"))
            return;
        if (!rpe->pipeline)
            return;
        if (!buffer || !buffer->peBuffer || buffer->destroyed)
            return;
        if (!(buffer->usage & WGPUBufferUsage_Indirect))
            return;
        if (offset + sizeof(VkDrawIndexedIndirectCommand) > buffer->size)
            return;
        if (offset % 4 != 0)
            return;

        rpe->cmd->ApiHandle().drawIndexedIndirect(buffer->peBuffer->ApiHandle(), offset, 1, sizeof(VkDrawIndexedIndirectCommand));
    }

    // ---- §17 dynamic state ----

    void wgpuRenderPassEncoderSetViewport(WGPURenderPassEncoder rpe,
                                          float x, float y, float width, float height,
                                          float minDepth, float maxDepth)
    {
        if (!RenderingActive(rpe, "wgpuRenderPassEncoderSetViewport"))
            return;

        vk::Viewport vp{x, y, width, height, minDepth, maxDepth};
        rpe->cmd->ApiHandle().setViewport(0, 1, &vp);
    }

    void wgpuRenderPassEncoderSetScissorRect(WGPURenderPassEncoder rpe,
                                             uint32_t x, uint32_t y, uint32_t width, uint32_t height)
    {
        if (!RenderingActive(rpe, "wgpuRenderPassEncoderSetScissorRect"))
            return;

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

    // ---- §17 occlusion queries (stub — full impl in Phase 13) ----

    void wgpuRenderPassEncoderBeginOcclusionQuery(WGPURenderPassEncoder rpe, uint32_t queryIndex)
    {
        (void)rpe;
        (void)queryIndex;
    }

    void wgpuRenderPassEncoderEndOcclusionQuery(WGPURenderPassEncoder rpe)
    {
        (void)rpe;
    }

    // ---- §18 bundles (stub — Phase 11) ----

    void wgpuRenderPassEncoderExecuteBundles(WGPURenderPassEncoder rpe,
                                             size_t bundleCount, WGPURenderBundle const *bundles)
    {
        (void)rpe;
        (void)bundleCount;
        (void)bundles;
    }

    // ---- §15 Debug markers ----

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
            PE_WARN("[WebGPU] popDebugGroup: no matching pushDebugGroup");
            return;
        }
        rpe->debugGroupDepth--;
        rpe->cmd->EndDebugRegion();
    }

    // ---- §17 end ----

    void wgpuRenderPassEncoderEnd(WGPURenderPassEncoder rpe)
    {
        if (!rpe || rpe->ended)
            return;

        if (rpe->debugGroupDepth != 0)
            PE_WARN("[WebGPU] wgpuRenderPassEncoderEnd: %u debug group(s) still open", rpe->debugGroupDepth);

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
