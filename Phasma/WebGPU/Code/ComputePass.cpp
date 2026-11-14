#include "ComputePass.h"
#include "CommandEncoder.h"
#include "ComputePipeline.h"
#include "PipelineBinding.h"
#include "PipelineLayout.h"
#include "BindGroup.h"
#include "Buffer.h"
#include "QuerySet.h"
#include "QueryCommands.h"
#include "Device.h"
#include "Texture.h"
#include "Utils.h"
#include "API/RHI.h"
#include "API/Vulkan/RHI_Vulkan.h"
#include "API/Vulkan/VulkanCommandBufferImpl.h"

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

    PeBarrierAccess BufferAccessForUsage(pwgpu::BufferUsageKind kind)
    {
        switch (kind)
        {
        case pwgpu::BufferUsageKind::Constant:
            return PE_ACCESS_UNIFORM_READ;
        case pwgpu::BufferUsageKind::StorageRead:
            return PE_ACCESS_SHADER_STORAGE_READ;
        case pwgpu::BufferUsageKind::Storage:
            return PE_ACCESS_SHADER_STORAGE_READ | PE_ACCESS_SHADER_STORAGE_WRITE;
        case pwgpu::BufferUsageKind::Input:
            return PE_ACCESS_INDIRECT_COMMAND_READ;
        default:
            return PE_ACCESS_NONE;
        }
    }

    PeBarrierSync BufferStageForUsage(pwgpu::BufferUsageKind kind)
    {
        if (kind == pwgpu::BufferUsageKind::Input)
            return PE_STAGE_DRAW_INDIRECT;
        return PE_STAGE_COMPUTE_SHADER;
    }

    PeBarrierAccess TextureAccessForUsage(pwgpu::SubresourceUsageKind kind)
    {
        switch (kind)
        {
        case pwgpu::SubresourceUsageKind::Sampled:
            return PE_ACCESS_SHADER_SAMPLED_READ;
        case pwgpu::SubresourceUsageKind::ReadOnlyStorage:
            return PE_ACCESS_SHADER_STORAGE_READ;
        case pwgpu::SubresourceUsageKind::WriteOnlyStorage:
            return PE_ACCESS_SHADER_STORAGE_WRITE;
        case pwgpu::SubresourceUsageKind::ReadWriteStorage:
            return PE_ACCESS_SHADER_STORAGE_READ | PE_ACCESS_SHADER_STORAGE_WRITE;
        default:
            return PE_ACCESS_NONE;
        }
    }

    PeImageLayout TextureLayoutForUsage(pwgpu::SubresourceUsageKind kind)
    {
        return kind == pwgpu::SubresourceUsageKind::Sampled
                   ? PE_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                   : PE_IMAGE_LAYOUT_GENERAL;
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

            LazyInitBindGroupTextures(cpe->cmd, bg);

            for (const auto &use : bg->textureUses)
            {
                if (!use.view || !use.view->texture || !use.view->texture->image)
                    continue;

                pe::ImageBarrierInfo barrier{};
                barrier.image = use.view->texture->image;
                barrier.stageFlags = PE_STAGE_COMPUTE_SHADER;
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

    // dispatchWorkgroupsIndirect, queue timeline: a workgroup count above maxComputeWorkgroupsPerDimension makes the
    // dispatch do nothing. Only the GPU knows the counts, so this one-thread shader copies them into scratch indirect
    // arguments, or zeros when any is over the limit, and the dispatch reads those. Built offline from:
    //
    //     struct Params { uint64_t source; uint64_t destination; uint limit; };
    //     [[vk::push_constant]] Params params;
    //
    //     [numthreads(1, 1, 1)]
    //     void main()
    //     {
    //         const uint3 counts = vk::RawBufferLoad<uint3>(params.source, 4);
    //         const bool valid = all(counts <= params.limit);
    //         vk::RawBufferStore<uint3>(params.destination, valid ? counts : uint3(0, 0, 0), 4);
    //     }
    //
    // with dxc -T cs_6_0 -E main -spirv -fspv-target-env=vulkan1.2 -O3 (DXC 1.9.0.5347, Vulkan SDK 1.4.350.0).
    // clang-format off
    constexpr uint32_t kIndirectValidationSpirv[] = {
        0x07230203, 0x00010500, 0x000e0000, 0x00000024, 0x00000000, 0x00020011, 0x00000001, 0x00020011,
        0x0000000b, 0x00020011, 0x000014e3, 0x0003000e, 0x000014e4, 0x00000001, 0x0006000f, 0x00000005,
        0x00000001, 0x6e69616d, 0x00000000, 0x00000002, 0x00060010, 0x00000001, 0x00000011, 0x00000001,
        0x00000001, 0x00000001, 0x00030003, 0x00000005, 0x00000258, 0x00090005, 0x00000003, 0x65707974,
        0x7375502e, 0x6e6f4368, 0x6e617473, 0x61502e74, 0x736d6172, 0x00000000, 0x00050006, 0x00000003,
        0x00000000, 0x72756f73, 0x00006563, 0x00060006, 0x00000003, 0x00000001, 0x74736564, 0x74616e69,
        0x006e6f69, 0x00050006, 0x00000003, 0x00000002, 0x696d696c, 0x00000074, 0x00040005, 0x00000002,
        0x61726170, 0x0000736d, 0x00040005, 0x00000001, 0x6e69616d, 0x00000000, 0x00050048, 0x00000003,
        0x00000000, 0x00000023, 0x00000000, 0x00050048, 0x00000003, 0x00000001, 0x00000023, 0x00000008,
        0x00050048, 0x00000003, 0x00000002, 0x00000023, 0x00000010, 0x00030047, 0x00000003, 0x00000002,
        0x00040015, 0x00000004, 0x00000020, 0x00000001, 0x0004002b, 0x00000004, 0x00000005, 0x00000000,
        0x0004002b, 0x00000004, 0x00000006, 0x00000002, 0x0004002b, 0x00000004, 0x00000007, 0x00000001,
        0x00040015, 0x00000008, 0x00000020, 0x00000000, 0x0004002b, 0x00000008, 0x00000009, 0x00000000,
        0x00040017, 0x0000000a, 0x00000008, 0x00000003, 0x0006002c, 0x0000000a, 0x0000000b, 0x00000009,
        0x00000009, 0x00000009, 0x00040015, 0x0000000c, 0x00000040, 0x00000000, 0x0005001e, 0x00000003,
        0x0000000c, 0x0000000c, 0x00000008, 0x00040020, 0x0000000d, 0x00000009, 0x00000003, 0x00020013,
        0x0000000e, 0x00030021, 0x0000000f, 0x0000000e, 0x00020014, 0x00000010, 0x00040020, 0x00000011,
        0x00000009, 0x0000000c, 0x00040020, 0x00000012, 0x000014e5, 0x0000000a, 0x00040020, 0x00000013,
        0x00000009, 0x00000008, 0x00040017, 0x00000014, 0x00000010, 0x00000003, 0x0004003b, 0x0000000d,
        0x00000002, 0x00000009, 0x00050036, 0x0000000e, 0x00000001, 0x00000000, 0x0000000f, 0x000200f8,
        0x00000015, 0x00050041, 0x00000011, 0x00000016, 0x00000002, 0x00000005, 0x0004003d, 0x0000000c,
        0x00000017, 0x00000016, 0x0004007c, 0x00000012, 0x00000018, 0x00000017, 0x0006003d, 0x0000000a,
        0x00000019, 0x00000018, 0x00000002, 0x00000004, 0x00050041, 0x00000013, 0x0000001a, 0x00000002,
        0x00000006, 0x0004003d, 0x00000008, 0x0000001b, 0x0000001a, 0x00060050, 0x0000000a, 0x0000001c,
        0x0000001b, 0x0000001b, 0x0000001b, 0x000500b2, 0x00000014, 0x0000001d, 0x00000019, 0x0000001c,
        0x0004009b, 0x00000010, 0x0000001e, 0x0000001d, 0x00050041, 0x00000011, 0x0000001f, 0x00000002,
        0x00000007, 0x0004003d, 0x0000000c, 0x00000020, 0x0000001f, 0x00060050, 0x00000014, 0x00000021,
        0x0000001e, 0x0000001e, 0x0000001e, 0x000600a9, 0x0000000a, 0x00000022, 0x00000021, 0x00000019,
        0x0000000b, 0x0004007c, 0x00000012, 0x00000023, 0x00000020, 0x0005003e, 0x00000023, 0x00000022,
        0x00000002, 0x00000004, 0x000100fd, 0x00010038,
    };
    // clang-format on

    struct IndirectValidationParams
    {
        uint64_t source = 0;
        uint64_t destination = 0;
        uint32_t limit = 0;
        uint32_t padding = 0;
    };

    // The validation pipeline, created once per device at its first indirect dispatch.
    bool EnsureIndirectValidationPipeline(WGPUDeviceImpl *device)
    {
        std::lock_guard<std::mutex> lock(device->indirectValidationMutex);
        if (device->indirectValidationPipeline)
            return true;

        vk::Device vkDevice = pe::VulkanRhi::Device();
        vk::PipelineLayout layout{};
        vk::ShaderModule module{};
        try
        {
            const vk::PushConstantRange range{vk::ShaderStageFlagBits::eCompute, 0, sizeof(IndirectValidationParams)};
            vk::PipelineLayoutCreateInfo layoutInfo{};
            layoutInfo.pushConstantRangeCount = 1;
            layoutInfo.pPushConstantRanges = &range;
            layout = vkDevice.createPipelineLayout(layoutInfo);

            vk::ShaderModuleCreateInfo moduleInfo{};
            moduleInfo.codeSize = sizeof(kIndirectValidationSpirv);
            moduleInfo.pCode = kIndirectValidationSpirv;
            module = vkDevice.createShaderModule(moduleInfo);

            vk::ComputePipelineCreateInfo pipelineInfo{};
            pipelineInfo.stage.stage = vk::ShaderStageFlagBits::eCompute;
            pipelineInfo.stage.module = module;
            pipelineInfo.stage.pName = "main";
            pipelineInfo.layout = layout;
            const vk::Pipeline pipeline = vkDevice.createComputePipeline(nullptr, pipelineInfo).value;

            vkDevice.destroyShaderModule(module);
            device->indirectValidationLayout = PeToBackendHandle(static_cast<VkPipelineLayout>(layout));
            device->indirectValidationPipeline = PeToBackendHandle(static_cast<VkPipeline>(pipeline));
            return true;
        }
        catch (...)
        {
            if (module)
                vkDevice.destroyShaderModule(module);

            if (layout)
                vkDevice.destroyPipelineLayout(layout);

            return false;
        }
    }

    // Records the validation shader for one indirect dispatch and returns its scratch arguments, which the command
    // encoder owns until the GPU is done with them. Null when the counts cannot be validated here.
    pe::Buffer *ValidatedIndirectArguments(WGPUComputePassEncoder cpe, WGPUBufferImpl *source, uint64_t offset)
    {
        WGPUDeviceImpl *device = cpe->device;
        const bool addressable = (source->peBuffer->Usage() & PE_BUFFER_USAGE_SHADER_DEVICE_ADDRESS) != 0;
        if (!addressable || !cpe->parent || !EnsureIndirectValidationPipeline(device))
            return nullptr;

        pe::Buffer *arguments = nullptr;
        try
        {
            arguments = pe::Buffer::Create({
                .size = 3 * sizeof(uint32_t),
                .usage = PE_BUFFER_USAGE_INDIRECT_BUFFER | PE_BUFFER_USAGE_SHADER_DEVICE_ADDRESS,
                .memoryUsage = PE_MEMORY_USAGE_GPU_ONLY,
                .name = "wgpu_indirect_dispatch_arguments",
            });
        }
        catch (...)
        {
            return nullptr;
        }

        cpe->parent->retained.nativeBuffers.push_back(arguments);

        IndirectValidationParams params{};
        params.source = source->peBuffer->GetDeviceAddress() + offset;
        params.destination = arguments->GetDeviceAddress();
        params.limit = device->limits.maxComputeWorkgroupsPerDimension;

        const vk::PipelineLayout layout{PeFromBackendHandle<VkPipelineLayout>(device->indirectValidationLayout)};
        const vk::Pipeline pipeline{PeFromBackendHandle<VkPipeline>(device->indirectValidationPipeline)};
        vk::CommandBuffer cmd = pe::GetVulkanCommandBuffer(cpe->cmd);

        // The application's last write of the counts (a copy, a dispatch, a queue write) lands before the shader reads.
        const vk::MemoryBarrier toShader{vk::AccessFlagBits::eMemoryWrite, vk::AccessFlagBits::eShaderRead};
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eAllCommands,
                            vk::PipelineStageFlagBits::eComputeShader,
                            {},
                            toShader,
                            {},
                            {});

        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, pipeline);
        cmd.pushConstants(layout, vk::ShaderStageFlagBits::eCompute, 0, sizeof(params), &params);
        cmd.dispatch(1, 1, 1);

        const vk::MemoryBarrier toIndirect{vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eIndirectCommandRead};
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                            vk::PipelineStageFlagBits::eDrawIndirect,
                            {},
                            toIndirect,
                            {},
                            {});

        // Back to the application's pipeline. Its bind groups stay bound: the validation layout has no sets.
        cpe->bindingCache.computePipeline = nullptr;
        pwgpu::BindWebGPUComputePipeline(cpe->cmd, cpe->pipeline, &cpe->bindingCache);
        return arguments;
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
            for (auto *buf : cpe->usedBuffers)
                wgpuBufferRelease(buf);
            if (cpe->timestampQuerySet)
                wgpuQuerySetRelease(cpe->timestampQuerySet);
            if (cpe->parent)
                wgpuCommandEncoderRelease(cpe->parent);
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
        if (pipeline->invalid || pipeline->backendPipeline == 0)
        {
            cpe->invalid = true;
            return;
        }

        cpe->pipeline = pipeline;

        wgpuComputePipelineAddRef(pipeline);
        cpe->retainedPipelines.push_back(pipeline);

        if (!pwgpu::BindWebGPUComputePipeline(cpe->cmd, pipeline, &cpe->bindingCache))
        {
            cpe->invalid = true;
            return;
        }

        if (pipeline->layout)
            pwgpu::RebindWebGPUCompatibleBindGroups(
                cpe->cmd, pwgpu::PipelineBindingPoint::Compute,
                pipeline->layout, cpe->currentBindGroups, &cpe->currentDynamicOffsets,
                &cpe->bindingCache);
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
            if (cpe->currentDynamicOffsets.size() <= groupIndex)
                cpe->currentDynamicOffsets.resize(groupIndex + 1);
            if (dynamicOffsetCount > 0 && dynamicOffsets)
                cpe->currentDynamicOffsets[groupIndex].assign(
                    dynamicOffsets, dynamicOffsets + dynamicOffsetCount);
            else
                cpe->currentDynamicOffsets[groupIndex].clear();
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

        pwgpu::BindWebGPUBindGroup(cpe->cmd, pwgpu::PipelineBindingPoint::Compute,
                                   cpe->pipeline->layout, groupIndex, group,
                                   dynamicOffsetCount, dynamicOffsets, &cpe->bindingCache);
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

        if (!pwgpu::DispatchWebGPUCompute(cpe->cmd, x, y, z))
            cpe->invalid = true;
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
            wgpuBufferAddRef(buffer);
            cpe->usedBuffers.push_back(buffer);
            return;
        }

        if (!ValidateBindGroupCompat(cpe))
            return;

        // ponytail: pe::CommandBuffer has no DispatchIndirect; DX12 needs an indirect
        // command signature path before this can be neutral. Fail the pass instead of
        // recording through the Vulkan handle on another backend.
        if (pe::GetRHI().GetApi() != PE_GRAPHICS_API_VULKAN)
        {
            ReportPassValidation(cpe,
                                 "wgpuComputePassEncoderDispatchWorkgroupsIndirect: not supported on "
                                 "this backend");
            cpe->invalid = true;
            return;
        }

        ValidateDispatchUsageScope(cpe, buffer);
        EmitDispatchResourceBarriers(cpe, buffer);

        pwgpu::FlushPendingBarriers(cpe->cmd);

        // ponytail: without device addresses (no bufferDeviceAddress) the counts are dispatched unchecked; such a
        // device needs a descriptor-bound variant of the validation shader.
        vk::CommandBuffer vkCmd = pe::GetVulkanCommandBuffer(cpe->cmd);
        if (pe::Buffer *arguments = ValidatedIndirectArguments(cpe, buffer, offset))
            vkCmd.dispatchIndirect(pe::GetVulkanBuffer(arguments), 0);
        else
            vkCmd.dispatchIndirect(pe::GetVulkanBuffer(buffer->peBuffer), offset);

        wgpuBufferAddRef(buffer);
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
            cpe->timestampQuerySet->backendQueryPool != 0)
        {
            pwgpu::WriteWebGPUTimestamp(cpe->cmd, cpe->timestampQuerySet,
                                        cpe->endTimestampIndex);
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

void pwgpu::DestroyIndirectDispatchValidation(WGPUDeviceImpl *device)
{
    if (!device || !device->indirectValidationPipeline)
        return;

    vk::Device vkDevice = pe::VulkanRhi::Device();
    vkDevice.destroyPipeline(vk::Pipeline{PeFromBackendHandle<VkPipeline>(device->indirectValidationPipeline)});
    vkDevice.destroyPipelineLayout(
        vk::PipelineLayout{PeFromBackendHandle<VkPipelineLayout>(device->indirectValidationLayout)});
    device->indirectValidationPipeline = 0;
    device->indirectValidationLayout = 0;
}
