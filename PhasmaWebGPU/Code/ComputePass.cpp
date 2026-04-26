#include "ComputePass.h"
#include "CommandEncoder.h"
#include "ComputePipeline.h"
#include "PipelineLayout.h"
#include "BindGroup.h"
#include "Buffer.h"
#include "QuerySet.h"
#include "Device.h"
#include "Texture.h"
#include "Utils.h"

extern "C" void wgpuComputePipelineAddRef(WGPUComputePipeline);
extern "C" void wgpuComputePipelineRelease(WGPUComputePipeline);
extern "C" void wgpuBindGroupAddRef(WGPUBindGroup);
extern "C" void wgpuBindGroupRelease(WGPUBindGroup);
extern "C" void wgpuQuerySetAddRef(WGPUQuerySet);
extern "C" void wgpuQuerySetRelease(WGPUQuerySet);

namespace
{
    // §13.7: encoder errors fire at finish() (first wins, bubbled by pass.end).
    void ReportPassValidation(WGPUComputePassEncoder cpe, const char *msg)
    {
        if (!cpe)
            return;
        if (cpe->deferredErrorMessage.empty())
            cpe->deferredErrorMessage = msg ? msg : "";
    }

    bool PassOpen(WGPUComputePassEncoder cpe, const char *apiName)
    {
        if (!cpe)
            return false;
        if (cpe->ended)
        {
            // No future end()/finish() will surface a deferred message.
            if (cpe->device)
            {
                std::string msg = std::string(apiName) + ": compute pass encoder is already ended";
                cpe->device->reportError(WGPUErrorType_Validation,
                                         pwgpu::ToStringView(msg.c_str()));
            }
            return false;
        }
        if (cpe->parent && cpe->parent->finished)
        {
            if (cpe->device)
            {
                std::string msg = std::string(apiName) + ": parent command encoder is already finished";
                cpe->device->reportError(WGPUErrorType_Validation,
                                         pwgpu::ToStringView(msg.c_str()));
            }
            return false;
        }
        if (cpe->invalid)
            return false;
        return true;
    }

    vk::AccessFlags2 BufferAccessForUsage(pwgpu::BufferUsageKind kind)
    {
        switch (kind)
        {
        case pwgpu::BufferUsageKind::Constant:
            return vk::AccessFlagBits2::eUniformRead;
        case pwgpu::BufferUsageKind::StorageRead:
            return vk::AccessFlagBits2::eShaderStorageRead;
        case pwgpu::BufferUsageKind::Storage:
            return vk::AccessFlagBits2::eShaderStorageRead |
                   vk::AccessFlagBits2::eShaderStorageWrite;
        case pwgpu::BufferUsageKind::Input:
            return vk::AccessFlagBits2::eIndirectCommandRead;
        default:
            return vk::AccessFlagBits2::eNone;
        }
    }

    vk::PipelineStageFlags2 BufferStageForUsage(pwgpu::BufferUsageKind kind)
    {
        if (kind == pwgpu::BufferUsageKind::Input)
            return vk::PipelineStageFlagBits2::eDrawIndirect;
        return vk::PipelineStageFlagBits2::eComputeShader;
    }

    vk::AccessFlags2 TextureAccessForUsage(pwgpu::SubresourceUsageKind kind)
    {
        switch (kind)
        {
        case pwgpu::SubresourceUsageKind::Sampled:
            return vk::AccessFlagBits2::eShaderSampledRead;
        case pwgpu::SubresourceUsageKind::ReadOnlyStorage:
            return vk::AccessFlagBits2::eShaderStorageRead;
        case pwgpu::SubresourceUsageKind::WriteOnlyStorage:
            return vk::AccessFlagBits2::eShaderStorageWrite;
        case pwgpu::SubresourceUsageKind::ReadWriteStorage:
            return vk::AccessFlagBits2::eShaderStorageRead |
                   vk::AccessFlagBits2::eShaderStorageWrite;
        default:
            return vk::AccessFlagBits2::eNone;
        }
    }

