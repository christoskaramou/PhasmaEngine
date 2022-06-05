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
#include "Renderer/Queue.h"
#include "Renderer/Semaphore.h"
#include "Renderer/Fence.h"

namespace pe
{
    CommandPool::CommandPool(uint32_t familyId, const std::string &name)
    {
        m_familyId = familyId;

        VkCommandPoolCreateInfo cpci{};
        cpci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        cpci.queueFamilyIndex = familyId;
        cpci.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

        VkCommandPool commandPool;
        PE_CHECK(vkCreateCommandPool(RHII.device, &cpci, nullptr, &commandPool));
        m_handle = commandPool;

        Debug::SetObjectName(m_handle, VK_OBJECT_TYPE_COMMAND_POOL, name);
    }

    CommandPool::~CommandPool()
    {
        if (m_handle)
        {
            vkDestroyCommandPool(RHII.device, m_handle, nullptr);
            m_handle = {};
        }
    }

    void CommandPool::Init(GpuHandle gpu)
    {
        uint32_t queueFamPropCount;
        vkGetPhysicalDeviceQueueFamilyProperties(gpu, &queueFamPropCount, nullptr);

        // CommandPools are creating CommandBuffers from a specific family id, this will have to
        // match with the Queue created from a falily id that this CommandBuffer will be submited to.
        s_availableCps = std::vector<std::unordered_set<CommandPool *>>(queueFamPropCount);
        for (uint32_t i = 0; i < queueFamPropCount; i++)
            s_availableCps[i] = std::unordered_set<CommandPool *>{};
    }

    void CommandPool::Clear()
    {
        for (auto &cp : s_allCps)
            CommandPool::Destroy(cp);

        s_allCps.clear();
        s_availableCps.clear();
    }

    CommandPool *CommandPool::GetNext(uint32_t familyId)
    {
        std::lock_guard<std::mutex> lock(s_availableMutex);

        auto &cps = s_availableCps[familyId];
        if (cps.empty())
        {
            CommandPool *cp = CommandPool::Create(familyId, "CommandPool");
            cps.insert(cp);
            s_allCps.insert(cp);
        }

        CommandPool *cp = *--cps.end();
        cps.erase(cp);
        return cp;
    }

    void CommandPool::Return(CommandPool *commandPool)
    {
        std::lock_guard<std::mutex> lock(s_availableMutex);

        uint32_t familyId = commandPool->GetFamilyId();
        auto &cps = s_availableCps[familyId];
        if (cps.find(commandPool) == cps.end())
            cps.insert(commandPool);
    }

    CommandBuffer::CommandBuffer(uint32_t familyId, const std::string &name)
    {
        m_familyId = familyId;
        m_commandPool = CommandPool::GetNext(familyId);
        m_fence = nullptr;

        VkCommandBufferAllocateInfo cbai{};
        cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cbai.commandPool = m_commandPool->Handle();
        cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cbai.commandBufferCount = 1;

        VkCommandBuffer commandBuffer;
        PE_CHECK(vkAllocateCommandBuffers(RHII.device, &cbai, &commandBuffer));
        m_handle = commandBuffer;

        Debug::SetObjectName(m_handle, VK_OBJECT_TYPE_COMMAND_BUFFER, name);

        this->name = name;
    }

    CommandBuffer::~CommandBuffer()
    {
        if (m_handle)
        {
            VkCommandBuffer cmd = m_handle;
            vkFreeCommandBuffers(RHII.device, m_commandPool->Handle(), 1, &cmd);
            m_handle = {};
        }

        if (m_commandPool)
        {
            CommandPool::Return(m_commandPool);
            m_commandPool = nullptr;
        }
    }

    void CommandBuffer::CopyBuffer(Buffer *srcBuffer, Buffer *dstBuffer, uint32_t regionCount, BufferCopy *pRegions)
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
        beginInfo.pInheritanceInfo = nullptr;

