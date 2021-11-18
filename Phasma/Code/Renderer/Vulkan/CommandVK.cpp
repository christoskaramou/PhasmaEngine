/*
Copyright (c) 2018-2021 Christos Karamoustos

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#if PE_VULKAN
#include "Renderer/Command.h"
#include "Renderer/RHI.h"
#include "Renderer/RenderPass.h"
#include "Renderer/FrameBuffer.h"
#include "Renderer/Pipeline.h"
#include "Renderer/Descriptor.h"
#include "Renderer/Buffer.h"
#include "Renderer/Image.h"

namespace pe
{
    CommandPool::CommandPool(uint32_t graphicsFamilyId)
    {
        VkCommandPoolCreateInfo cpci{};
        cpci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        cpci.queueFamilyIndex = graphicsFamilyId;
        cpci.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

        VkCommandPool commandPool;
        vkCreateCommandPool(RHII.device, &cpci, nullptr, &commandPool);
        m_handle = commandPool;
    }

    CommandPool::~CommandPool()
    {
        if (m_handle)
        {
            vkDestroyCommandPool(RHII.device, m_handle, nullptr);
            m_handle = {};
        }
    }

    CommandBuffer::CommandBuffer(CommandPool* commandPool) : m_commandPool(commandPool)
    {
        VkCommandBufferAllocateInfo cbai{};
        cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cbai.commandPool = commandPool->Handle();
        cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cbai.commandBufferCount = 1;

        VkCommandBuffer commandBuffer;
        vkAllocateCommandBuffers(RHII.device, &cbai, &commandBuffer);
        m_handle = commandBuffer;
    }

    CommandBuffer::~CommandBuffer()
    {
        if (m_handle)
        {
            VkCommandBuffer cmd = m_handle;
            vkFreeCommandBuffers(RHII.device, m_commandPool->Handle(), 1, &cmd);
            m_handle = {};
        }
    }

    void CommandBuffer::CopyBuffer(Buffer* srcBuffer, Buffer* dstBuffer, uint32_t regionCount, BufferCopy* pRegions)
    {
        std::vector<VkBufferCopy> regions(regionCount);
        for (uint32_t i = 0; i < regionCount; i++)
        {
            regions[i].srcOffset = pRegions[i].srcOffset;
            regions[i].dstOffset = pRegions[i].dstOffset;
            regions[i].size = pRegions[i].size;
        }

        vkCmdCopyBuffer(m_handle, srcBuffer->Handle(), dstBuffer->Handle(), regionCount, regions.data());
    }

    void CommandBuffer::Begin()
    {
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkBeginCommandBuffer(m_handle, &beginInfo);
    }

    void CommandBuffer::End()
    {
        vkEndCommandBuffer(m_handle);
    }

    void CommandBuffer::PipelineBarrier()
    {
    }

    void CommandBuffer::SetDepthBias(float constantFactor, float clamp, float slopeFactor)
    {
        vkCmdSetDepthBias(m_handle, constantFactor, clamp, slopeFactor);
    }

    void CommandBuffer::BlitImage(
        Image* srcImage, ImageLayout srcImageLayout,
        Image* dstImage, ImageLayout dstImageLayout,
        uint32_t regionCount, ImageBlit* pRegions,
        Filter filter)
    {
        std::vector<VkImageBlit> regions(regionCount);
        for (uint32_t i = 0; i < regionCount; i++)
        {
            regions[i].srcOffsets[0].x = pRegions[i].srcOffsets[0].x;
            regions[i].srcOffsets[0].y = pRegions[i].srcOffsets[0].y;
            regions[i].srcOffsets[0].z = pRegions[i].srcOffsets[0].z;
            regions[i].srcOffsets[1].x = pRegions[i].srcOffsets[1].x;
            regions[i].srcOffsets[1].y = pRegions[i].srcOffsets[1].y;
            regions[i].srcOffsets[1].z = pRegions[i].srcOffsets[1].z;
            regions[i].srcSubresource.aspectMask = pRegions[i].srcSubresource.aspectMask;
            regions[i].srcSubresource.mipLevel = pRegions[i].srcSubresource.mipLevel;
            regions[i].srcSubresource.baseArrayLayer = pRegions[i].srcSubresource.baseArrayLayer;
            regions[i].srcSubresource.layerCount = pRegions[i].srcSubresource.layerCount;
            regions[i].dstOffsets[0].x = pRegions[i].dstOffsets[0].x;
            regions[i].dstOffsets[0].y = pRegions[i].dstOffsets[0].y;
            regions[i].dstOffsets[0].z = pRegions[i].dstOffsets[0].z;
            regions[i].dstOffsets[1].x = pRegions[i].dstOffsets[1].x;
            regions[i].dstOffsets[1].y = pRegions[i].dstOffsets[1].y;
            regions[i].dstOffsets[1].z = pRegions[i].dstOffsets[1].z;
            regions[i].dstSubresource.aspectMask = pRegions[i].dstSubresource.aspectMask;
            regions[i].dstSubresource.mipLevel = pRegions[i].dstSubresource.mipLevel;
            regions[i].dstSubresource.baseArrayLayer = pRegions[i].dstSubresource.baseArrayLayer;
            regions[i].dstSubresource.layerCount = pRegions[i].dstSubresource.layerCount;
        }

        vkCmdBlitImage(m_handle,
            srcImage->Handle(), (VkImageLayout)srcImageLayout,
            dstImage->Handle(), (VkImageLayout)dstImageLayout,
            regionCount, regions.data(),
            (VkFilter)filter);
    }

    bool DepthFormat(Format format)
    {
        return format == VK_FORMAT_D32_SFLOAT_S8_UINT ||
            format == VK_FORMAT_D32_SFLOAT ||
            format == VK_FORMAT_D24_UNORM_S8_UINT;
    }

    void CommandBuffer::BeginPass(RenderPass* pass, FrameBuffer* frameBuffer)
    {
        const vec4 color(0.0f, 0.0f, 0.0f, 1.0f);
        VkClearValue clearColor;
        memcpy(clearColor.color.float32, &color, sizeof(vec4));

        VkClearDepthStencilValue depthStencil;
        depthStencil.depth = GlobalSettings::ReverseZ ? 0.f : 1.f;
        depthStencil.stencil = 0;

        std::vector<VkClearValue> clearValues(pass->attachments.size());
        for (int i = 0; i < pass->attachments.size(); i++)
        {
            if (DepthFormat(pass->attachments[i].format))
                clearValues[i].depthStencil = depthStencil;
            else
                clearValues[i].color = clearColor.color;

        }

        VkRenderPassBeginInfo rpi{};
        rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpi.renderPass = pass->Handle();
        rpi.framebuffer = frameBuffer->Handle();
        rpi.renderArea.offset = VkOffset2D{ 0, 0 };
        rpi.renderArea.extent = VkExtent2D{ frameBuffer->width, frameBuffer->height };
        rpi.clearValueCount = (uint32_t)clearValues.size();
        rpi.pClearValues = clearValues.data();

        vkCmdBeginRenderPass(m_handle, &rpi, VK_SUBPASS_CONTENTS_INLINE);
    }

    void CommandBuffer::EndPass()
    {
        vkCmdEndRenderPass(m_handle);
    }

    void CommandBuffer::BindPipeline(Pipeline* pipeline)
    {
        vkCmdBindPipeline(m_handle, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->Handle());
    }

    void CommandBuffer::BindComputePipeline(Pipeline* pipeline)
    {
        vkCmdBindPipeline(m_handle, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->Handle());
    }

    void CommandBuffer::BindVertexBuffer(Buffer* buffer, size_t offset, uint32_t firstBinding, uint32_t bindingCount)
    {
        VkBuffer buff = buffer->Handle();
        vkCmdBindVertexBuffers(m_handle, firstBinding, bindingCount, &buff, &offset);
    }

    void CommandBuffer::BindIndexBuffer(Buffer*buffer, size_t offset)
    {
        vkCmdBindIndexBuffer(m_handle, buffer->Handle(), offset, VK_INDEX_TYPE_UINT32);
    }

    void CommandBuffer::BindDescriptors(Pipeline* pipeline, uint32_t count, Descriptor** descriptors)
    {
        std::vector<VkDescriptorSet> dsets(count);
        for (uint32_t i = 0; i < count; i++)
            dsets[i] = descriptors[i]->Handle();

        vkCmdBindDescriptorSets(m_handle, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->layout, 0, count, dsets.data(), 0, nullptr);
    }

    void CommandBuffer::BindComputeDescriptors(Pipeline* pipeline, uint32_t count, Descriptor** descriptors)
    {
        std::vector<VkDescriptorSet> dsets(count);
        for (uint32_t i = 0; i < count; i++)
            dsets[i] = descriptors[i]->Handle();

        vkCmdBindDescriptorSets(m_handle, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->layout, 0, count, dsets.data(), 0, nullptr);
    }

    void CommandBuffer::Dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
    {
        vkCmdDispatch(m_handle, groupCountX, groupCountY, groupCountZ);
    }

    void CommandBuffer::PushConstants(Pipeline* pipeline, ShaderStageFlags shaderStageFlags, uint32_t offset, uint32_t size, const void* pValues)
    {
        vkCmdPushConstants(m_handle, pipeline->layout, shaderStageFlags, offset, size, pValues);
    }

    void CommandBuffer::Draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance)
    {
        vkCmdDraw(m_handle, vertexCount, instanceCount, firstVertex, firstInstance);
    }

    void CommandBuffer::DrawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance)
    {
        vkCmdDrawIndexed(m_handle, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
    }

    void CommandBuffer::Submit()
    {
    }
}
#endif
