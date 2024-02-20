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
#include "Renderer/Event.h"

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

    CommandPool *CommandPool::GetFree(uint32_t familyId)
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
        m_commandPool = CommandPool::GetFree(familyId);
        m_semaphore = Semaphore::Create(true, name + "_semaphore");
        m_event = Event::Create(name + "_event");
        m_submitions = 0;
        m_renderPass = nullptr;
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
        m_commandFlags = CommandType::None;
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

    void CommandBuffer::SetDepthBias(float constantFactor, float clamp, float slopeFactor)
    {
        vkCmdSetDepthBias(m_handle, constantFactor, clamp, slopeFactor);
    }

    void CommandBuffer::BlitImage(Image *src, Image *dst, ImageBlit *region, Filter filter)
    {
        dst->BlitImage(this, src, region, filter);
        m_commandFlags |= CommandType::TransferBit;
    }

    void CommandBuffer::ClearColors(std::vector<Image *> images)
    {
        std::vector<ImageBarrierInfo> barriers(images.size());
        for (uint32_t i = 0; i < images.size(); i++)
        {
            barriers[i].image = images[i];
            barriers[i].layout = ImageLayout::TransferDst;
            barriers[i].stageFlags = PipelineStage::TransferBit;
            barriers[i].accessMask = Access::TransferWriteBit;
        }
        Image::Barriers(this, barriers);

        for (uint32_t i = 0; i < images.size(); i++)
        {
            Image *image = images[i];
            const vec4 &color = image->m_imageInfo.clearColor;

            VkClearColorValue clearValue{};
            clearValue.float32[0] = color[0];
            clearValue.float32[1] = color[1];
            clearValue.float32[2] = color[2];
            clearValue.float32[3] = color[3];

            VkImageSubresourceRange rangeVK{};
            rangeVK.aspectMask = GetAspectMaskVK(image->GetFormat());
            rangeVK.baseMipLevel = 0;
            rangeVK.levelCount = 1;
            rangeVK.baseArrayLayer = 0;
            rangeVK.layerCount = 1;

            BeginDebugRegion("Clear Color: " + image->m_imageInfo.name);
            vkCmdClearColorImage(m_handle, image->Handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearValue, 1, &rangeVK);
            EndDebugRegion();

            m_commandFlags |= CommandType::TransferBit;
        }
    }

    void CommandBuffer::ClearDepthStencils(std::vector<Image *> images)
    {
        std::vector<ImageBarrierInfo> barriers(images.size());
        for (uint32_t i = 0; i < images.size(); i++)
        {
            barriers[i].image = images[i];
            barriers[i].layout = ImageLayout::TransferDst;
            barriers[i].stageFlags = PipelineStage::TransferBit;
            barriers[i].accessMask = Access::TransferWriteBit;
        }
        Image::Barriers(this, barriers);

        for (uint32_t i = 0; i < images.size(); i++)
        {
            Image *image = images[i];

            VkClearDepthStencilValue clearValue{};
            clearValue.depth = image->m_imageInfo.clearColor[0];
            clearValue.stencil = static_cast<uint32_t>(image->m_imageInfo.clearColor[1]);

            VkImageSubresourceRange rangeVK{};
            rangeVK.aspectMask = GetAspectMaskVK(image->GetFormat());
            rangeVK.baseMipLevel = 0;
            rangeVK.levelCount = 1;
            rangeVK.baseArrayLayer = 0;
            rangeVK.layerCount = 1;

            BeginDebugRegion("Clear Depth: " + image->m_imageInfo.name);
            vkCmdClearDepthStencilImage(m_handle, image->Handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearValue, 1, &rangeVK);
            EndDebugRegion();

            m_commandFlags |= CommandType::TransferBit;
        }
    }

    // Note: We don't care about the actual images, having the Attachment struct for convenience
    RenderPass *CommandBuffer::GetRenderPass(const std::vector<Attachment> &attachments)
    {
        Hash hash;
        hash.Combine(attachments.size());
        for (const auto &attachment : attachments)
        {
            hash.Combine(static_cast<uint32_t>(attachment.image->GetFormat()));
            hash.Combine(static_cast<uint32_t>(attachment.image->m_imageInfo.samples));
            hash.Combine(static_cast<uint32_t>(attachment.loadOp));
            hash.Combine(static_cast<uint32_t>(attachment.storeOp));
            hash.Combine(static_cast<uint32_t>(attachment.stencilLoadOp));
            hash.Combine(static_cast<uint32_t>(attachment.stencilStoreOp));
            hash.Combine(static_cast<uint32_t>(attachment.initialLayout));
            hash.Combine(static_cast<uint32_t>(attachment.finalLayout));
        }

        auto it = s_renderPasses.find(hash);
        if (it != s_renderPasses.end())
        {
            return it->second;
        }
        else
        {
            std::string name = "Auto_Gen_RenderPass_" + std::to_string(s_renderPasses.size());
            RenderPass *newRenderPass = RenderPass::Create(attachments, name);
            s_renderPasses[hash] = newRenderPass;

            return newRenderPass;
        }
    }

    // Note: Cares only about the actual images, just having the Attachment struct for convenience
    FrameBuffer *CommandBuffer::GetFrameBuffer(RenderPass *renderPass, const std::vector<Attachment> &attachments)
    {
        Hash hash;
        hash.Combine(reinterpret_cast<std::intptr_t>(renderPass));

        for (const auto &attachment : attachments)
            hash.Combine(reinterpret_cast<std::intptr_t>(attachment.image));

        auto it = s_frameBuffers.find(hash);
        if (it != s_frameBuffers.end())
        {
            return it->second;
        }
        else
        {
            std::vector<ImageViewHandle> views{};
            for (const auto &attachment : attachments)
            {
                Image *image = attachment.image;
                if (!image->HasRTV())
                    image->CreateRTV();
                views.push_back(image->GetRTV());
            }

            std::string name = "Auto_Gen_FrameBuffer_" + std::to_string(s_frameBuffers.size());
            FrameBuffer *newFrameBuffer = FrameBuffer::Create(attachments[0].image->m_imageInfo.width,
                                                              attachments[0].image->m_imageInfo.height,
                                                              static_cast<uint32_t>(views.size()),
                                                              views.data(),
                                                              renderPass,
                                                              name);
            s_frameBuffers[hash] = newFrameBuffer;

            return newFrameBuffer;
        }
    }

    void CommandBuffer::BeginPass(const std::vector<Attachment> &attachments, const std::string &name)
    {
        m_passActive = true;
        m_renderPass = GetRenderPass(attachments);
        m_frameBuffer = GetFrameBuffer(m_renderPass, attachments);
        std::vector<VkClearValue> clearValues(attachments.size());
        uint32_t clearOps = 0;

        for (const auto &attachment : attachments)
        {
            Image *renderTarget = attachment.image;
            Format format = renderTarget->GetFormat();
            if (IsDepthFormat(format) && (attachment.loadOp == AttachmentLoadOp::Clear || attachment.stencilLoadOp == AttachmentLoadOp::Clear))
            {
                const float clearDepth = renderTarget->m_imageInfo.clearColor[0];
                uint32_t clearStencil = static_cast<uint32_t>(renderTarget->m_imageInfo.clearColor[1]);
                clearValues[clearOps].depthStencil = {clearDepth, clearStencil};
                clearOps++;
            }
            else if (attachment.loadOp == AttachmentLoadOp::Clear)
            {
                const vec4 &clearColor = renderTarget->m_imageInfo.clearColor;
                clearValues[clearOps].color = {clearColor[0], clearColor[1], clearColor[2], clearColor[3]};
                clearOps++;
            }

            ImageBarrierInfo info{};
            info.layout = attachment.finalLayout;
            info.stageFlags = PipelineStage::AllGraphicsBit;
            info.accessMask = Access::None;
            renderTarget->SetCurrentInfo(info);
        }

        VkRenderPassBeginInfo rpi{};
        rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpi.renderPass = m_renderPass->Handle();
        rpi.framebuffer = m_frameBuffer->Handle();
        rpi.renderArea.offset = VkOffset2D{0, 0};
        rpi.renderArea.extent = VkExtent2D{m_frameBuffer->GetWidth(), m_frameBuffer->GetHeight()};
        rpi.clearValueCount = clearOps > 0 ? static_cast<uint32_t>(clearValues.size()) : 0;
        rpi.pClearValues = clearOps > 0 ? clearValues.data() : nullptr;

        BeginDebugRegion(name + "_pass");
        vkCmdBeginRenderPass(m_handle, &rpi, VK_SUBPASS_CONTENTS_INLINE);
        m_commandFlags |= CommandType::GraphicsBit;
    }

    void CommandBuffer::EndPass()
    {
        vkCmdEndRenderPass(m_handle);
        EndDebugRegion();

        m_renderPass = nullptr;
        m_frameBuffer = nullptr;
        m_boundPipeline = nullptr;
        m_boundVertexBuffer = nullptr;
        m_boundVertexBufferOffset = -1;
        m_boundVertexBufferFirstBinding = UINT32_MAX;
        m_boundVertexBufferBindingCount = UINT32_MAX;
        m_boundIndexBuffer = nullptr;
        m_boundIndexBufferOffset = -1;
        m_passActive = false;
    }

    Pipeline *CommandBuffer::GetPipeline(RenderPass *renderPass, PassInfo &passInfo)
    {
        Hash hash;
        hash.Combine(reinterpret_cast<std::intptr_t>(renderPass));
        hash.Combine(passInfo.GetHash());

        auto it = s_pipelines.find(hash); // PassInfo is hashable
        if (it != s_pipelines.end())
        {
            return it->second;
        }
        else
        {
            Pipeline *newPipeline = Pipeline::Create(renderPass, passInfo);
            s_pipelines[hash] = newPipeline;

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

    void CommandBuffer::BindPipeline(PassInfo &passInfo, bool bindDescriptors)
    {
        Pipeline *pipeline = GetPipeline(m_renderPass, passInfo);
        if (pipeline == m_boundPipeline)
            return;

        m_boundPipeline = pipeline;

        if (passInfo.pCompShader)
        {
            BindComputePipeline(m_boundPipeline);
        }
        else
        {
            BindGraphicsPipeline(m_boundPipeline);
        }

        if (bindDescriptors)
        {
            // auto bind descriptor sets
            BindDescriptors(passInfo.GetDescriptors(RHII.GetFrameIndex()));
        }
    }

    void CommandBuffer::BindVertexBuffer(Buffer *buffer, size_t offset, uint32_t firstBinding, uint32_t bindingCount)
    {
        if (m_boundVertexBuffer == buffer &&
            m_boundVertexBufferOffset == offset &&
            m_boundVertexBufferFirstBinding == firstBinding &&
            m_boundVertexBufferBindingCount == bindingCount)
            return;

        m_boundVertexBuffer = buffer;
        m_boundVertexBufferOffset = offset;
        m_boundVertexBufferFirstBinding = firstBinding;
        m_boundVertexBufferBindingCount = bindingCount;

        VkBuffer buff = buffer->Handle();
        vkCmdBindVertexBuffers(m_handle, firstBinding, bindingCount, &buff, &offset);
    }

    void CommandBuffer::BindIndexBuffer(Buffer *buffer, size_t offset)
    {
        if (m_boundIndexBuffer == buffer && m_boundIndexBufferOffset == offset)
            return;

        m_boundIndexBuffer = buffer;
        vkCmdBindIndexBuffer(m_handle, buffer->Handle(), offset, VK_INDEX_TYPE_UINT32);
    }

    void CommandBuffer::BindDescriptors(uint32_t count, Descriptor **descriptors)
    {
        PE_ERROR_IF(!m_boundPipeline, "CommandBuffer::BindDescriptors: No bound pipeline found!");

        if (count == 0)
            return;

        std::vector<VkDescriptorSet> dsets(count);
        for (uint32_t i = 0; i < count; i++)
            dsets[i] = descriptors[i]->Handle();

        // auto dynamicOffsets = Descriptor::GetAllFrameDynamicOffsets(count, descriptors);

        VkPipelineBindPoint pipelineBindPoint = m_boundPipeline->m_info.pCompShader ? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS;
        vkCmdBindDescriptorSets(m_handle,
                                pipelineBindPoint,
                                m_boundPipeline->m_layout,
                                0, count, dsets.data(),
                                0, nullptr); // static_cast<uint32_t>(dynamicOffsets.size()), dynamicOffsets.data());
    }

    void CommandBuffer::BindDescriptors(const std::vector<Descriptor *> &descriptors)
    {
        BindDescriptors(static_cast<uint32_t>(descriptors.size()), const_cast<Descriptor **>(descriptors.data()));
    }

    void CommandBuffer::PushDescriptor(uint32_t set, const std::vector<PushDescriptorInfo> &info)
    {
        PE_ERROR_IF(!m_boundPipeline, "CommandBuffer::PushDescriptor: No bound pipeline found!");

        std::vector<VkWriteDescriptorSet> writes{};
        std::vector<std::vector<VkDescriptorImageInfo>> imageInfoVK{};
        std::vector<std::vector<VkDescriptorBufferInfo>> bufferInfoVK{};
        imageInfoVK.resize(info.size());
        bufferInfoVK.resize(info.size());
        writes.resize(info.size());

        for (uint32_t i = 0; i < info.size(); i++)
        {
            writes[i] = {};
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstBinding = info[i].binding;
            writes[i].dstArrayElement = 0;
            writes[i].descriptorType = Translate<VkDescriptorType>(info[i].type);
            if (info[i].views.size() > 0)
            {
                imageInfoVK[i].resize(info[i].views.size());
                for (uint32_t j = 0; j < info[i].views.size(); j++)
                {
                    imageInfoVK[i][j] = {};
                    imageInfoVK[i][j].imageView = info[i].views[j];
                    imageInfoVK[i][j].imageLayout = Translate<VkImageLayout>(info[i].layouts[j]);
                    imageInfoVK[i][j].sampler = info[i].type == DescriptorType::CombinedImageSampler ? info[i].samplers[j] : SamplerHandle{};
                }

                writes[i].descriptorCount = static_cast<uint32_t>(imageInfoVK[i].size());
                writes[i].pImageInfo = imageInfoVK[i].data();
            }
            else if (info[i].buffers.size() > 0)
            {
                bufferInfoVK[i].resize(info[i].buffers.size());
                for (uint32_t j = 0; j < info[i].buffers.size(); j++)
                {
                    bufferInfoVK[i][j] = {};
                    bufferInfoVK[i][j].buffer = info[i].buffers[j]->Handle();
                    bufferInfoVK[i][j].offset = info[i].offsets.size() > 0 ? info[i].offsets[j] : 0;
                    bufferInfoVK[i][j].range = info[i].ranges.size() > 0 ? info[i].ranges[j] : VK_WHOLE_SIZE;
                }

                writes[i].descriptorCount = static_cast<uint32_t>(bufferInfoVK[i].size());
                writes[i].pBufferInfo = bufferInfoVK[i].data();
            }
            else if (info[i].samplers.size() > 0 && info[i].type != DescriptorType::CombinedImageSampler)
            {
                imageInfoVK[i].resize(info[i].samplers.size());
                for (uint32_t j = 0; j < info[i].samplers.size(); j++)
                {
                    imageInfoVK[i][j] = {};
                    imageInfoVK[i][j].imageView = nullptr;
                    imageInfoVK[i][j].imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                    imageInfoVK[i][j].sampler = info[i].samplers[j];
                }

                writes[i].descriptorCount = static_cast<uint32_t>(imageInfoVK[i].size());
                writes[i].pImageInfo = imageInfoVK[i].data();
            }
            else
            {
                PE_ERROR("CommandBuffer::PushDescriptor: No views or buffers or samplers found!");
            }
        }

        // vkCmdPushDescriptorSetKHR is part of VK_KHR_push_descriptor extension, has to be loaded
        static auto vkCmdPushDescriptorSetKHR = (PFN_vkCmdPushDescriptorSetKHR)vkGetDeviceProcAddr(RHII.GetDevice(), "vkCmdPushDescriptorSetKHR");
        PE_ERROR_IF(!vkCmdPushDescriptorSetKHR, "CommandBuffer::PushDescriptor: vkCmdPushDescriptorSetKHR not found!");

        vkCmdPushDescriptorSetKHR(m_handle,
                                  m_boundPipeline->m_info.pCompShader ? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  m_boundPipeline->m_layout,
                                  set,
                                  static_cast<uint32_t>(writes.size()),
                                  writes.data());
    }

    void CommandBuffer::SetViewport(float x, float y, float width, float height)
    {
        VkViewport viewport{};
        viewport.x = x;
        viewport.y = y;
        viewport.width = width;
        viewport.height = height;
        viewport.minDepth = 0.f;
        viewport.maxDepth = 1.f;

        vkCmdSetViewport(m_handle, 0, 1, &viewport);
    }

    void CommandBuffer::SetScissor(int x, int y, uint32_t width, uint32_t height)
    {
        VkRect2D scissor{};
        scissor.offset = {x, y};
        scissor.extent = {width, height};

        vkCmdSetScissor(m_handle, 0, 1, &scissor);
    }

    void CommandBuffer::SetLineWidth(float width)
    {
        vkCmdSetLineWidth(m_handle, width);
    }

    void CommandBuffer::SetDepthTestEnable(uint32_t enable)
    {
        vkCmdSetDepthTestEnable(m_handle, enable);
    }

    void CommandBuffer::SetDepthWriteEnable(uint32_t enable)
    {
        vkCmdSetDepthWriteEnable(m_handle, enable);
    }

    void CommandBuffer::Dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
    {
        vkCmdDispatch(m_handle, groupCountX, groupCountY, groupCountZ);
        m_commandFlags |= CommandType::ComputeBit;
    }

    void CommandBuffer::PushConstants()
    {
        PE_ERROR_IF(!m_boundPipeline, "CommandBuffer::PushConstants: No bound pipeline found!");

        const auto &stages = m_boundPipeline->m_info.m_pushConstantStages;
        const auto &offsets = m_boundPipeline->m_info.m_pushConstantOffsets;
        const auto &sizes = m_boundPipeline->m_info.m_pushConstantSizes;

        for (size_t i = 0; i < stages.size(); i++)
        {
            vkCmdPushConstants(m_handle,
                               m_boundPipeline->m_layout,
                               Translate<VkShaderStageFlags>(stages[i]),
                               offsets[i],
                               sizes[i],
                               m_pushConstants.Data());
        }
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
                               uint32_t waitSemaphoresCount,
                               PipelineStageFlags *waitStages, Semaphore **waitSemaphores,
                               uint32_t signalSemaphoresCount,
                               PipelineStageFlags *signalStages, Semaphore **signalSemaphores)
    {
        CommandBuffer *cmd = this;
        queue->Submit(1, &cmd,
                      waitSemaphoresCount, waitStages, waitSemaphores,
                      signalSemaphoresCount, signalStages, signalSemaphores);
    }

    void CommandBuffer::BufferBarrier(const BufferBarrierInfo &info)
    {
        info.buffer->Barrier(this, info);
    }

    void CommandBuffer::BufferBarriers(const std::vector<BufferBarrierInfo> &infos)
    {
        Buffer::Barriers(this, infos);
    }

    void CommandBuffer::ImageBarrier(const ImageBarrierInfo &info)
    {
        info.image->Barrier(this, info);
    }

    void CommandBuffer::ImageBarriers(const std::vector<ImageBarrierInfo> &infos)
    {
        Image::Barriers(this, infos);
    }

    void CommandBuffer::MemoryBarrier(const MemoryBarrierInfo &info)
    {
        VkMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
        barrier.srcStageMask = Translate<VkPipelineStageFlags2>(info.srcStageMask);
        barrier.srcAccessMask = Translate<VkAccessFlags2>(info.srcAccessMask);
        barrier.dstStageMask = Translate<VkPipelineStageFlags2>(info.dstStageMask);
        barrier.dstAccessMask = Translate<VkAccessFlags2>(info.dstAccessMask);

        VkDependencyInfo dependencyInfo{};
        dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependencyInfo.memoryBarrierCount = 1;
        dependencyInfo.pMemoryBarriers = &barrier;

        vkCmdPipelineBarrier2(m_handle, &dependencyInfo);
    }

    void CommandBuffer::MemoryBarriers(const std::vector<MemoryBarrierInfo> &infos)
    {
        std::vector<VkMemoryBarrier2> barriers(infos.size());

        for (uint32_t i = 0; i < infos.size(); i++)
        {
            barriers[i].sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
            barriers[i].srcStageMask = Translate<VkPipelineStageFlags2>(infos[i].srcStageMask);
            barriers[i].srcAccessMask = Translate<VkAccessFlags2>(infos[i].srcAccessMask);
            barriers[i].dstStageMask = Translate<VkPipelineStageFlags2>(infos[i].dstStageMask);
            barriers[i].dstAccessMask = Translate<VkAccessFlags2>(infos[i].dstAccessMask);
        }

        VkDependencyInfo dependencyInfo{};
        dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependencyInfo.memoryBarrierCount = static_cast<uint32_t>(barriers.size());
        dependencyInfo.pMemoryBarriers = barriers.data();

        vkCmdPipelineBarrier2(m_handle, &dependencyInfo);
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
        m_commandFlags |= CommandType::TransferBit;
    }

    void CommandBuffer::CopyImage(Image *src, Image *dst)
    {
        dst->CopyImage(this, src);
        m_commandFlags |= CommandType::TransferBit;
    }

    void CommandBuffer::GenerateMipMaps(Image *image)
    {
        image->GenerateMipMaps(this);
        m_commandFlags |= CommandType::TransferBit;
    }

    void CommandBuffer::SetEvent(Image *image,
                                 ImageLayout scrLayout, ImageLayout dstLayout,
                                 PipelineStageFlags srcStage, PipelineStageFlags dstStage,
                                 AccessFlags srcAccess, AccessFlags dstAccess)
    {
        m_event->Set(this, image, scrLayout, dstLayout, srcStage, dstStage, srcAccess, dstAccess);
    }

    void CommandBuffer::WaitEvent()
    {
        m_event->Wait();
    }

    void CommandBuffer::ResetEvent(PipelineStageFlags resetStage)
    {
        m_event->Reset(resetStage);
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
        s_allCmds = std::vector<std::unordered_map<size_t, CommandBuffer *>>(queueFamPropCount);
        s_availableCmds = std::vector<std::unordered_map<size_t, CommandBuffer *>>(queueFamPropCount);
        for (uint32_t i = 0; i < queueFamPropCount; i++)
        {
            s_allCmds[i] = std::unordered_map<size_t, CommandBuffer *>();
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

    CommandBuffer *CommandBuffer::GetFree(uint32_t familyId)
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
        PE_ERROR_IF(!cmd || !cmd->m_handle, "CommandBuffer::Return: CommandBuffer is null or invalid");
        PE_ERROR_IF(s_allCmds[cmd->GetFamilyId()].find(cmd->m_id) == s_allCmds[cmd->m_familyId].end(), "CommandBuffer::Return: CommandBuffer does not belong to pool");
        PE_ERROR_IF(cmd->m_recording, "CommandBuffer::Return: " + cmd->m_name + " is still recording!");
        PE_ERROR_IF(cmd->m_semaphore->GetValue() != cmd->m_submitions, "CommandBuffer::Return: " + cmd->m_name + " is not finished!");
        //--------------------------------------------------------------

        s_availableCmds[cmd->GetFamilyId()][cmd->m_id] = cmd;
    }

    void CommandBuffer::Wait()
    {
        std::lock_guard<std::mutex> lock(s_WaitMutex);

        PE_ERROR_IF(m_recording, "CommandBuffer::Wait: " + m_name + " is still recording!");

        m_semaphore->Wait(m_submitions);

        if (!m_afterWaitCallbacks.IsEmpty())
        {
            m_afterWaitCallbacks.ReverseInvoke();
            m_afterWaitCallbacks.Clear();
        }
    }

    void CommandBuffer::AddAfterWaitCallback(Delegate<>::FunctionType &&func)
    {
        m_afterWaitCallbacks += std::forward<Delegate<>::FunctionType>(func);
    }
}
#endif
