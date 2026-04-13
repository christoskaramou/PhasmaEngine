#include "ComputePass.h"
#include "CommandEncoder.h"
#include "ComputePipeline.h"
#include "PipelineLayout.h"
#include "BindGroup.h"
#include "Buffer.h"
#include "QuerySet.h"
#include "Device.h"
#include "Utils.h"

extern "C" void wgpuComputePipelineAddRef(WGPUComputePipeline);
extern "C" void wgpuComputePipelineRelease(WGPUComputePipeline);
extern "C" void wgpuBindGroupAddRef(WGPUBindGroup);
extern "C" void wgpuBindGroupRelease(WGPUBindGroup);
extern "C" void wgpuQuerySetAddRef(WGPUQuerySet);
extern "C" void wgpuQuerySetRelease(WGPUQuerySet);

namespace
{
    bool PassOpen(WGPUComputePassEncoder cpe, const char *apiName)
    {
        if (!cpe || cpe->ended)
        {
            PE_WARN("[WebGPU] %s: compute pass encoder is already ended", apiName);
            return false;
        }
        return true;
    }
} // namespace

extern "C"
{

    void wgpuComputePassEncoderAddRef(WGPUComputePassEncoder cpe)
    {
        if (cpe)
            cpe->refCount.fetch_add(1, std::memory_order_relaxed);
    }

    void wgpuComputePassEncoderRelease(WGPUComputePassEncoder cpe)
    {
        if (!cpe)
            return;
        if (cpe->refCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
        {
            for (auto *p : cpe->retainedPipelines)
                wgpuComputePipelineRelease(p);
            for (auto *bg : cpe->retainedBindGroups)
                wgpuBindGroupRelease(bg);
            if (cpe->timestampQuerySet)
                wgpuQuerySetRelease(cpe->timestampQuerySet);
            delete cpe;
        }
    }

    // ---- §16.1.2 setPipeline ----

    void wgpuComputePassEncoderSetPipeline(WGPUComputePassEncoder cpe, WGPUComputePipeline pipeline)
    {
        if (!PassOpen(cpe, "wgpuComputePassEncoderSetPipeline"))
            return;
        if (!pipeline || pipeline->invalid || pipeline->vkPipeline == VK_NULL_HANDLE)
            return;

        cpe->pipeline = pipeline;

        wgpuComputePipelineAddRef(pipeline);
        cpe->retainedPipelines.push_back(pipeline);

        cpe->cmd->ApiHandle().bindPipeline(vk::PipelineBindPoint::eCompute, pipeline->vkPipeline);
    }

    // ---- §14.1 setBindGroup ----

    void wgpuComputePassEncoderSetBindGroup(WGPUComputePassEncoder cpe, uint32_t groupIndex,
                                            WGPUBindGroup group,
                                            size_t dynamicOffsetCount, uint32_t const *dynamicOffsets)
    {
        if (!PassOpen(cpe, "wgpuComputePassEncoderSetBindGroup"))
            return;
        if (!cpe->pipeline || !cpe->pipeline->layout)
            return;

        // §14.1: index must be < maxBindGroups
        if (cpe->device && groupIndex >= cpe->device->limits.maxBindGroups)
            return;

        if (!group || group->invalid || !group->descriptor)
            return;

        auto &bgls = cpe->pipeline->layout->bindGroupLayouts;
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

        if (cpe->device && dynamicOffsetCount > 0 && dynamicOffsets)
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
                        if (offset % cpe->device->limits.minUniformBufferOffsetAlignment != 0)
                            return;
                    }
                    else
                    {
                        if (offset % cpe->device->limits.minStorageBufferOffsetAlignment != 0)
                            return;
                    }
                    dynIdx++;
                }
            }
        }

        wgpuBindGroupAddRef(group);
        cpe->retainedBindGroups.push_back(group);

        vk::PipelineLayout vkLayout(cpe->pipeline->layout->vkLayout);
        vk::DescriptorSet ds = group->descriptor->ApiHandle();

        cpe->cmd->ApiHandle().bindDescriptorSets(
            vk::PipelineBindPoint::eCompute,
            vkLayout, groupIndex,
            1, &ds,
            static_cast<uint32_t>(dynamicOffsetCount), dynamicOffsets);
    }

    // ---- §16.1.2 dispatchWorkgroups ----

    void wgpuComputePassEncoderDispatchWorkgroups(WGPUComputePassEncoder cpe,
                                                  uint32_t x, uint32_t y, uint32_t z)
    {
        if (!PassOpen(cpe, "wgpuComputePassEncoderDispatchWorkgroups"))
            return;
        if (!cpe->pipeline)
        {
            PE_WARN("[WebGPU] dispatchWorkgroups: no pipeline set");
            return;
        }

        if (cpe->device)
        {
            uint32_t limit = cpe->device->limits.maxComputeWorkgroupsPerDimension;
            if (x > limit || y > limit || z > limit)
                return;
        }

        cpe->cmd->ApiHandle().dispatch(x, y, z);
    }

    // ---- §16.1.2 dispatchWorkgroupsIndirect ----

    void wgpuComputePassEncoderDispatchWorkgroupsIndirect(WGPUComputePassEncoder cpe,
                                                          WGPUBuffer buffer, uint64_t offset)
    {
        if (!PassOpen(cpe, "wgpuComputePassEncoderDispatchWorkgroupsIndirect"))
            return;
        if (!cpe->pipeline)
        {
            PE_WARN("[WebGPU] dispatchWorkgroupsIndirect: no pipeline set");
            return;
        }
        if (!buffer || !buffer->peBuffer || buffer->destroyed)
            return;
        if (!(buffer->usage & WGPUBufferUsage_Indirect))
            return;
        if (offset + 12 > buffer->size)
            return;
        if (offset % 4 != 0)
            return;

        cpe->cmd->ApiHandle().dispatchIndirect(buffer->peBuffer->ApiHandle(), offset);
        cpe->usedBuffers.push_back(buffer);
    }

    // ---- §16.1.3 end ----

    void wgpuComputePassEncoderEnd(WGPUComputePassEncoder cpe)
    {
        if (!cpe || cpe->ended)
            return;

        if (cpe->debugGroupDepth != 0)
            PE_WARN("[WebGPU] wgpuComputePassEncoderEnd: %u debug group(s) still open", cpe->debugGroupDepth);

        if (cpe->timestampQuerySet && cpe->endTimestampIndex != UINT32_MAX &&
            cpe->timestampQuerySet->queryPool != VK_NULL_HANDLE)
        {
            cpe->cmd->ApiHandle().writeTimestamp2(
                vk::PipelineStageFlagBits2::eAllCommands,
                cpe->timestampQuerySet->queryPool, cpe->endTimestampIndex);
        }

        if (cpe->parent)
        {
            cpe->parent->retained.computePipelines.insert(
                cpe->parent->retained.computePipelines.end(),
                cpe->retainedPipelines.begin(), cpe->retainedPipelines.end());
            cpe->retainedPipelines.clear();

            cpe->parent->retained.bindGroups.insert(
                cpe->parent->retained.bindGroups.end(),
                cpe->retainedBindGroups.begin(), cpe->retainedBindGroups.end());
            cpe->retainedBindGroups.clear();

            cpe->parent->retained.usedBuffers.insert(
                cpe->parent->retained.usedBuffers.end(),
                cpe->usedBuffers.begin(), cpe->usedBuffers.end());
            cpe->usedBuffers.clear();

            if (cpe->timestampQuerySet)
            {
                cpe->parent->retained.querySets.push_back(cpe->timestampQuerySet);
                cpe->timestampQuerySet = nullptr;
            }

            cpe->parent->hasOpenPass = false;
        }

        cpe->ended = true;
    }

    // ---- §15 Debug markers ----

    void wgpuComputePassEncoderInsertDebugMarker(WGPUComputePassEncoder cpe, WGPUStringView label)
    {
        if (!PassOpen(cpe, "wgpuComputePassEncoderInsertDebugMarker"))
            return;
        cpe->cmd->InsertDebugLabel(pwgpu::ToString(label));
    }

    void wgpuComputePassEncoderPushDebugGroup(WGPUComputePassEncoder cpe, WGPUStringView groupLabel)
    {
        if (!PassOpen(cpe, "wgpuComputePassEncoderPushDebugGroup"))
            return;
        cpe->debugGroupDepth++;
        cpe->cmd->BeginDebugRegion(pwgpu::ToString(groupLabel));
    }

    void wgpuComputePassEncoderPopDebugGroup(WGPUComputePassEncoder cpe)
    {
        if (!PassOpen(cpe, "wgpuComputePassEncoderPopDebugGroup"))
            return;
        if (cpe->debugGroupDepth == 0)
        {
            PE_WARN("[WebGPU] popDebugGroup: no matching pushDebugGroup");
            return;
        }
        cpe->debugGroupDepth--;
        cpe->cmd->EndDebugRegion();
    }

    void wgpuComputePassEncoderSetLabel(WGPUComputePassEncoder cpe, WGPUStringView label)
    {
        if (cpe)
            cpe->label = pwgpu::ToString(label);
    }

} // extern "C"
