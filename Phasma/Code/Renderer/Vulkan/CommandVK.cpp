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
    CommandPool::CommandPool(uint32_t familyId, CommandPoolCreateFlags flags, const std::string &name)
    {
        m_familyId = familyId;
        m_flags = flags;

        VkCommandPoolCreateInfo cpci{};
        cpci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        cpci.queueFamilyIndex = familyId;
        cpci.flags = Translate<VkCommandPoolCreateFlags>(flags);

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
            CommandPoolCreateFlags flags = CommandPoolCreate::TransientBit; // | CommandPoolCreate::ResetCommandBuffer;
            CommandPool *cp = CommandPool::Create(familyId, flags, "CommandPool_" + std::to_string(familyId) + "_" + std::to_string(s_allCps[familyId].size()));
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
        m_boundPipeline = nullptr;

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

        Reset();

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

    void CommandBuffer::Reset()
    {
        if (m_commandPool->GetFlags() & CommandPoolCreate::ResetCommandBuffer)
            vkResetCommandBuffer(m_handle, 0);
        else
            vkResetCommandPool(RHII.GetDevice(), m_commandPool->Handle(), 0);
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

    RenderPass *CommandBuffer::GetRenderPass(uint32_t count, AttachmentInfo *colorInfos, AttachmentInfo *depthInfo)
    {
        Hash hash;
        hash.Combine(count);
        for (uint32_t i = 0; i < count; i++)
        {
            hash.Combine(static_cast<uint32_t>(colorInfos[i].image->imageInfo.format));
            hash.Combine(static_cast<uint32_t>(colorInfos[i].image->imageInfo.samples));
            hash.Combine(static_cast<uint32_t>(colorInfos[i].loadOp));
            hash.Combine(static_cast<uint32_t>(colorInfos[i].storeOp));
            hash.Combine(static_cast<uint32_t>(colorInfos[i].initialLayout));
            hash.Combine(static_cast<uint32_t>(colorInfos[i].finalLayout));
        }
        if (depthInfo)
        {
            hash.Combine(static_cast<uint32_t>(depthInfo->image->imageInfo.format));
            hash.Combine(static_cast<uint32_t>(depthInfo->image->imageInfo.samples));
            hash.Combine(static_cast<uint32_t>(depthInfo->loadOp));
            hash.Combine(static_cast<uint32_t>(depthInfo->storeOp));
            hash.Combine(static_cast<uint32_t>(depthInfo->initialLayout));
            hash.Combine(static_cast<uint32_t>(depthInfo->finalLayout));
        }

        auto it = s_renderPasses.find(hash);
        if (it != s_renderPasses.end())
        {
            return it->second;
        }
        else
        {
            std::vector<Attachment> attachements{};
            for (uint32_t i = 0; i < count; i++)
            {
                Attachment attachement{};
                attachement.format = colorInfos[i].image->imageInfo.format;
                attachement.samples = colorInfos[i].image->imageInfo.samples;
                attachement.loadOp = colorInfos[i].loadOp;
                attachement.storeOp = colorInfos[i].storeOp;
                attachement.initialLayout = colorInfos[i].initialLayout;
                attachement.finalLayout = colorInfos[i].finalLayout;
                attachements.push_back(attachement);
            }

            if (depthInfo)
            {
                Attachment attachement{};
                attachement.format = depthInfo->image->imageInfo.format;
                attachement.samples = depthInfo->image->imageInfo.samples;
                attachement.loadOp = depthInfo->loadOp;
                attachement.storeOp = depthInfo->storeOp;
                attachement.initialLayout = depthInfo->initialLayout;
                attachement.finalLayout = depthInfo->finalLayout;
                attachements.push_back(attachement);
            }

            std::string name = "Auto_Gen_RenderPass_" + std::to_string(s_renderPasses.size());
            RenderPass *newRenderPass = RenderPass::Create(static_cast<uint32_t>(attachements.size()), attachements.data(), name);
            s_renderPasses[hash] = newRenderPass;

            return newRenderPass;
        }
    }

    FrameBuffer *CommandBuffer::GetFrameBuffer(RenderPass *renderPass, Image **colorTargets, Image *depthTarget)
    {
        uint32_t count = static_cast<uint32_t>(renderPass->attachments.size());

        Hash hash;
        hash.Combine(count);
        hash.Combine(reinterpret_cast<std::intptr_t>(renderPass));

        uint32_t colorCount = depthTarget ? count - 1 : count;
        for (uint32_t i = 0; i < colorCount; i++)
            hash.Combine(reinterpret_cast<std::intptr_t>(colorTargets[i]));
        if (depthTarget)
            hash.Combine(reinterpret_cast<std::intptr_t>(depthTarget));

        auto it = s_frameBuffers.find(hash);
        if (it != s_frameBuffers.end())
        {
            return it->second;
        }
        else
        {
            uint32_t width;
            uint32_t height;

            // if no color attachments pick depth
            if (colorCount > 0)
            {
                width = colorTargets[0]->imageInfo.width;
                height = colorTargets[0]->imageInfo.height;
            }
            else
            {
                width = depthTarget->imageInfo.width;
                height = depthTarget->imageInfo.height;
            }

            std::vector<ImageViewHandle> views{};
            for (uint32_t i = 0; i < colorCount; i++)
            {
                // create render target view if not exists
                if (!colorTargets[i]->HasRTV())
                    colorTargets[i]->CreateRTV();
                views.push_back(colorTargets[i]->GetRTV());
            }

            if (depthTarget)
            {
                // create render target view if not exists
                if (!depthTarget->HasRTV())
                    depthTarget->CreateRTV();
                views.push_back(depthTarget->GetRTV());
            }

            std::string name = "Auto_Gen_FrameBuffer_" + std::to_string(s_frameBuffers.size());
            FrameBuffer *newFrameBuffer = FrameBuffer::Create(width,
                                                              height,
                                                              static_cast<uint32_t>(views.size()),
                                                              views.data(),
                                                              renderPass,
                                                              name);
            s_frameBuffers[hash] = newFrameBuffer;

            return newFrameBuffer;
        }
    }

    void CommandBuffer::BeginPass(RenderPass *renderPass, Image **colorTargets, Image *depthTarget)
    {
#if (USE_DYNAMIC_RENDERING == 0)
        FrameBuffer *frameBuffer = GetFrameBuffer(renderPass, colorTargets, depthTarget);

        std::vector<Attachment> &attachments = renderPass->attachments;
        uint32_t count = static_cast<uint32_t>(attachments.size());

        std::vector<VkClearValue> clearValues(count);

        uint32_t colorCount = depthTarget ? count - 1 : count;
        uint32_t i = 0;
        for (; i < colorCount; i++)
        {
            colorTargets[i]->SetCurrentLayout(attachments[i].finalLayout);
            clearValues[i].color = {1.0f, 0.0f, 0.0f, 1.0f};
        }
        if (depthTarget)
        {
            depthTarget->SetCurrentLayout(attachments[i].finalLayout);
            clearValues[i].depthStencil = {GlobalSettings::ReverseZ ? 0.f : 1.f, 0};
        }

        VkRenderPassBeginInfo rpi{};
        rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpi.renderPass = renderPass->Handle();
        rpi.framebuffer = frameBuffer->Handle();
        rpi.renderArea.offset = VkOffset2D{0, 0};
        rpi.renderArea.extent = VkExtent2D{frameBuffer->GetWidth(), frameBuffer->GetHeight()};
        rpi.clearValueCount = static_cast<uint32_t>(clearValues.size());
        rpi.pClearValues = clearValues.data();

        vkCmdBeginRenderPass(m_handle, &rpi, VK_SUBPASS_CONTENTS_INLINE);
#else
        m_dynamicPass = true;

        std::vector<VkRenderingAttachmentInfo> vkColorInfos(count);
        VkRenderingAttachmentInfo vkDepthInfo{};

        for (uint32_t i = 0; i < count; i++)
        {
            VkRenderingAttachmentInfo vkColorInfo{};
            vkColorInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            vkColorInfo.imageView = colorInfos[i].image->view;
            vkColorInfo.imageLayout = Translate<VkImageLayout>(colorInfos[i].image->GetCurrentLayout());
            vkColorInfo.loadOp = Translate<VkAttachmentLoadOp>(colorInfos[i].loadOp);
            vkColorInfo.storeOp = Translate<VkAttachmentStoreOp>(colorInfos[i].storeOp);
            vkColorInfo.clearValue.color = {0.0f, 0.0f, 0.0f, 0.0f};
            vkColorInfos[i] = vkColorInfo;
        }

        if (depthInfo)
        {
            Image &image = *depthInfo->image;
            vkDepthInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            vkDepthInfo.imageView = image.view;
            vkDepthInfo.imageLayout = Translate<VkImageLayout>(image.GetCurrentLayout());
            vkDepthInfo.loadOp = Translate<VkAttachmentLoadOp>(depthInfo->loadOp);
            vkDepthInfo.storeOp = Translate<VkAttachmentStoreOp>(depthInfo->storeOp);
            vkDepthInfo.clearValue.depthStencil = {GlobalSettings::ReverseZ ? 0.f : 1.f, 0};
        }

        uint32_t width;
        uint32_t height;
        if (count > 0)
        {
            width = colorInfos[0].image->imageInfo.width;
            height = colorInfos[0].image->imageInfo.height;
        }
        else
        {
            width = depthInfo->image->imageInfo.width;
            height = depthInfo->image->imageInfo.height;
        }

        VkRenderingInfo renderingInfo{};
        renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        renderingInfo.renderArea = {0, 0, width, height};
        renderingInfo.layerCount = 1;
        renderingInfo.colorAttachmentCount = static_cast<uint32_t>(vkColorInfos.size());
        renderingInfo.pColorAttachments = vkColorInfos.data();
        renderingInfo.pDepthAttachment = depthInfo ? &vkDepthInfo : nullptr;
        renderingInfo.pStencilAttachment = depthInfo ? &vkDepthInfo : nullptr;

        vkCmdBeginRendering(m_handle, &renderingInfo);
#endif
    }

    void CommandBuffer::EndPass()
    {
        if (m_dynamicPass)
        {
            vkCmdEndRendering(m_handle);
            m_dynamicPass = false;
        }
        else
        {
            vkCmdEndRenderPass(m_handle);
        }

        m_boundPipeline = nullptr;
    }

    Pipeline *CommandBuffer::GetPipeline(const PassInfo &passInfo)
    {
        auto it = s_pipelines.find(passInfo.GetHash()); // PassInfo is hashable
        if (it != s_pipelines.end())
        {
            return it->second;
        }
        else
        {
            Pipeline *newPipeline = Pipeline::Create(passInfo);
            s_pipelines[passInfo.GetHash()] = newPipeline;

            return newPipeline;
        }
    }

    void CommandBuffer::BindGraphicsPipeline(Pipeline *pipeline)
    {
        vkCmdBindPipeline(m_handle, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->Handle());
    }

    void CommandBuffer::BindComputePipeline(Pipeline *pipeline)
    {
        vkCmdBindPipeline(m_handle, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->Handle());
    }

    void CommandBuffer::BindPipeline(const PassInfo &passInfo)
    {
        m_boundPipeline = GetPipeline(passInfo);

        if (passInfo.pCompShader)
            BindComputePipeline(m_boundPipeline);
        else
            BindGraphicsPipeline(m_boundPipeline);
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

    void CommandBuffer::BindDescriptors(uint32_t count, Descriptor **descriptors)
    {
        PE_ERROR_IF(!m_boundPipeline, "CommandBuffer::BindDescriptors: No bound pipeline found!");
        std::vector<VkDescriptorSet> dsets(count);
        for (uint32_t i = 0; i < count; i++)
            dsets[i] = descriptors[i]->Handle();

        auto dynamicOffsets = Descriptor::GetAllFrameDynamicOffsets(count, descriptors);

        vkCmdBindDescriptorSets(m_handle,
                                VK_PIPELINE_BIND_POINT_GRAPHICS,
                                m_boundPipeline->layout,
                                0, count, dsets.data(),
                                static_cast<uint32_t>(dynamicOffsets.size()), dynamicOffsets.data());
    }

    void CommandBuffer::BindComputeDescriptors(uint32_t count, Descriptor **descriptors)
    {
        PE_ERROR_IF(!m_boundPipeline, "CommandBuffer::BindComputeDescriptors: No bound pipeline found!");
        std::vector<VkDescriptorSet> dsets(count);
        for (uint32_t i = 0; i < count; i++)
            dsets[i] = descriptors[i]->Handle();

        auto dynamicOffsets = Descriptor::GetAllFrameDynamicOffsets(count, descriptors);

        vkCmdBindDescriptorSets(m_handle,
                                VK_PIPELINE_BIND_POINT_COMPUTE,
                                m_boundPipeline->layout,
                                0, count, dsets.data(),
                                static_cast<uint32_t>(dynamicOffsets.size()), dynamicOffsets.data());
    }

    void CommandBuffer::SetViewport(float x, float y, float width, float height)
    {
        VkViewport viewport{};
        viewport.x = x;
        viewport.y = y;
        viewport.width = width;
        viewport.height = height;
        viewport.minDepth = 0.f; // GlobalSettings::ReverseZ ? 1.f : 0.f;
        viewport.maxDepth = 1.f; // GlobalSettings::ReverseZ ? 0.f : 1.f;

        vkCmdSetViewport(m_handle, 0, 1, &viewport);
    }

    void CommandBuffer::SetScissor(int x, int y, uint32_t width, uint32_t height)
    {
        VkRect2D scissor{};
        scissor.offset = {x, y};
        scissor.extent = {width, height};

        vkCmdSetScissor(m_handle, 0, 1, &scissor);
    }

    void CommandBuffer::Dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
    {
        vkCmdDispatch(m_handle, groupCountX, groupCountY, groupCountZ);
    }

    void CommandBuffer::PushConstants(ShaderStageFlags stage, uint32_t offset, uint32_t size, const void *pValues)
    {
        PE_ERROR_IF(!m_boundPipeline, "CommandBuffer::PushConstants: No bound pipeline found!");
        auto flags = Translate<VkShaderStageFlags>(stage);
        vkCmdPushConstants(m_handle, m_boundPipeline->layout, flags, offset, size, pValues);
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
