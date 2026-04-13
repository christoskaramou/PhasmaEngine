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
    bool EncoderOpen(WGPURenderBundleEncoder rbe, const char *apiName)
    {
        if (!rbe)
            return false;
        if (rbe->finished)
        {
            PE_WARN("[WebGPU] %s: render bundle encoder is already finished", apiName);
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
            return;

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
        if (!rbe->pipeline || !rbe->pipeline->layout)
            return;

        if (rbe->device && groupIndex >= rbe->device->limits.maxBindGroups)
            return;

        if (!group || group->invalid || !group->descriptor)
            return;

        auto &bgls = rbe->pipeline->layout->bindGroupLayouts;
        if (groupIndex >= bgls.size() || !bgls[groupIndex])
            return;
        if (group->layout != bgls[groupIndex])
        {
            PE_WARN("[WebGPU] renderBundleEncoder setBindGroup: layout mismatch at index %u", groupIndex);
            return;
        }

        if (static_cast<uint32_t>(dynamicOffsetCount) != group->layout->dynamicOffsetCount)
        {
            PE_WARN("[WebGPU] renderBundleEncoder setBindGroup: dynamicOffsetCount %zu != expected %u",
                    dynamicOffsetCount, group->layout->dynamicOffsetCount);
            return;
        }

        if (dynamicOffsetCount > 0 && !dynamicOffsets)
            return;

        if (rbe->device && dynamicOffsetCount > 0 && dynamicOffsets)
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
                        if (offset % rbe->device->limits.minUniformBufferOffsetAlignment != 0)
                            return;
                    }
                    else
                    {
                        if (offset % rbe->device->limits.minStorageBufferOffsetAlignment != 0)
                            return;
                    }
                    dynIdx++;
                }
            }
        }

        wgpuBindGroupAddRef(group);
        rbe->retainedBindGroups.push_back(group);

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
        if (!buffer || !buffer->peBuffer || buffer->destroyed)
            return;
        if (!(buffer->usage & WGPUBufferUsage_Vertex))
            return;

        wgpuBufferAddRef(buffer);
        rbe->retainedBuffers.push_back(buffer);

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
        if (!buffer || !buffer->peBuffer || buffer->destroyed)
            return;
        if (!(buffer->usage & WGPUBufferUsage_Index))
            return;

        wgpuBufferAddRef(buffer);
        rbe->retainedBuffers.push_back(buffer);

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
        if (!rbe->pipeline)
            return;

        rbe->drawCount++;
        rbe->commands.push_back([indexCount, instanceCount, firstIndex, baseVertex, firstInstance](vk::CommandBuffer cmd)
                                { cmd.drawIndexed(indexCount, instanceCount, firstIndex, baseVertex, firstInstance); });
    }

    void wgpuRenderBundleEncoderDrawIndirect(WGPURenderBundleEncoder rbe, WGPUBuffer buffer, uint64_t offset)
    {
        if (!EncoderOpen(rbe, "wgpuRenderBundleEncoderDrawIndirect"))
            return;
        if (!rbe->pipeline)
            return;
        if (!buffer || !buffer->peBuffer || buffer->destroyed)
            return;
        if (!(buffer->usage & WGPUBufferUsage_Indirect))
            return;
        if (offset + sizeof(VkDrawIndirectCommand) > buffer->size)
            return;
        if (offset % 4 != 0)
            return;

        wgpuBufferAddRef(buffer);
        rbe->retainedBuffers.push_back(buffer);

        vk::Buffer vkBuf = buffer->peBuffer->ApiHandle();
        rbe->drawCount++;
        rbe->commands.push_back([vkBuf, offset](vk::CommandBuffer cmd)
                                { cmd.drawIndirect(vkBuf, offset, 1, sizeof(VkDrawIndirectCommand)); });
    }

    void wgpuRenderBundleEncoderDrawIndexedIndirect(WGPURenderBundleEncoder rbe, WGPUBuffer buffer, uint64_t offset)
    {
        if (!EncoderOpen(rbe, "wgpuRenderBundleEncoderDrawIndexedIndirect"))
            return;
        if (!rbe->pipeline)
            return;
        if (!buffer || !buffer->peBuffer || buffer->destroyed)
            return;
        if (!(buffer->usage & WGPUBufferUsage_Indirect))
            return;
        if (offset + sizeof(VkDrawIndexedIndirectCommand) > buffer->size)
            return;
        if (offset % 4 != 0)
            return;

        wgpuBufferAddRef(buffer);
        rbe->retainedBuffers.push_back(buffer);

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
