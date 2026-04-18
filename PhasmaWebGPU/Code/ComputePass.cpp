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
    void ReportPassValidation(WGPUComputePassEncoder cpe, const char *msg)
    {
        if (cpe && cpe->device)
            cpe->device->reportError(WGPUErrorType_Validation, pwgpu::ToStringView(msg));
    }

    bool PassOpen(WGPUComputePassEncoder cpe, const char *apiName)
    {
        if (!cpe)
            return false;
        if (cpe->ended)
        {
            std::string msg = std::string(apiName) + ": compute pass encoder is already ended";
            ReportPassValidation(cpe, msg.c_str());
            return false;
        }
        if (cpe->parent && cpe->parent->finished)
        {
            std::string msg = std::string(apiName) + ": parent command encoder is already finished";
            ReportPassValidation(cpe, msg.c_str());
            return false;
        }
        if (cpe->invalid)
            return false;
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
        if (!pipeline)
        {
            cpe->invalid = true;
            return;
        }
        if (pipeline->device != cpe->device)
        {
            cpe->invalid = true;
            return;
        }
        if (pipeline->invalid || pipeline->vkPipeline == VK_NULL_HANDLE)
        {
            cpe->invalid = true;
            return;
        }

        cpe->pipeline = pipeline;

        wgpuComputePipelineAddRef(pipeline);
        cpe->retainedPipelines.push_back(pipeline);

        cpe->cmd->ApiHandle().bindPipeline(vk::PipelineBindPoint::eCompute, pipeline->vkPipeline);

        // Replay any bind groups already recorded via setBindGroup before
        // setPipeline. setBindGroup is a no-op for the actual Vulkan bind
        // until a pipeline is known (needed for vkCmdBindDescriptorSets's
        // layout argument). Without this replay, dispatches hit
        // VUID-vkCmdDispatch-None-08600 (descriptor set 0 not bound).
        if (pipeline->layout)
        {
            auto &bgls = pipeline->layout->bindGroupLayouts;
            vk::PipelineLayout vkLayout(pipeline->layout->vkLayout);
            for (size_t i = 0; i < cpe->currentBindGroups.size() && i < bgls.size(); ++i)
            {
                auto *bg = cpe->currentBindGroups[i];
                if (!bg || !bg->descriptor || !bgls[i])
                    continue;
                if (!BglGroupEquivalent(bg->layout, bgls[i]))
                    continue;
                vk::DescriptorSet ds = bg->descriptor->ApiHandle();
                cpe->cmd->ApiHandle().bindDescriptorSets(
                    vk::PipelineBindPoint::eCompute, vkLayout,
                    static_cast<uint32_t>(i), 1, &ds, 0, nullptr);
            }
        }
    }

    // ---- §14.1 setBindGroup ----

    void wgpuComputePassEncoderSetBindGroup(WGPUComputePassEncoder cpe, uint32_t groupIndex,
                                            WGPUBindGroup group,
                                            size_t dynamicOffsetCount, uint32_t const *dynamicOffsets)
    {
        if (!PassOpen(cpe, "wgpuComputePassEncoderSetBindGroup"))
            return;

        if (cpe->device && groupIndex >= cpe->device->limits.maxBindGroups)
        {
            cpe->invalid = true;
            return;
        }

        if (group)
        {
            if (group->device != cpe->device)
            {
                cpe->invalid = true;
                return;
            }
            if (group->invalid)
            {
                cpe->invalid = true;
                return;
            }
        }

        if (cpe->device && groupIndex < cpe->device->limits.maxBindGroups)
        {
            if (cpe->currentBindGroups.size() <= groupIndex)
                cpe->currentBindGroups.resize(groupIndex + 1, nullptr);
            cpe->currentBindGroups[groupIndex] = group;
        }

        if (group)
        {
            wgpuBindGroupAddRef(group);
            cpe->retainedBindGroups.push_back(group);
        }

        if (group && group->layout)
        {
            if (static_cast<uint32_t>(dynamicOffsetCount) != group->layout->dynamicOffsetCount)
            {
                cpe->invalid = true;
                return;
            }
            if (dynamicOffsetCount > 0 && !dynamicOffsets)
            {
                cpe->invalid = true;
                return;
            }
            if (cpe->device && dynamicOffsetCount > 0 && dynamicOffsets)
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
                    uint32_t align = isUniform ? cpe->device->limits.minUniformBufferOffsetAlignment
                                               : cpe->device->limits.minStorageBufferOffsetAlignment;
                    if (align > 0 && offset % align != 0)
                    {
                        cpe->invalid = true;
                        return;
                    }
                    if (dyn.buffer)
                    {
                        uint64_t bufSize = dyn.buffer->size;
                        uint64_t effOffset = dyn.baseOffset + static_cast<uint64_t>(offset);
                        if (effOffset > bufSize || dyn.bindingSize > bufSize - effOffset)
                        {
                            cpe->invalid = true;
                            return;
                        }
                    }
                }
            }
        }

        if (!cpe->pipeline || !cpe->pipeline->layout)
            return;

        if (!group || !group->descriptor)
            return;

        auto &bgls = cpe->pipeline->layout->bindGroupLayouts;
        if (groupIndex >= bgls.size() || !bgls[groupIndex])
            return;
        if (!BglGroupEquivalent(group->layout, bgls[groupIndex]))
        {
            PE_WARN("[WebGPU] setBindGroup: bind group layout does not match pipeline layout at index %u", groupIndex);
            return;
        }

        vk::PipelineLayout vkLayout(cpe->pipeline->layout->vkLayout);
        vk::DescriptorSet ds = group->descriptor->ApiHandle();

        cpe->cmd->ApiHandle().bindDescriptorSets(
            vk::PipelineBindPoint::eCompute,
            vkLayout, groupIndex,
            1, &ds,
            static_cast<uint32_t>(dynamicOffsetCount), dynamicOffsets);
    }

    // ---- §16.1.2 dispatchWorkgroups ----

    static bool ValidateBindGroupCompat(WGPUComputePassEncoder cpe)
    {
        if (!cpe || !cpe->pipeline || !cpe->pipeline->layout)
            return true;
        auto &bgls = cpe->pipeline->layout->bindGroupLayouts;
        for (size_t i = 0; i < bgls.size(); ++i)
        {
            auto *plBgl = bgls[i];
            if (!plBgl)
                continue;
            if (i >= cpe->currentBindGroups.size())
            {
                cpe->invalid = true;
                return false;
            }
            auto *bg = cpe->currentBindGroups[i];
            if (!bg || bg->invalid || !bg->layout)
            {
                cpe->invalid = true;
                return false;
            }
            if (!BglGroupEquivalent(bg->layout, plBgl))
            {
                cpe->invalid = true;
                return false;
            }
        }
        return true;
    }

    static void ValidateDispatchUsageScope(WGPUComputePassEncoder cpe,
                                           WGPUBuffer indirectBuffer = nullptr)
    {
        if (!cpe->pipeline || !cpe->pipeline->layout)
            return;
        auto &bgls = cpe->pipeline->layout->bindGroupLayouts;
        const size_t pipelineGroupCount = bgls.size();
        pwgpu::UsageScope scope;
        scope.strictWritableDuplicates = true;
        for (size_t i = 0; i < cpe->currentBindGroups.size() && i < pipelineGroupCount; ++i)
        {
            // §16.1.2: only bind groups in slots used by the pipeline layout contribute.
            if (!bgls[i])
                continue;
            auto *bg = cpe->currentBindGroups[i];
            if (!bg || bg->invalid)
                continue;
            for (auto &use : bg->textureUses)
            {
                std::string err;
                if (!scope.AddView(use.view, use.kind, err))
                {
                    cpe->usageScopeValid = false;
                    return;
                }
            }
            for (auto &use : bg->bufferUses)
            {
                std::string err;
                if (!scope.AddBuffer(use.buffer, use.kind, err))
                {
                    cpe->usageScopeValid = false;
                    return;
                }
            }
        }
        if (indirectBuffer)
        {
            std::string err;
            if (!scope.AddBuffer(indirectBuffer, pwgpu::BufferUsageKind::Input, err))
            {
                cpe->usageScopeValid = false;
                return;
            }
        }
    }

    void wgpuComputePassEncoderDispatchWorkgroups(WGPUComputePassEncoder cpe,
                                                  uint32_t x, uint32_t y, uint32_t z)
    {
        if (!PassOpen(cpe, "wgpuComputePassEncoderDispatchWorkgroups"))
            return;
        if (!cpe->pipeline)
        {
            cpe->invalid = true;
            PE_WARN("[WebGPU] dispatchWorkgroups: no pipeline set");
            return;
        }

        if (cpe->device)
        {
            uint32_t limit = cpe->device->limits.maxComputeWorkgroupsPerDimension;
            if (x > limit || y > limit || z > limit)
            {
                cpe->invalid = true;
                return;
            }
        }

        if (!ValidateBindGroupCompat(cpe))
            return;

        ValidateDispatchUsageScope(cpe);

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
            cpe->invalid = true;
            PE_WARN("[WebGPU] dispatchWorkgroupsIndirect: no pipeline set");
            return;
        }
        auto markInvalid = [&]()
        { cpe->invalid = true; };
        if (!buffer)
        {
            markInvalid();
            return;
        }
        if (buffer->device != cpe->device)
        {
            markInvalid();
            return;
        }
        if (buffer->invalid)
        {
            markInvalid();
            return;
        }
        if (!(buffer->usage & WGPUBufferUsage_Indirect))
        {
            markInvalid();
            return;
        }
        if (offset % 4 != 0)
        {
            markInvalid();
            return;
        }
        if (offset > buffer->size || buffer->size - offset < 12)
        {
            markInvalid();
            return;
        }
        if (buffer->internalState == BufferInternalState::Destroyed || !buffer->peBuffer)
        {
            cpe->usedBuffers.push_back(buffer);
            return;
        }

        if (!ValidateBindGroupCompat(cpe))
            return;

        ValidateDispatchUsageScope(cpe, buffer);

        cpe->cmd->ApiHandle().dispatchIndirect(buffer->peBuffer->ApiHandle(), offset);
        cpe->usedBuffers.push_back(buffer);
    }

    // ---- §16.1.3 end ----

    void wgpuComputePassEncoderEnd(WGPUComputePassEncoder cpe)
    {
        if (!cpe)
            return;
        if (cpe->ended)
        {
            ReportPassValidation(cpe, "ComputePassEncoder.end(): pass is already ended");
            return;
        }
        if (cpe->parent && cpe->parent->finished)
        {
            ReportPassValidation(cpe, "ComputePassEncoder.end(): parent command encoder is already finished");
            cpe->ended = true;
            return;
        }
        if (!cpe->wasOpened)
        {
            ReportPassValidation(cpe, "ComputePassEncoder.end(): pass was never opened (invalid begin)");
            if (cpe->parent)
                cpe->parent->invalid = true;
            cpe->ended = true;
            return;
        }

        if ((cpe->invalid || !cpe->usageScopeValid || cpe->debugGroupDepth != 0) && cpe->parent)
            cpe->parent->invalid = true;

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
            cpe->invalid = true;
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