        PE_CHECK(vkBeginCommandBuffer(m_handle, &beginInfo));
    }

    void CommandBuffer::End()
    {
        PE_CHECK(vkEndCommandBuffer(m_handle));
    }

    void CommandBuffer::PipelineBarrier()
    {
    }

    void CommandBuffer::SetDepthBias(float constantFactor, float clamp, float slopeFactor)
    {
        vkCmdSetDepthBias(m_handle, constantFactor, clamp, slopeFactor);
    }

    void CommandBuffer::BlitImage(
        Image *srcImage, ImageLayout srcImageLayout,
        Image *dstImage, ImageLayout dstImageLayout,
        uint32_t regionCount, ImageBlit *pRegions,
        Filter filter)
    {
        std::vector<VkImageBlit> regions(regionCount);
        memcpy(regions.data(), pRegions, regionCount * sizeof(VkImageBlit));

        vkCmdBlitImage(m_handle,
                       srcImage->Handle(), (VkImageLayout)srcImageLayout,
                       dstImage->Handle(), (VkImageLayout)dstImageLayout,
                       regionCount, regions.data(),
                       (VkFilter)filter);
    }

    bool IsDepth(Format format)
    {
        return format == VK_FORMAT_D32_SFLOAT_S8_UINT ||
               format == VK_FORMAT_D32_SFLOAT ||
               format == VK_FORMAT_D24_UNORM_S8_UINT;
    }

    void CommandBuffer::BeginPass(RenderPass *pass, FrameBuffer *frameBuffer)
    {
        std::vector<VkClearValue> clearValues(pass->attachments.size());
        for (int i = 0; i < pass->attachments.size(); i++)
        {
            if (IsDepth(pass->attachments[i].format))
                clearValues[i].depthStencil = {GlobalSettings::ReverseZ ? 0.f : 1.f, 0};
            else
                clearValues[i].color = {1.0f, 0.0f, 0.0f, 1.0f};
        }

        VkRenderPassBeginInfo rpi{};
        rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpi.renderPass = pass->Handle();
        rpi.framebuffer = frameBuffer->Handle();
        rpi.renderArea.offset = VkOffset2D{0, 0};
        rpi.renderArea.extent = VkExtent2D{frameBuffer->width, frameBuffer->height};
        rpi.clearValueCount = static_cast<uint32_t>(clearValues.size());
        rpi.pClearValues = clearValues.data();

        vkCmdBeginRenderPass(m_handle, &rpi, VK_SUBPASS_CONTENTS_INLINE);
    }

    void CommandBuffer::EndPass()
    {
        vkCmdEndRenderPass(m_handle);
    }

    void CommandBuffer::BindPipeline(Pipeline *pipeline)
    {
        vkCmdBindPipeline(m_handle, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->Handle());
    }

    void CommandBuffer::BindComputePipeline(Pipeline *pipeline)
    {
        vkCmdBindPipeline(m_handle, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->Handle());
    }

    void CommandBuffer::BindVertexBuffer(Buffer *buffer, size_t offset, uint32_t firstBinding, uint32_t bindingCount)
    {
        VkBuffer buff = buffer->Handle();
        vkCmdBindVertexBuffers(m_handle, firstBinding, bindingCount, &buff, &offset);
    }

    void CommandBuffer::BindIndexBuffer(Buffer *buffer, size_t offset)
    {
        vkCmdBindIndexBuffer(m_handle, buffer->Handle(), offset, VK_INDEX_TYPE_UINT32);
    }

    void CommandBuffer::BindDescriptors(Pipeline *pipeline, uint32_t count, Descriptor **descriptors)
    {
        std::vector<VkDescriptorSet> dsets(count);
        for (uint32_t i = 0; i < count; i++)
            dsets[i] = descriptors[i]->Handle();

        vkCmdBindDescriptorSets(m_handle, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->layout, 0, count, dsets.data(), 0, nullptr);
    }

    void CommandBuffer::BindComputeDescriptors(Pipeline *pipeline, uint32_t count, Descriptor **descriptors)
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

    void CommandBuffer::PushConstants(Pipeline *pipeline, ShaderStageFlags shaderStageFlags, uint32_t offset, uint32_t size, const void *pValues)
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

    void CommandBuffer::Submit(Queue *queue,
                               PipelineStageFlags *waitStages,
                               uint32_t waitSemaphoresCount, Semaphore **waitSemaphores,
                               uint32_t signalSemaphoresCount, Semaphore **signalSemaphores,
                               Fence *fence)
    {
        m_fence = fence;
        CommandBuffer *cmd = this;
        queue->Submit(1, &cmd,
                      waitStages,
                      waitSemaphoresCount, waitSemaphores,
                      signalSemaphoresCount, signalSemaphores,
                      fence);
    }

    void CommandBuffer::Init(GpuHandle gpu, uint32_t countPerFamily)
    {
        uint32_t queueFamPropCount;
        vkGetPhysicalDeviceQueueFamilyProperties(gpu, &queueFamPropCount, nullptr);

        // CommandPools are creating CommandBuffers from a specific family id, this will have to
        // match with the Queue created from a falily id that this CommandBuffer will be submited to.
        s_allCmds = std::vector<std::map<CommandBuffer *, CommandBuffer *>>(queueFamPropCount);
        s_availableCmds = std::vector<std::unordered_map<CommandBuffer *, CommandBuffer *>>(queueFamPropCount);
        for (uint32_t i = 0; i < queueFamPropCount; i++)
        {
            s_allCmds[i] = std::map<CommandBuffer *, CommandBuffer *>();
            s_availableCmds[i] = std::unordered_map<CommandBuffer *, CommandBuffer *>();
            for (uint32_t j = 0; j < countPerFamily; j++)
            {
                CommandBuffer *cmd = CommandBuffer::Create(i, "CommandBuffer_" + std::to_string(i) + "_" + std::to_string(j));
                s_allCmds[i][cmd] = cmd;
                s_availableCmds[i][cmd] = cmd;
            }
        }
    }

    void CommandBuffer::Clear()
    {
        auto &allCmds = s_allCmds;
        for (auto &cmds : allCmds)
        {
            for (auto cmd : cmds)
                CommandBuffer::Destroy(cmd.second);
        }

        allCmds.clear();
        s_availableCmds.clear();
    }

    void CommandBuffer::CheckFutures()
    {
        // join finished futures
        auto &queueWaits = s_queueWaits;
        for (auto it = queueWaits.begin(); it != queueWaits.end();)
        {
            if (it->second.wait_for(std::chrono::seconds(0)) != std::future_status::timeout)
            {
                CommandBuffer *cmd = it->second.get();
                cmd->m_fence = nullptr;
                s_availableCmds[cmd->GetFamilyId()][cmd] = cmd;

                it = queueWaits.erase(it);
            }
            else
                ++it;
        }
    }

    CommandBuffer *CommandBuffer::GetNext(uint32_t familyId)
    {
        s_availableCmdsReady = false;

        CheckFutures();

        if (s_availableCmds[familyId].empty())
        {
            CommandBuffer *cmd = CommandBuffer::Create(familyId, "CommandBuffer_" + std::to_string(familyId) + "_" + std::to_string(s_allCmds[familyId].size()));
            s_availableCmds[familyId][cmd] = cmd;
            s_allCmds[familyId][cmd] = cmd;
        }

        CommandBuffer *commandBuffer = s_availableCmds[familyId].begin()->second;
        s_availableCmds[familyId].erase(commandBuffer);

        s_availableCmdsReady = true;

        return commandBuffer;
    }

    void CommandBuffer::Return(CommandBuffer *cmd)
    {
        if (!cmd || !cmd->Handle())
            return;

        if (s_queueWaits.find(cmd) != s_queueWaits.end())
            return;

        auto func = [cmd]()
        {
            if (cmd->m_fence && cmd->m_fence->Handle())
            {
                while (!cmd->m_fence->m_canReturnToPool)
                    std::this_thread::yield();
            }

            while (!s_availableCmdsReady)
                std::this_thread::yield();

            return cmd;
        };

        // start a new thread to return the cmd after checked
        auto future = std::async(std::launch::async, func);

        // add it to the deque
        s_queueWaits[cmd] = std::move(future);
    }
}
#endif
