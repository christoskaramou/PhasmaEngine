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

    size_t CommandBuffer::GetHash(uint32_t count, AttachmentInfo *colorInfos, AttachmentInfo *depthInfo)
    {
        Hash hash;
        hash.Combine(count);
        for (uint32_t i = 0; i < count; i++)
        {
            hash.Combine(reinterpret_cast<std::intptr_t>(colorInfos[i].image));
            hash.Combine(static_cast<uint32_t>(colorInfos[i].loadOp));
            hash.Combine(static_cast<uint32_t>(colorInfos[i].storeOp));
        }
        if (depthInfo)
        {
            hash.Combine(reinterpret_cast<std::intptr_t>(depthInfo->image));
            hash.Combine(static_cast<uint32_t>(depthInfo->loadOp));
            hash.Combine(static_cast<uint32_t>(depthInfo->storeOp));
        }

        return hash;
    }

    RenderPass *CommandBuffer::GetRenderPass(size_t hash,
                                             uint32_t count,
                                             AttachmentInfo *colorInfos,
                                             AttachmentInfo *depthInfo)
    {
        if (s_renderPasses.find(hash) == s_renderPasses.end())
        {
            std::vector<Attachment> attachements{};
            for (uint32_t i = 0; i < count; i++)
            {
                Attachment attachement{};
                attachement.format = colorInfos[i].image->imageInfo.format;
                attachement.samples = colorInfos[i].image->imageInfo.samples;
                attachement.loadOp = colorInfos[i].loadOp;
                attachement.storeOp = colorInfos[i].storeOp;
                attachement.finalLayout = colorInfos[i].image->GetLayout(0, 0);
                attachements.push_back(attachement);
            }

            if (depthInfo)
            {
                Attachment attachement{};
                attachement.format = depthInfo->image->imageInfo.format;
                attachement.samples = depthInfo->image->imageInfo.samples;
                attachement.loadOp = depthInfo->loadOp;
                attachement.storeOp = depthInfo->storeOp;
                attachement.finalLayout = depthInfo->image->GetLayout(0, 0);
                attachements.push_back(attachement);
            }

            std::string name = "Auto_Gen_RenderPass_" + std::to_string(s_renderPasses.size());
            s_renderPasses[hash] = RenderPass::Create(static_cast<uint32_t>(attachements.size()), attachements.data(), name);
        }

        return s_renderPasses[hash];
    }

    FrameBuffer *CommandBuffer::GetFrameBuffer(size_t hash,
                                               uint32_t count,
                                               AttachmentInfo *colorInfos,
                                               AttachmentInfo *depthInfo,
                                               RenderPass *renderPass)
    {
        if (s_frameBuffers.find(hash) == s_frameBuffers.end())
        {
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

            std::vector<ImageViewHandle> views{};
            for (uint32_t i = 0; i < count; i++)
                views.push_back(colorInfos[i].image->view);

            if (depthInfo)
                views.push_back(depthInfo->image->view);

            std::string name = "Auto_Gen_FrameBuffer_" + std::to_string(s_frameBuffers.size());
            s_frameBuffers[hash] = FrameBuffer::Create(width,
                                                       height,
                                                       static_cast<uint32_t>(views.size()),
                                                       views.data(),
                                                       renderPass,
                                                       name);
        }

        return s_frameBuffers[hash];
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
        rpi.renderArea.extent = VkExtent2D{frameBuffer->GetWidth(), frameBuffer->GetHeight()};
        rpi.clearValueCount = static_cast<uint32_t>(clearValues.size());
        rpi.pClearValues = clearValues.data();

        vkCmdBeginRenderPass(m_handle, &rpi, VK_SUBPASS_CONTENTS_INLINE);
    }

    void CommandBuffer::BeginPass(uint32_t count,
                                  AttachmentInfo *colorInfos,
                                  AttachmentInfo *depthInfo,
                                  RenderPass **outRenderPass)
    {
#if (USE_DYNAMIC_RENDERING == 0)
        Hash hash = GetHash(count, colorInfos, depthInfo);
        RenderPass *rp = GetRenderPass(hash, count, colorInfos, depthInfo);
        FrameBuffer *fb = GetFrameBuffer(hash, count, colorInfos, depthInfo, rp);
        
        *outRenderPass = rp;

        BeginPass(rp, fb);
#else
        m_dynamicPass = true;

        std::vector<VkRenderingAttachmentInfo> vkColorInfos(count);
        VkRenderingAttachmentInfo vkDepthInfo{};

        for (uint32_t i = 0; i < count; i++)
        {
            VkRenderingAttachmentInfo vkColorInfo{};
            vkColorInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            vkColorInfo.imageView = colorInfos[i].image->view;
            vkColorInfo.imageLayout = Translate<VkImageLayout>(colorInfos[i].image->GetLayout());
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
            vkDepthInfo.imageLayout = Translate<VkImageLayout>(image.GetLayout());
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
            vkCmdEndRenderPass(m_handle);
    }

    Pipeline *CommandBuffer::GetPipeline(const PipelineCreateInfo &pipelineInfo)
    {
        Hash hash;

        if (pipelineInfo.pVertShader)
            hash.Combine(pipelineInfo.pVertShader->GetCache().GetHash());

        if (pipelineInfo.pFragShader)
            hash.Combine(pipelineInfo.pFragShader->GetCache().GetHash());

        if (pipelineInfo.pCompShader)
            hash.Combine(pipelineInfo.pCompShader->GetCache().GetHash());

        for (auto &binding : pipelineInfo.vertexInputBindingDescriptions)
        {
            hash.Combine(binding.binding);
            hash.Combine(binding.stride);
            hash.Combine(static_cast<int>(binding.inputRate));
        }

        for (auto &attribute : pipelineInfo.vertexInputAttributeDescriptions)
        {
            hash.Combine(attribute.location);
            hash.Combine(attribute.binding);
            hash.Combine(static_cast<int>(attribute.format));
            hash.Combine(attribute.offset);
        }

        hash.Combine(pipelineInfo.width);
        hash.Combine(pipelineInfo.height);
        hash.Combine(static_cast<int>(pipelineInfo.cullMode));

        for (auto &attachment : pipelineInfo.colorBlendAttachments)
        {
            hash.Combine(attachment.blendEnable);
            hash.Combine(static_cast<int>(attachment.srcColorBlendFactor));
            hash.Combine(static_cast<int>(attachment.dstColorBlendFactor));
            hash.Combine(static_cast<int>(attachment.colorBlendOp));
            hash.Combine(static_cast<int>(attachment.srcAlphaBlendFactor));
            hash.Combine(static_cast<int>(attachment.dstAlphaBlendFactor));
            hash.Combine(static_cast<int>(attachment.alphaBlendOp));
            hash.Combine(attachment.colorWriteMask.Value());
        }

        for (auto &dynamic : pipelineInfo.dynamicStates)
            hash.Combine(static_cast<int>(dynamic));

        hash.Combine(pipelineInfo.pushConstantStage.Value());
        hash.Combine(pipelineInfo.pushConstantSize);

        for (auto &layout : pipelineInfo.descriptorSetLayouts)
            hash.Combine(reinterpret_cast<intptr_t>(layout));

        if (pipelineInfo.renderPass)
            hash.Combine(reinterpret_cast<intptr_t>(pipelineInfo.renderPass));

        hash.Combine(pipelineInfo.dynamicColorTargets);
        for (uint32_t i = 0; i < pipelineInfo.dynamicColorTargets; i++)
            hash.Combine(static_cast<int>(pipelineInfo.colorFormats[i]));
        if (pipelineInfo.depthFormat)
            hash.Combine(static_cast<int>(*pipelineInfo.depthFormat));

        hash.Combine(reinterpret_cast<intptr_t>(pipelineInfo.pipelineCache.Get()));

        if (s_pipelines.find(hash) == s_pipelines.end())
            s_pipelines[hash] = Pipeline::Create(pipelineInfo);

        return s_pipelines[hash];
    }

    void CommandBuffer::BindPipeline(Pipeline *pipeline)
    {
        vkCmdBindPipeline(m_handle, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->Handle());
    }

    void CommandBuffer::BindPipeline(const PipelineCreateInfo &pipelineInfo, Pipeline **outPipeline)
    {
        Pipeline *pipeline = GetPipeline(pipelineInfo);
        *outPipeline = pipeline;
        BindPipeline(pipeline);
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

    void CommandBuffer::PushConstants(Pipeline *pipeline, ShaderStageFlags stage, uint32_t offset, uint32_t size, const void *pValues)
    {
        auto flags = Translate<VkShaderStageFlags>(stage);
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
