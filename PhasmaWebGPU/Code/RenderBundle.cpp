#include "RenderBundle.h"
#include "RenderPipeline.h"
#include "PipelineLayout.h"
#include "BindGroup.h"
#include "Buffer.h"
#include "Device.h"
#include "Utils.h"

extern "C" void wgpuDeviceRelease(WGPUDevice);
extern "C" void wgpuRenderPipelineAddRef(WGPURenderPipeline);
extern "C" void wgpuRenderPipelineRelease(WGPURenderPipeline);
extern "C" void wgpuBindGroupAddRef(WGPUBindGroup);
extern "C" void wgpuBindGroupRelease(WGPUBindGroup);
extern "C" void wgpuBufferAddRef(WGPUBuffer);
extern "C" void wgpuBufferRelease(WGPUBuffer);

namespace
{
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
} // namespace

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
        if (!pipeline || pipeline->invalid || pipeline->vkPipeline == VK_NULL_HANDLE)
        {
            rbe->invalid = true;
            return;
        }
        if (pipeline->device != rbe->device)
        {
            rbe->invalid = true;
            return;
        }

        if (pipeline->sampleCount != rbe->sampleCount)
        {
            PE_WARN("[WebGPU] renderBundleEncoder setPipeline: pipeline sampleCount %u != encoder sampleCount %u",
                    pipeline->sampleCount, rbe->sampleCount);
            rbe->invalid = true;
            return;
        }
        if (pipeline->depthStencilFormat != rbe->depthStencilFormat)
        {
            PE_WARN("[WebGPU] renderBundleEncoder setPipeline: pipeline depthStencilFormat mismatch");
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
            PE_WARN("[WebGPU] renderBundleEncoder setPipeline: pipeline colorFormats mismatch");
            rbe->invalid = true;
            return;
        }

        if (pipeline->writesDepth && rbe->depthReadOnly)
        {
            rbe->invalid = true;
            return;
        }
        if (pipeline->writesStencil && rbe->stencilReadOnly)
        {
            rbe->invalid = true;
            return;
        }

        rbe->pipeline = pipeline;

        wgpuRenderPipelineAddRef(pipeline);
        rbe->retainedPipelines.push_back(pipeline);

        VkPipeline vkp = pipeline->vkPipeline;
        rbe->commands.push_back([vkp](vk::CommandBuffer cmd)
                                { cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, vkp); });
    }

    void wgpuRenderBundleEncoderSetBindGroup(WGPURenderBundleEncoder rbe, uint32_t groupIndex,
                                             WGPUBindGroup group,
                                             size_t dynamicOffsetCount, uint32_t const *dynamicOffsets)
    {
        if (!EncoderOpen(rbe, "wgpuRenderBundleEncoderSetBindGroup"))
            return;

        if (rbe->device && groupIndex >= rbe->device->limits.maxBindGroups)
        {
            rbe->invalid = true;
            return;
        }

        if (group)
        {
            if (group->device != rbe->device)
            {
                rbe->invalid = true;
                return;
            }
            if (group->invalid)
            {
                rbe->invalid = true;
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
        }

        if (group && group->layout)
        {
            if (static_cast<uint32_t>(dynamicOffsetCount) != group->layout->dynamicOffsetCount)
            {
                rbe->invalid = true;
                return;
            }
            if (dynamicOffsetCount > 0 && !dynamicOffsets)
            {
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
                        rbe->invalid = true;
                        return;
                    }
                    if (dyn.buffer)
                    {
                        uint64_t bufSize = dyn.buffer->size;
                        uint64_t effOffset = dyn.baseOffset + static_cast<uint64_t>(offset);
                        if (effOffset > bufSize || dyn.bindingSize > bufSize - effOffset)
                        {
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
        {
            PE_WARN("[WebGPU] renderBundleEncoder setBindGroup: layout mismatch at index %u", groupIndex);
            return;
        }

        VkPipelineLayout vkLayout = rbe->pipeline->layout->vkLayout;
        vk::DescriptorSet ds = group->descriptor->ApiHandle();
        std::vector<uint32_t> dynOffsets(dynamicOffsets, dynamicOffsets + dynamicOffsetCount);

        rbe->commands.push_back([vkLayout, groupIndex, ds, dynOffsets](vk::CommandBuffer cmd)
                                { cmd.bindDescriptorSets(
                                      vk::PipelineBindPoint::eGraphics,
                                      vk::PipelineLayout(vkLayout), groupIndex,
                                      1, &ds,
                                      static_cast<uint32_t>(dynOffsets.size()),
                                      dynOffsets.empty() ? nullptr : dynOffsets.data()); });
    }

    void wgpuRenderBundleEncoderSetVertexBuffer(WGPURenderBundleEncoder rbe, uint32_t slot,
                                                WGPUBuffer buffer, uint64_t offset, uint64_t size)
    {
        if (!EncoderOpen(rbe, "wgpuRenderBundleEncoderSetVertexBuffer"))
            return;

        if (rbe->device && slot >= rbe->device->limits.maxVertexBuffers)
        {
            rbe->invalid = true;
            return;
        }
        if (offset % 4u != 0u)
        {
            rbe->invalid = true;
            return;
        }
        uint64_t bufferSize = buffer ? buffer->size : 0u;
        if (size == WGPU_WHOLE_SIZE)
            size = (offset > bufferSize) ? 0u : (bufferSize - offset);
        if (offset > bufferSize || size > bufferSize - offset)
        {
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
            rbe->invalid = true;
            return;
        }
        if (!(buffer->usage & WGPUBufferUsage_Vertex))
        {
            rbe->invalid = true;
            return;
        }

        if (rbe->boundVertexBuffers.size() <= slot)
            rbe->boundVertexBuffers.resize(slot + 1);
        rbe->boundVertexBuffers[slot].bound = true;
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

        vk::Buffer vkBuf = buffer->peBuffer->ApiHandle();
        vk::DeviceSize vkOffset = static_cast<vk::DeviceSize>(offset);

        rbe->commands.push_back([vkBuf, slot, vkOffset](vk::CommandBuffer cmd)
                                { cmd.bindVertexBuffers(slot, 1, &vkBuf, &vkOffset); });
    }

    void wgpuRenderBundleEncoderSetIndexBuffer(WGPURenderBundleEncoder rbe, WGPUBuffer buffer,
                                               WGPUIndexFormat format, uint64_t offset, uint64_t size)
    {
        if (!EncoderOpen(rbe, "wgpuRenderBundleEncoderSetIndexBuffer"))
            return;

        if (!buffer)
        {
            rbe->invalid = true;
            return;
        }
        if (buffer->device != rbe->device || buffer->invalid)
        {
            rbe->invalid = true;
            return;
        }
        if (!(buffer->usage & WGPUBufferUsage_Index))
        {
            rbe->invalid = true;
            return;
        }
        if (format != WGPUIndexFormat_Uint16 && format != WGPUIndexFormat_Uint32)
        {
            rbe->invalid = true;
            return;
        }
        uint32_t indexSize = (format == WGPUIndexFormat_Uint16) ? 2u : 4u;
        if (offset % indexSize != 0u)
        {
            rbe->invalid = true;
            return;
        }
        uint64_t boundSize = size;
        if (boundSize == WGPU_WHOLE_SIZE)
            boundSize = (offset > buffer->size) ? 0u : (buffer->size - offset);
        if (offset > buffer->size || boundSize > buffer->size - offset)
        {
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

        vk::Buffer vkBuf = buffer->peBuffer->ApiHandle();
        vk::IndexType indexType = (format == WGPUIndexFormat_Uint16) ? vk::IndexType::eUint16 : vk::IndexType::eUint32;

        rbe->commands.push_back([vkBuf, offset, indexType](vk::CommandBuffer cmd)
                                { cmd.bindIndexBuffer(vkBuf, offset, indexType); });
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
        rbe->commands.push_back([vertexCount, instanceCount, firstVertex, firstInstance](vk::CommandBuffer cmd)
                                { cmd.draw(vertexCount, instanceCount, firstVertex, firstInstance); });
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
        rbe->commands.push_back([indexCount, instanceCount, firstIndex, baseVertex, firstInstance](vk::CommandBuffer cmd)
                                { cmd.drawIndexed(indexCount, instanceCount, firstIndex, baseVertex, firstInstance); });
    }

    void wgpuRenderBundleEncoderDrawIndirect(WGPURenderBundleEncoder rbe, WGPUBuffer buffer, uint64_t offset)
    {
        if (!EncoderOpen(rbe, "wgpuRenderBundleEncoderDrawIndirect"))
            return;

        if (!buffer)
        {
            rbe->invalid = true;
            return;
        }
        if (buffer->device != rbe->device || buffer->invalid)
        {
            rbe->invalid = true;
            return;
        }
        if (!(buffer->usage & WGPUBufferUsage_Indirect))
        {
            rbe->invalid = true;
            return;
        }
        if (offset % 4u != 0u)
        {
            rbe->invalid = true;
            return;
        }
        constexpr uint64_t kDrawArgsSize = sizeof(VkDrawIndirectCommand);
        if (offset > buffer->size || kDrawArgsSize > buffer->size - offset)
        {
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

        vk::Buffer vkBuf = buffer->peBuffer->ApiHandle();
        rbe->drawCount++;
        rbe->commands.push_back([vkBuf, offset](vk::CommandBuffer cmd)
                                { cmd.drawIndirect(vkBuf, offset, 1, sizeof(VkDrawIndirectCommand)); });
    }

    void wgpuRenderBundleEncoderDrawIndexedIndirect(WGPURenderBundleEncoder rbe, WGPUBuffer buffer, uint64_t offset)
    {
        if (!EncoderOpen(rbe, "wgpuRenderBundleEncoderDrawIndexedIndirect"))
            return;

        if (!buffer)
        {
            rbe->invalid = true;
            return;
        }
        if (buffer->device != rbe->device || buffer->invalid)
        {
            rbe->invalid = true;
            return;
        }
        if (!(buffer->usage & WGPUBufferUsage_Indirect))
        {
            rbe->invalid = true;
            return;
        }
        if (offset % 4u != 0u)
        {
            rbe->invalid = true;
            return;
        }
        constexpr uint64_t kDrawArgsSize = sizeof(VkDrawIndexedIndirectCommand);
        if (offset > buffer->size || kDrawArgsSize > buffer->size - offset)
        {
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

        vk::Buffer vkBuf = buffer->peBuffer->ApiHandle();
        rbe->drawCount++;
        rbe->commands.push_back([vkBuf, offset](vk::CommandBuffer cmd)
                                { cmd.drawIndexedIndirect(vkBuf, offset, 1, sizeof(VkDrawIndexedIndirectCommand)); });
    }

    WGPURenderBundle wgpuRenderBundleEncoderFinish(WGPURenderBundleEncoder rbe,
                                                   WGPURenderBundleDescriptor const *descriptor)
    {
        if (!rbe)
            return nullptr;

        if (rbe->finished)
        {
            PE_WARN("[WebGPU] wgpuRenderBundleEncoderFinish: encoder already finished");
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
                PE_WARN("[WebGPU] wgpuRenderBundleEncoderFinish: %u debug group(s) still open", rbe->debugGroupDepth);
            return rb;
        }

        rb->colorFormats = std::move(rbe->colorFormats);
        rb->depthStencilFormat = rbe->depthStencilFormat;
        rb->sampleCount = rbe->sampleCount;
        rb->depthReadOnly = rbe->depthReadOnly;
        rb->stencilReadOnly = rbe->stencilReadOnly;

        rb->drawCount = rbe->drawCount;
        rb->commands = std::move(rbe->commands);

        rb->retainedPipelines = std::move(rbe->retainedPipelines);
        rb->retainedBindGroups = std::move(rbe->retainedBindGroups);
        rb->retainedBuffers = std::move(rbe->retainedBuffers);

        rb->usageScope = std::move(rbe->usageScope);
        rb->usageScopeValid = rbe->usageScopeValid;

        return rb;
    }

    void wgpuRenderBundleEncoderInsertDebugMarker(WGPURenderBundleEncoder rbe, WGPUStringView label)
    {
        if (!EncoderOpen(rbe, "wgpuRenderBundleEncoderInsertDebugMarker"))
            return;

        std::string str = pwgpu::ToString(label);
        rbe->commands.push_back([str](vk::CommandBuffer cmd)
                                {
            vk::DebugUtilsLabelEXT labelInfo{};
            labelInfo.pLabelName = str.c_str();
            cmd.insertDebugUtilsLabelEXT(labelInfo); });
    }

    void wgpuRenderBundleEncoderPushDebugGroup(WGPURenderBundleEncoder rbe, WGPUStringView groupLabel)
    {
        if (!EncoderOpen(rbe, "wgpuRenderBundleEncoderPushDebugGroup"))
            return;
        rbe->debugGroupDepth++;

        std::string str = pwgpu::ToString(groupLabel);
        rbe->commands.push_back([str](vk::CommandBuffer cmd)
                                {
            vk::DebugUtilsLabelEXT labelInfo{};
            labelInfo.pLabelName = str.c_str();
            cmd.beginDebugUtilsLabelEXT(labelInfo); });
    }

    void wgpuRenderBundleEncoderPopDebugGroup(WGPURenderBundleEncoder rbe)
    {
        if (!EncoderOpen(rbe, "wgpuRenderBundleEncoderPopDebugGroup"))
            return;
        if (rbe->debugGroupDepth == 0)
        {
            PE_WARN("[WebGPU] renderBundleEncoder popDebugGroup: no matching pushDebugGroup");
            return;
        }
        rbe->debugGroupDepth--;

        rbe->commands.push_back([](vk::CommandBuffer cmd)
                                { cmd.endDebugUtilsLabelEXT(); });
    }

    void wgpuRenderBundleEncoderSetLabel(WGPURenderBundleEncoder rbe, WGPUStringView label)
    {
        if (rbe)
            rbe->label = pwgpu::ToString(label);
    }

} // extern "C"
