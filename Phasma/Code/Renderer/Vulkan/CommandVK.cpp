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
        PE_CHECK(vkCreateCommandPool(RHII.GetDevice(), &cpci, nullptr, &commandPool));
        m_handle = commandPool;

        Debug::SetObjectName(m_handle, ObjectType::CommandPool, name);
    }

    CommandPool::~CommandPool()
    {
        if (m_handle)
        {
            vkDestroyCommandPool(RHII.GetDevice(), m_handle, nullptr);
            m_handle = {};
        }
    }

    void CommandPool::Init(GpuHandle gpu)
    {
        uint32_t queueFamPropCount;
        vkGetPhysicalDeviceQueueFamilyProperties(gpu, &queueFamPropCount, nullptr);

        // CommandPools are creating CommandBuffers from a specific family id, this will have to
        // match with the Queue created from a falily id that this CommandBuffer will be submited to.
        s_allCps = std::vector<std::unordered_map<CommandPool *, CommandPool *>>(queueFamPropCount);
        s_availableCps = std::vector<std::unordered_map<CommandPool *, CommandPool *>>(queueFamPropCount);
        for (uint32_t i = 0; i < queueFamPropCount; i++)
            s_availableCps[i] = std::unordered_map<CommandPool *, CommandPool *>{};
    }

    void CommandPool::Clear()
    {
        for (auto &cps : s_allCps)
        {
            for (auto &cp : cps)
                CommandPool::Destroy(cp.second);
        }

        s_allCps.clear();
        s_availableCps.clear();
    }

    CommandPool *CommandPool::GetNext(uint32_t familyId)
    {
        std::lock_guard<std::mutex> lock(s_getNextMutex);

        if (s_availableCps[familyId].empty())
        {
            CommandPool *cp = CommandPool::Create(familyId, "CommandPool_" + std::to_string(familyId) + "_" + std::to_string(s_allCps[familyId].size()));
            s_allCps[familyId][cp] = cp;
            return cp;
        }

        CommandPool *cp = s_availableCps[familyId].begin()->second;
        s_availableCps[familyId].erase(cp);
        return cp;
    }

    void CommandPool::Return(CommandPool *commandPool)
    {
        std::lock_guard<std::mutex> lock(s_returnMutex);

        uint32_t familyId = commandPool->GetFamilyId();

        if (s_availableCps[familyId].find(commandPool) == s_availableCps[familyId].end())
            s_availableCps[familyId][commandPool] = commandPool;
    }

    CommandBuffer::CommandBuffer(uint32_t familyId, const std::string &name)
    {
        m_familyId = familyId;
        m_commandPool = CommandPool::GetNext(familyId);
        m_semaphore = Semaphore::Create(true, name + "_semaphore");
        m_submitions = 0;

        VkCommandBufferAllocateInfo cbai{};
        cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cbai.commandPool = m_commandPool->Handle();
        cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cbai.commandBufferCount = 1;

        VkCommandBuffer commandBuffer;
        PE_CHECK(vkAllocateCommandBuffers(RHII.GetDevice(), &cbai, &commandBuffer));
        m_handle = commandBuffer;

        m_name = name;

        Debug::SetObjectName(m_handle, ObjectType::CommandBuffer, name);
    }

    CommandBuffer::~CommandBuffer()
    {
        Semaphore::Destroy(m_semaphore);

        if (m_handle && m_commandPool)
        {
            VkCommandBuffer cmd = m_handle;
            vkFreeCommandBuffers(RHII.GetDevice(), m_commandPool->Handle(), 1, &cmd);
            m_handle = {};

            CommandPool::Return(m_commandPool);
            m_commandPool = nullptr;
        }
    }

    void CommandBuffer::Begin()
    {
        if (m_recording)
            PE_ERROR("CommandBuffer::Begin: CommandBuffer is already recording!");

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        beginInfo.pInheritanceInfo = nullptr;

        PE_CHECK(vkBeginCommandBuffer(m_handle, &beginInfo));
        m_recording = true;
    }

    void CommandBuffer::End()
    {
        if (!m_recording)
            PE_ERROR("CommandBuffer::End: CommandBuffer is not in recording state!");

        PE_CHECK(vkEndCommandBuffer(m_handle));
        m_recording = false;
    }

    void CommandBuffer::PipelineBarrier()
    {
    }

    void CommandBuffer::SetDepthBias(float constantFactor, float clamp, float slopeFactor)
    {
        vkCmdSetDepthBias(m_handle, constantFactor, clamp, slopeFactor);
    }

    void CommandBuffer::BlitImage(Image *src, Image *dst, ImageBlit *region, Filter filter)
    {
        dst->BlitImage(this, src, region, filter);
    }

    void CommandBuffer::BeginPass(RenderPass *pass, FrameBuffer *frameBuffer)
    {
        std::vector<VkClearValue> clearValues(pass->attachments.size());
        for (int i = 0; i < pass->attachments.size(); i++)
        {
            if (IsDepthFormat(pass->attachments[i].format))
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

        auto dynamicOffsets = Descriptor::GetAllFrameDynamicOffsets(count, descriptors);

        vkCmdBindDescriptorSets(m_handle,
                                VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipeline->layout,
                                0, count, dsets.data(),
                                static_cast<uint32_t>(dynamicOffsets.size()), dynamicOffsets.data());
    }

    void CommandBuffer::BindComputeDescriptors(Pipeline *pipeline, uint32_t count, Descriptor **descriptors)
    {
        std::vector<VkDescriptorSet> dsets(count);
        for (uint32_t i = 0; i < count; i++)
            dsets[i] = descriptors[i]->Handle();
            
        auto dynamicOffsets = Descriptor::GetAllFrameDynamicOffsets(count, descriptors);

        vkCmdBindDescriptorSets(m_handle,
                                VK_PIPELINE_BIND_POINT_COMPUTE,
                                pipeline->layout,
                                0, count, dsets.data(),
                                static_cast<uint32_t>(dynamicOffsets.size()), dynamicOffsets.data());
    }

    void CommandBuffer::Dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
    {
        vkCmdDispatch(m_handle, groupCountX, groupCountY, groupCountZ);
    }

    void CommandBuffer::PushConstants(Pipeline *pipeline, ShaderStageFlags stage, uint32_t offset, uint32_t size, const void *pValues)
    {
        uint32_t flags = GetShaderStageVK(stage);
        vkCmdPushConstants(m_handle, pipeline->layout, flags, offset, size, pValues);
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
                               uint32_t signalSemaphoresCount, Semaphore **signalSemaphores)
    {
        CommandBuffer *cmd = this;
        queue->Submit(1, &cmd,
                      waitStages,
                      waitSemaphoresCount, waitSemaphores,
                      signalSemaphoresCount, signalSemaphores);
    }

    void CommandBuffer::ImageBarrier(Image *image,
                                     ImageLayout newLayout,
                                     uint32_t baseArrayLayer,
                                     uint32_t arrayLayers,
                                     uint32_t baseMipLevel,
                                     uint32_t mipLevels)
    {
        image->Barrier(this, newLayout, baseArrayLayer, arrayLayers, baseMipLevel, mipLevels);
    }

    void CommandBuffer::CopyBuffer(Buffer *src, Buffer *dst, const size_t size, size_t srcOffset, size_t dstOffset)
    {
        dst->CopyBuffer(this, src, size, srcOffset, dstOffset);
    }

    void CommandBuffer::CopyBufferStaged(Buffer *buffer, void *data, size_t size, size_t dtsOffset)
    {
        buffer->CopyBufferStaged(this, data, size, dtsOffset);
    }

    void CommandBuffer::CopyDataToImageStaged(Image *image,
                                              void *data,
                                              size_t size,
                                              uint32_t baseArrayLayer,
                                              uint32_t layerCount,
                                              uint32_t mipLevel)
    {
        image->CopyDataToImageStaged(this, data, size, baseArrayLayer, layerCount, mipLevel);
    }

    void CommandBuffer::CopyImage(Image *src, Image *dst)
    {
        dst->CopyImage(this, src);
    }

    void CommandBuffer::GenerateMipMaps(Image *image)
    {
        image->GenerateMipMaps(this);
    }

    void CommandBuffer::BeginDebugRegion(const std::string &name)
    {
        Debug::BeginCmdRegion(this, name);
    }

    void CommandBuffer::InsertDebugLabel(const std::string &name)
    {
        Debug::InsertCmdLabel(this, name);
    }

    void CommandBuffer::EndDebugRegion()
    {
        Debug::EndCmdRegion(this);
    }

    void CommandBuffer::Init(GpuHandle gpu, uint32_t countPerFamily)
    {
        uint32_t queueFamPropCount;
        vkGetPhysicalDeviceQueueFamilyProperties(gpu, &queueFamPropCount, nullptr);

        // CommandPools are creating CommandBuffers from a specific family id, this will have to
        // match with the Queue created from a falily id that this CommandBuffer will be submited to.
        s_allCmds = std::vector<std::map<size_t, CommandBuffer *>>(queueFamPropCount);
        s_availableCmds = std::vector<std::unordered_map<size_t, CommandBuffer *>>(queueFamPropCount);
        for (uint32_t i = 0; i < queueFamPropCount; i++)
        {
            s_allCmds[i] = std::map<size_t, CommandBuffer *>();
            s_availableCmds[i] = std::unordered_map<size_t, CommandBuffer *>();
            for (uint32_t j = 0; j < countPerFamily; j++)
            {
                std::string name = "CommandBuffer_" + std::to_string(i) + "_" + std::to_string(j);
                CommandBuffer *cmd = CommandBuffer::Create(i, name);
                s_allCmds[i][cmd->m_id] = cmd;
                s_availableCmds[i][cmd->m_id] = cmd;
            }
        }
    }

    void CommandBuffer::Clear()
    {
        for (auto &cmds : s_allCmds)
        {
            for (auto cmd : cmds)
                CommandBuffer::Destroy(cmd.second);
        }

        s_availableCmds.clear();
    }

    CommandBuffer *CommandBuffer::GetNext(uint32_t familyId)
    {
        std::lock_guard<std::mutex> lock(s_getNextMutex);

        if (s_availableCmds[familyId].empty())
        {
            std::string name = "CommandBuffer_" + std::to_string(familyId) + "_" + std::to_string(s_allCmds[familyId].size());
            CommandBuffer *cmd = CommandBuffer::Create(familyId, name);
            s_allCmds[familyId][cmd->m_id] = cmd;
            return cmd;
        }

        CommandBuffer *cmd = s_availableCmds[familyId].begin()->second;
        s_availableCmds[familyId].erase(cmd->m_id);

        return cmd;
    }

    void CommandBuffer::Return(CommandBuffer *cmd)
    {
        std::lock_guard<std::mutex> lock(s_returnMutex);

        // CHECKS
        // -------------------------------------------------------------
        if (!cmd || !cmd->m_handle)
            PE_ERROR("CommandBuffer::Return: CommandBuffer is null or invalid");

        if (s_allCmds[cmd->GetFamilyId()].find(cmd->m_id) == s_allCmds[cmd->m_familyId].end())
            PE_ERROR("CommandBuffer::Return: CommandBuffer does not belong to pool");

        if (cmd->m_recording)
            PE_ERROR("CommandBuffer::Return: " + cmd->m_name + " is still recording!");

        if (cmd->m_semaphore->GetValue() != cmd->m_submitions)
            PE_ERROR("CommandBuffer::Return: " + cmd->m_name + " is not finished!");
        //--------------------------------------------------------------

        s_availableCmds[cmd->GetFamilyId()][cmd->m_id] = cmd;
    }

    void CommandBuffer::Wait()
    {
        std::lock_guard<std::mutex> lock(s_WaitMutex);

        if (m_recording)
            PE_ERROR("CommandBuffer::Wait: " + m_name + " is still recording!");

        m_semaphore->Wait(m_submitions);

        if (!m_afterWaitCallbacks.IsEmpty())
        {
            m_afterWaitCallbacks.ReverseInvoke();
            m_afterWaitCallbacks.Clear();
        }
    }

    void CommandBuffer::AddAfterWaitCallback(Delegate<>::Func_type &&func)
    {
        m_afterWaitCallbacks += std::forward<Delegate<>::Func_type>(func);
    }
}
#endif