    vk::ImageLayout TextureLayoutForUsage(pwgpu::SubresourceUsageKind kind)
    {
        return kind == pwgpu::SubresourceUsageKind::Sampled
                   ? vk::ImageLayout::eShaderReadOnlyOptimal
                   : vk::ImageLayout::eGeneral;
    }

    void EmitDispatchResourceBarriers(WGPUComputePassEncoder cpe, WGPUBuffer indirectBuffer = nullptr)
    {
        if (!cpe || !cpe->pipeline || !cpe->pipeline->layout)
            return;

        std::vector<pe::ImageBarrierInfo> imageBarriers;
        std::vector<pe::BufferBarrierInfo> bufferBarriers;

        auto appendBindGroupBarriers = [&](WGPUBindGroupImpl *bg)
        {
            if (!bg || bg->invalid)
                return;

            for (const auto &use : bg->textureUses)
            {
                if (!use.view || !use.view->texture || !use.view->texture->image)
                    continue;

                pe::ImageBarrierInfo barrier{};
                barrier.image = use.view->texture->image;
                barrier.stageFlags = vk::PipelineStageFlagBits2::eComputeShader;
                barrier.accessMask = TextureAccessForUsage(use.kind);
                barrier.layout = TextureLayoutForUsage(use.kind);
                barrier.baseMipLevel = use.view->baseMipLevel;
                barrier.mipLevels = use.view->mipLevelCount;
                barrier.baseArrayLayer = use.view->baseArrayLayer;
                barrier.arrayLayers = use.view->arrayLayerCount;
                imageBarriers.push_back(barrier);
            }

            for (const auto &use : bg->bufferUses)
            {
                if (!use.buffer || !use.buffer->peBuffer)
                    continue;

                pe::BufferBarrierInfo barrier{};
                barrier.buffer = use.buffer->peBuffer;
                barrier.stageMask = BufferStageForUsage(use.kind);
                barrier.accessMask = BufferAccessForUsage(use.kind);
                bufferBarriers.push_back(barrier);
            }
        };

        auto &bgls = cpe->pipeline->layout->bindGroupLayouts;
        for (size_t i = 0; i < cpe->currentBindGroups.size() && i < bgls.size(); ++i)
        {
            if (!bgls[i])
                continue;
            appendBindGroupBarriers(cpe->currentBindGroups[i]);
        }

        if (indirectBuffer && indirectBuffer->peBuffer)
        {
            pe::BufferBarrierInfo barrier{};
            barrier.buffer = indirectBuffer->peBuffer;
            barrier.stageMask = BufferStageForUsage(pwgpu::BufferUsageKind::Input);
            barrier.accessMask = BufferAccessForUsage(pwgpu::BufferUsageKind::Input);
            bufferBarriers.push_back(barrier);
        }

        if (!bufferBarriers.empty())
            cpe->cmd->BufferBarriers(bufferBarriers);
        if (!imageBarriers.empty())
            cpe->cmd->ImageBarriers(imageBarriers);
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
            char buf[160];
            std::snprintf(buf, sizeof(buf),
                          "wgpuComputePassEncoderSetBindGroup: index (%u) must be < "
                          "maxBindGroups (%u)",
                          groupIndex, cpe->device->limits.maxBindGroups);
            ReportPassValidation(cpe, buf);
            cpe->invalid = true;
            return;
        }

        if (group)
        {
            if (group->device != cpe->device)
            {
                ReportPassValidation(cpe,
                                     "wgpuComputePassEncoderSetBindGroup: bindGroup is not valid "
                                     "to use with this encoder (cross-device)");
                cpe->invalid = true;
                return;
            }
            if (group->invalid)
            {
                if (group->invalidFromDestroyedResource)
                {
                    // §3.3: defer to queue.submit, don't fire at finish.
                    cpe->deferredResourceError = true;
                }
                else
                {
                    ReportPassValidation(
                        cpe,
                        "wgpuComputePassEncoderSetBindGroup: bindGroup is not valid "
                        "to use with this encoder (invalid bindGroup)");
                    cpe->invalid = true;
                }
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
                char buf[160];
                std::snprintf(buf, sizeof(buf),
                              "wgpuComputePassEncoderSetBindGroup: dynamicOffsetCount (%zu) does not match "
                              "bind group layout dynamicOffsetCount (%u)",
                              dynamicOffsetCount, group->layout->dynamicOffsetCount);
                ReportPassValidation(cpe, buf);
                cpe->invalid = true;
                return;
            }
            if (dynamicOffsetCount > 0 && !dynamicOffsets)
            {
                ReportPassValidation(cpe,
                                     "wgpuComputePassEncoderSetBindGroup: dynamicOffsets is null but "
                                     "dynamicOffsetCount > 0");
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
                        char buf[192];
                        std::snprintf(buf, sizeof(buf),
                                      "wgpuComputePassEncoderSetBindGroup: dynamicOffsets[%u]=%u is not a "
                                      "multiple of %s (%u) for binding %u",
                                      i, offset,
                                      isUniform ? "minUniformBufferOffsetAlignment"
                                                : "minStorageBufferOffsetAlignment",
                                      align, dynLayoutEntries[i]->binding);
                        ReportPassValidation(cpe, buf);
                        cpe->invalid = true;
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
                                          "wgpuComputePassEncoderSetBindGroup: dynamicOffsets[%u]=%u "
                                          "exceeds buffer bounds for binding %u "
                                          "(baseOffset=%llu, bindingSize=%llu, bufferSize=%llu)",
                                          i, offset, dyn.binding,
                                          (unsigned long long)dyn.baseOffset,
                                          (unsigned long long)dyn.bindingSize,
                                          (unsigned long long)bufSize);
                            ReportPassValidation(cpe, buf);
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
            // §10.2.7: empty default/explicit BGLs are treated as "null" and
            // ignored when checking setBindGroup() compatibility.
            if (plBgl->entries.empty())
                continue;
            if (i >= cpe->currentBindGroups.size())
            {
                char buf[160];
                std::snprintf(buf, sizeof(buf),
                              "compute pass: bind group at index %zu required by pipeline "
                              "layout is not set",
                              i);
                ReportPassValidation(cpe, buf);
                cpe->invalid = true;
                return false;
            }
            auto *bg = cpe->currentBindGroups[i];
            if (!bg || bg->invalid || !bg->layout)
            {
                char buf[160];
                std::snprintf(buf, sizeof(buf),
                              "compute pass: bind group at index %zu required by pipeline "
                              "layout is null or invalid",
                              i);
                ReportPassValidation(cpe, buf);
                cpe->invalid = true;
                return false;
            }
            if (!BglGroupEquivalent(bg->layout, plBgl))
            {
                char buf[160];
                std::snprintf(buf, sizeof(buf),
                              "compute pass: bind group at index %zu is not group-equivalent "
                              "with the pipeline layout",
                              i);
                ReportPassValidation(cpe, buf);
                cpe->invalid = true;
                return false;
            }
        }
        return true;
    }

    static void ValidateDispatchUsageScope(WGPUComputePassEncoder cpe, WGPUBuffer indirectBuffer = nullptr)
    {
        if (!cpe->pipeline || !cpe->pipeline->layout)
            return;
        auto &bgls = cpe->pipeline->layout->bindGroupLayouts;
        const size_t pipelineGroupCount = bgls.size();
        pwgpu::UsageScope scope;
        scope.strictWritableDuplicates = true;
        for (size_t i = 0; i < cpe->currentBindGroups.size() && i < pipelineGroupCount; ++i)
        {
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

        constexpr uint64_t kU32Max = 0xFFFFFFFFull;
        const uint64_t countProduct = (uint64_t)x * (uint64_t)y * (uint64_t)z;
        if (countProduct > kU32Max)
        {
            cpe->invalid = true;
            ReportPassValidation(
                cpe,
                "dispatchWorkgroups: workgroupCountX*Y*Z exceeds 2^32-1 (workgroup_index overflow)");
            return;
        }
        const uint64_t invocations = cpe->pipeline->workgroupInvocations ? cpe->pipeline->workgroupInvocations : 1;
        if (invocations != 0 && countProduct > kU32Max / invocations)
        {
            cpe->invalid = true;
            ReportPassValidation(
                cpe,
                "dispatchWorkgroups: workgroupCount*workgroupSize exceeds 2^32-1 (global_invocation_index overflow)");
            return;
        }

        if (!ValidateBindGroupCompat(cpe))
            return;

        ValidateDispatchUsageScope(cpe);
        EmitDispatchResourceBarriers(cpe);

        cpe->cmd->ApiHandle().dispatch(x, y, z);
    }

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
        if (!buffer)
        {
            ReportPassValidation(
                cpe, "wgpuComputePassEncoderDispatchWorkgroupsIndirect: indirectBuffer is null");
            cpe->invalid = true;
            return;
        }
        if (buffer->device != cpe->device)
        {
            ReportPassValidation(cpe,
                                 "wgpuComputePassEncoderDispatchWorkgroupsIndirect: indirectBuffer "
                                 "was created by a different device than the compute pass encoder");
            cpe->invalid = true;
            return;
        }
        if (buffer->invalid)
        {
            ReportPassValidation(
                cpe,
                "wgpuComputePassEncoderDispatchWorkgroupsIndirect: indirectBuffer is invalid");
            cpe->invalid = true;
            return;
        }
        if (!(buffer->usage & WGPUBufferUsage_Indirect))
        {
            ReportPassValidation(cpe,
                                 "wgpuComputePassEncoderDispatchWorkgroupsIndirect: indirectBuffer "
                                 "usage does not contain INDIRECT");
            cpe->invalid = true;
            return;
        }
        if (offset % 4 != 0)
        {
            ReportPassValidation(cpe,
                                 "wgpuComputePassEncoderDispatchWorkgroupsIndirect: indirectOffset "
                                 "is not a multiple of 4");
            cpe->invalid = true;
            return;
        }
        if (offset > buffer->size || buffer->size - offset < 12)
        {
            ReportPassValidation(cpe,
                                 "wgpuComputePassEncoderDispatchWorkgroupsIndirect: indirectOffset "
                                 "+ 12 exceeds indirectBuffer.size");
            cpe->invalid = true;
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
        EmitDispatchResourceBarriers(cpe, buffer);

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
            pwgpu::FireSyncValidation(cpe->device, "ComputePassEncoder.end(): pass is already ended");
            return;
        }
        if (cpe->parent && cpe->parent->finished)
        {
            pwgpu::FireSyncValidation(cpe->device,
                                      "ComputePassEncoder.end(): parent command encoder is already finished");
            cpe->ended = true;
            return;
        }
        if (!cpe->wasOpened)
        {
            pwgpu::FireSyncValidation(cpe->device,
                                      "ComputePassEncoder.end(): pass was never opened (invalid begin)");
            if (cpe->parent)
                cpe->parent->invalid = true;
            cpe->ended = true;
            return;
        }

        if ((cpe->invalid || !cpe->usageScopeValid || cpe->debugGroupDepth != 0) && cpe->parent)
        {
            cpe->parent->invalid = true;
            if (cpe->parent->deferredErrorMessage.empty() && !cpe->deferredErrorMessage.empty())
                cpe->parent->deferredErrorMessage = cpe->deferredErrorMessage;
        }
        // Submit-time validity propagates regardless of finish-time invalid.
        if (cpe->deferredResourceError && cpe->parent)
            cpe->parent->deferredResourceError = true;

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
