#ifdef PE_VULKAN
#include "Renderer/Command.h"
#include "Renderer/RHI.h"
#include "Renderer/RenderPass.h"
#include "Renderer/Framebuffer.h"
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
        m_apiHandle = commandPool;

        Debug::SetObjectName(m_apiHandle, name);
    }

    CommandPool::~CommandPool()
    {
        if (m_apiHandle)
        {
            vkDestroyCommandPool(RHII.GetDevice(), m_apiHandle, nullptr);
            m_apiHandle = {};
        }
    }

    void CommandPool::Init(GpuApiHandle gpu)
    {
        uint32_t queueFamPropCount;
        vkGetPhysicalDeviceQueueFamilyProperties(gpu, &queueFamPropCount, nullptr);

        // CommandPools are creating CommandBuffers from a specific family id, this will have to
        // match with the Queue created from a falily id that this CommandBuffer will be submited to.
        s_allCps.resize(queueFamPropCount);
        s_availableCps.resize(queueFamPropCount);
        for (uint32_t i = 0; i < queueFamPropCount; i++)
            s_availableCps[i] = {};
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
        : m_id{ID::NextID()},
          m_familyId{familyId},
          m_submitions{0},
          m_renderPass{nullptr},
          m_boundPipeline{nullptr},
          m_commandPool{CommandPool::GetFree(familyId)},
          m_semaphore{Semaphore::Create(true, name + "_semaphore")},
          m_event{Event::Create(name + "_event")},
          m_name{name}
    {

        VkCommandBufferAllocateInfo cbai{};
        cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cbai.commandPool = m_commandPool->ApiHandle();
        cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cbai.commandBufferCount = 1;

        VkCommandBuffer commandBuffer;
        PE_CHECK(vkAllocateCommandBuffers(RHII.GetDevice(), &cbai, &commandBuffer));
        m_apiHandle = commandBuffer;

        Debug::SetObjectName(m_apiHandle, name);
    }

    CommandBuffer::~CommandBuffer()
    {
        Semaphore::Destroy(m_semaphore);

        if (m_apiHandle && m_commandPool)
        {
            VkCommandBuffer cmd = m_apiHandle;
            vkFreeCommandBuffers(RHII.GetDevice(), m_commandPool->ApiHandle(), 1, &cmd);
            m_apiHandle = {};

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

        PE_CHECK(vkBeginCommandBuffer(m_apiHandle, &beginInfo));
        m_recording = true;
        m_commandFlags = CommandType::None;

        BeginDebugRegion(m_name);
    }

    void CommandBuffer::End()
    {  
        EndDebugRegion();
        
        if (!m_recording)
            PE_ERROR("CommandBuffer::End: CommandBuffer is not in recording state!");

        PE_CHECK(vkEndCommandBuffer(m_apiHandle));
        m_recording = false;
    }

    void CommandBuffer::Reset()
    {
        if (m_commandPool->GetFlags() & CommandPoolCreate::ResetCommandBuffer)
            vkResetCommandBuffer(m_apiHandle, 0);
        else
            vkResetCommandPool(RHII.GetDevice(), m_commandPool->ApiHandle(), 0);
    }

    void CommandBuffer::SetDepthBias(float constantFactor, float clamp, float slopeFactor)
    {
        vkCmdSetDepthBias(m_apiHandle, constantFactor, clamp, slopeFactor);
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
            const vec4 &color = image->m_createInfo.clearColor;

            VkClearColorValue clearValue{};
            clearValue.float32[0] = color[0];
            clearValue.float32[1] = color[1];
            clearValue.float32[2] = color[2];
            clearValue.float32[3] = color[3];

            VkImageSubresourceRange rangeVK{};
            rangeVK.aspectMask = Translate<VkImageAspectFlags>(GetAspectMask(image->GetFormat()));
            rangeVK.baseMipLevel = 0;
            rangeVK.levelCount = 1;
            rangeVK.baseArrayLayer = 0;
            rangeVK.layerCount = 1;

            BeginDebugRegion("Clear Color: " + image->m_createInfo.name);
            vkCmdClearColorImage(m_apiHandle, image->ApiHandle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearValue, 1, &rangeVK);
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
            clearValue.depth = image->m_createInfo.clearColor[0];
            clearValue.stencil = static_cast<uint32_t>(image->m_createInfo.clearColor[1]);

            VkImageSubresourceRange rangeVK{};
            rangeVK.aspectMask = Translate<VkImageAspectFlags>(GetAspectMask(image->GetFormat()));
            rangeVK.baseMipLevel = 0;
            rangeVK.levelCount = 1;
            rangeVK.baseArrayLayer = 0;
            rangeVK.layerCount = 1;

            BeginDebugRegion("Clear Depth: " + image->m_createInfo.name);
            vkCmdClearDepthStencilImage(m_apiHandle, image->ApiHandle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearValue, 1, &rangeVK);
            EndDebugRegion();

            m_commandFlags |= CommandType::TransferBit;
        }
    }

    // Note: We don't care about the actual images, having the Attachment struct for convenience
    RenderPass *CommandBuffer::GetRenderPass(uint32_t count, Attachment *attachments)
    {
        Hash hash;
        hash.Combine(count);
        for (uint32_t i = 0; i < count; i++)
        {
            const Attachment &attachment = attachments[i];
            hash.Combine(static_cast<uint32_t>(attachment.image->GetFormat()));
            hash.Combine(static_cast<uint32_t>(attachment.image->m_createInfo.samples));
            hash.Combine(static_cast<uint32_t>(attachment.loadOp));
            hash.Combine(static_cast<uint32_t>(attachment.storeOp));
            hash.Combine(static_cast<uint32_t>(attachment.stencilLoadOp));
            hash.Combine(static_cast<uint32_t>(attachment.stencilStoreOp));
        }

        auto it = s_renderPasses.find(hash);
        if (it != s_renderPasses.end())
        {
            return it->second;
        }
        else
        {
            std::string name = "Auto_Gen_RenderPass_" + std::to_string(s_renderPasses.size());
            RenderPass *newRenderPass = RenderPass::Create(count, attachments, name);
            s_renderPasses[hash] = newRenderPass;

            return newRenderPass;
        }
    }

    // Note: Cares only about the actual images, just having the Attachment struct for convenience
    Framebuffer *CommandBuffer::GetFramebuffer(RenderPass *renderPass, uint32_t count, Attachment *attachments)
    {
        Hash hash;
        hash.Combine(reinterpret_cast<std::intptr_t>(renderPass));

        for (uint32_t i = 0; i < count; i++)
            hash.Combine(reinterpret_cast<std::intptr_t>(attachments[i].image));

        auto it = s_framebuffers.find(hash);
        if (it != s_framebuffers.end())
        {
            return it->second;
        }
        else
        {
            std::vector<ImageViewApiHandle> views{};
            views.reserve(count);

            for (uint32_t i = 0; i < count; i++)
            {
                Image *image = attachments[i].image;
                if (!image->HasRTV())
                    image->CreateRTV();
                views.push_back(image->GetRTV());
            }

            std::string name = "Auto_Gen_Framebuffer_" + std::to_string(s_framebuffers.size());
            Framebuffer *newFramebuffer = Framebuffer::Create(attachments[0].image->m_createInfo.width,
                                                              attachments[0].image->m_createInfo.height,
                                                              static_cast<uint32_t>(views.size()),
                                                              views.data(),
                                                              renderPass,
                                                              name);
            s_framebuffers[hash] = newFramebuffer;

            return newFramebuffer;
        }
    }

    void CommandBuffer::BeginPass(uint32_t count, Attachment *attachments, const std::string &name, bool skipDynamicPass)
    {
        BeginDebugRegion(name + "_pass");
        m_commandFlags |= CommandType::GraphicsBit;
        m_dynamicPass = Settings::Get<GlobalSettings>().dynamic_rendering && !skipDynamicPass;
        m_attachmentCount = count;
        m_attachments = attachments;

        std::vector<ImageBarrierInfo> attachmentBarriers;
        attachmentBarriers.reserve(count);

        if (m_dynamicPass)
        {
            std::vector<VkRenderingAttachmentInfo> colorInfos;
            colorInfos.reserve(count);
            VkRenderingAttachmentInfo depthInfo{};
            bool hasDepth = false;
            bool hasStencil = false;

            for (uint32_t i = 0; i < count; i++)
            {
                const Attachment &attachment = attachments[i];

                ImageBarrierInfo barrier{};
                barrier.image = attachment.image;
                barrier.layout = ImageLayout::Attachment;

                if (HasDepth(attachment.image->GetFormat()))
                {
                    hasDepth = true;
                    hasStencil = HasStencil(attachment.image->GetFormat());

                    const vec4 &clearColor = attachment.image->GetClearColor();
                    const float clearDepth = clearColor[0];
                    uint32_t clearStencil = static_cast<uint32_t>(clearColor[1]);

                    depthInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
                    depthInfo.imageView = attachment.image->GetRTV();
                    depthInfo.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
                    depthInfo.loadOp = Translate<VkAttachmentLoadOp>(attachment.loadOp);
                    depthInfo.storeOp = Translate<VkAttachmentStoreOp>(attachment.storeOp);
                    depthInfo.clearValue.depthStencil = {clearDepth, clearStencil};

                    barrier.stageFlags = PipelineStage::EarlyFragmentTestsBit | PipelineStage::LateFragmentTestsBit;
                    if (attachment.loadOp == AttachmentLoadOp::Load)
                        barrier.accessMask = Access::DepthStencilAttachmentReadBit;
                    else
                        barrier.accessMask = Access::DepthStencilAttachmentWriteBit;
                }
                else
                {
                    const vec4 &clearColor = attachment.image->GetClearColor();

                    VkRenderingAttachmentInfo colorInfo{};
                    colorInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
                    colorInfo.imageView = attachment.image->GetRTV();
                    colorInfo.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
                    colorInfo.loadOp = Translate<VkAttachmentLoadOp>(attachment.loadOp);
                    colorInfo.storeOp = Translate<VkAttachmentStoreOp>(attachment.storeOp);
                    colorInfo.clearValue.color = {clearColor[0], clearColor[1], clearColor[2], clearColor[3]};
                    colorInfos.push_back(colorInfo);

                    barrier.stageFlags = PipelineStage::ColorAttachmentOutputBit;
                    if (attachment.loadOp == AttachmentLoadOp::Load)
                        barrier.accessMask = Access::ColorAttachmentReadBit;
                    else
                        barrier.accessMask = Access::ColorAttachmentWriteBit;
                }

                attachmentBarriers.push_back(barrier);
            }

            Image::Barriers(this, attachmentBarriers);

            VkRenderingInfo renderingInfo{};
            renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
            renderingInfo.renderArea = {0, 0, attachments[0].image->GetWidth(), attachments[0].image->GetHeight()};
            renderingInfo.layerCount = 1;
            renderingInfo.colorAttachmentCount = static_cast<uint32_t>(colorInfos.size());
            renderingInfo.pColorAttachments = colorInfos.data();
            renderingInfo.pDepthAttachment = hasDepth ? &depthInfo : nullptr;
            renderingInfo.pStencilAttachment = hasStencil ? &depthInfo : nullptr;

            vkCmdBeginRendering(m_apiHandle, &renderingInfo);
        }
        else
        {
            std::vector<VkClearValue> clearValues(count);
            uint32_t clearOps = 0;

            for (uint32_t i = 0; i < count; i++)
            {
                const Attachment &attachment = attachments[i];
                Image *renderTarget = attachment.image;
                if (HasDepth(renderTarget->GetFormat()) && (attachment.loadOp == AttachmentLoadOp::Clear || attachment.stencilLoadOp == AttachmentLoadOp::Clear))
                {
                    const float clearDepth = renderTarget->m_createInfo.clearColor[0];
                    uint32_t clearStencil = static_cast<uint32_t>(renderTarget->m_createInfo.clearColor[1]);
                    clearValues[clearOps].depthStencil = {clearDepth, clearStencil};
                    clearOps++;
                }
                else if (attachment.loadOp == AttachmentLoadOp::Clear)
                {
                    const vec4 &clearColor = renderTarget->m_createInfo.clearColor;
                    clearValues[clearOps].color = {clearColor[0], clearColor[1], clearColor[2], clearColor[3]};
                    clearOps++;
                }

                ImageBarrierInfo barrier{};
                barrier.image = attachment.image;
                barrier.layout = ImageLayout::Attachment;
                if (HasDepth(renderTarget->GetFormat()))
                {
                    barrier.stageFlags = PipelineStage::EarlyFragmentTestsBit | PipelineStage::LateFragmentTestsBit;
                    barrier.accessMask = Access::DepthStencilAttachmentWriteBit;
                }
                else
                {
                    barrier.stageFlags = PipelineStage::ColorAttachmentOutputBit;
                    barrier.accessMask = Access::ColorAttachmentWriteBit;
                }
                attachmentBarriers.push_back(barrier);
            }

            Image::Barriers(this, attachmentBarriers);

            m_renderPass = GetRenderPass(count, attachments);
            m_framebuffer = GetFramebuffer(m_renderPass, count, attachments);

            VkRenderPassBeginInfo rpi{};
            rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            rpi.renderPass = m_renderPass->ApiHandle();
            rpi.framebuffer = m_framebuffer->ApiHandle();
            rpi.renderArea.offset = VkOffset2D{0, 0};
            rpi.renderArea.extent = VkExtent2D{m_framebuffer->GetWidth(), m_framebuffer->GetHeight()};
            rpi.clearValueCount = clearOps > 0 ? static_cast<uint32_t>(clearValues.size()) : 0;
            rpi.pClearValues = clearOps > 0 ? clearValues.data() : nullptr;

            vkCmdBeginRenderPass(m_apiHandle, &rpi, VK_SUBPASS_CONTENTS_INLINE);
        }
    }

    void CommandBuffer::EndPass()
    {
        // in favor of tracking image layout, stage and access, we need to set the correct access mask for the last operation
        // attachments with AttachmentLoadOp::Load will have the ReadBit access mask and it is used for waiting to read/load the attachment
        // but after the render pass is done, the correct access mask should be the WriteBit since it is the last operation
        // so set the correct track info access mask here before any other access operation happen
        for (uint32_t i = 0; i < m_attachmentCount; i++)
        {
            const Attachment &attachment = m_attachments[i];
            if (attachment.loadOp == AttachmentLoadOp::Load)
            {
                attachment.image->m_trackInfos[0][0].accessMask = HasDepth(attachment.image->GetFormat())
                                                                      ? Access::DepthStencilAttachmentWriteBit
                                                                      : Access::ColorAttachmentWriteBit;
            }
        }

        if (m_dynamicPass)
            vkCmdEndRendering(m_apiHandle);
        else
            vkCmdEndRenderPass(m_apiHandle);

        EndDebugRegion();

        m_attachmentCount = 0;
        m_attachments = nullptr;
        m_renderPass = nullptr;
        m_framebuffer = nullptr;
        m_dynamicPass = false;
        m_boundPipeline = nullptr;
        m_boundVertexBuffer = nullptr;
        m_boundVertexBufferOffset = -1;
        m_boundVertexBufferFirstBinding = UINT32_MAX;
        m_boundVertexBufferBindingCount = UINT32_MAX;
        m_boundIndexBuffer = nullptr;
        m_boundIndexBufferOffset = -1;
    }

    Pipeline *CommandBuffer::GetPipeline(RenderPass *renderPass, PassInfo &passInfo)
    {
        Hash hash;

        if (renderPass)
            hash.Combine(reinterpret_cast<std::intptr_t>(renderPass));

        hash.Combine(passInfo.GetHash());

        auto it = s_pipelines.find(hash);
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

    void CommandBuffer::BindGraphicsPipeline()
    {
        vkCmdBindPipeline(m_apiHandle, VK_PIPELINE_BIND_POINT_GRAPHICS, m_boundPipeline->ApiHandle());
    }

    void CommandBuffer::BindComputePipeline()
    {
        vkCmdBindPipeline(m_apiHandle, VK_PIPELINE_BIND_POINT_COMPUTE, m_boundPipeline->ApiHandle());
    }

    void CommandBuffer::BindPipeline(PassInfo &passInfo, bool bindDescriptors)
    {
        Pipeline *pipeline = GetPipeline(m_renderPass, passInfo);
        if (pipeline == m_boundPipeline)
            return;

        m_boundPipeline = pipeline;

        if (passInfo.pCompShader)
        {
            BindComputePipeline();
        }
        else
        {
            BindGraphicsPipeline();
        }

        if (bindDescriptors)
        {
            // auto bind descriptor sets
            auto descriptors = passInfo.GetDescriptors(RHII.GetFrameIndex());
            uint32_t count = static_cast<uint32_t>(descriptors.size());
            BindDescriptors(count, descriptors.data());
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

        VkBuffer buff = buffer->ApiHandle();
        vkCmdBindVertexBuffers(m_apiHandle, firstBinding, bindingCount, &buff, &offset);
    }

    void CommandBuffer::BindIndexBuffer(Buffer *buffer, size_t offset)
    {
        if (m_boundIndexBuffer == buffer && m_boundIndexBufferOffset == offset)
            return;

        m_boundIndexBuffer = buffer;
        vkCmdBindIndexBuffer(m_apiHandle, buffer->ApiHandle(), offset, VK_INDEX_TYPE_UINT32);
    }

    void CommandBuffer::BindGraphicsDescriptors(uint32_t count, Descriptor **descriptors)
    {
        std::vector<VkDescriptorSet> dsets(count);
        for (uint32_t i = 0; i < count; i++)
        {
            dsets[i] = descriptors[i]->ApiHandle();
        }

        vkCmdBindDescriptorSets(m_apiHandle,
                                VK_PIPELINE_BIND_POINT_GRAPHICS,
                                m_boundPipeline->m_layout,
                                0, count, dsets.data(),
                                0, nullptr);
    }

    void CommandBuffer::BindComputeDescriptors(uint32_t count, Descriptor **descriptors)
    {
        std::vector<VkDescriptorSet> dsets(count);
        for (uint32_t i = 0; i < count; i++)
        {
            dsets[i] = descriptors[i]->ApiHandle();
        }

        vkCmdBindDescriptorSets(m_apiHandle,
                                VK_PIPELINE_BIND_POINT_COMPUTE,
                                m_boundPipeline->m_layout,
                                0, count, dsets.data(),
                                0, nullptr);
    }

    void CommandBuffer::BindDescriptors(uint32_t count, Descriptor **descriptors)
    {
        PE_ERROR_IF(!m_boundPipeline, "CommandBuffer::BindDescriptors: No bound pipeline found!");

        if (m_boundPipeline->m_info.pCompShader)
        {
            BindComputeDescriptors(count, descriptors);
        }
        else
        {
            BindGraphicsDescriptors(count, descriptors);
        }
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
                    imageInfoVK[i][j].sampler = info[i].type == DescriptorType::CombinedImageSampler ? info[i].samplers[j] : SamplerApiHandle{};
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
                    bufferInfoVK[i][j].buffer = info[i].buffers[j]->ApiHandle();
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

        vkCmdPushDescriptorSetKHR(m_apiHandle,
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

        vkCmdSetViewport(m_apiHandle, 0, 1, &viewport);
    }

    void CommandBuffer::SetScissor(int x, int y, uint32_t width, uint32_t height)
    {
        VkRect2D scissor{};
        scissor.offset = {x, y};
        scissor.extent = {width, height};

        vkCmdSetScissor(m_apiHandle, 0, 1, &scissor);
    }

    void CommandBuffer::SetLineWidth(float width)
    {
        vkCmdSetLineWidth(m_apiHandle, width);
    }

    void CommandBuffer::SetDepthTestEnable(uint32_t enable)
    {
        vkCmdSetDepthTestEnable(m_apiHandle, enable);
    }

    void CommandBuffer::SetDepthWriteEnable(uint32_t enable)
    {
        vkCmdSetDepthWriteEnable(m_apiHandle, enable);
    }

    void CommandBuffer::Dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
    {
        vkCmdDispatch(m_apiHandle, groupCountX, groupCountY, groupCountZ);
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
            vkCmdPushConstants(m_apiHandle,
                               m_boundPipeline->m_layout,
                               Translate<VkShaderStageFlags>(stages[i]),
                               offsets[i],
                               sizes[i],
                               m_pushConstants.Data());
        }
    }

    void CommandBuffer::Draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance)
    {
        vkCmdDraw(m_apiHandle, vertexCount, instanceCount, firstVertex, firstInstance);
    }

    void CommandBuffer::DrawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance)
    {
        vkCmdDrawIndexed(m_apiHandle, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
    }

    void CommandBuffer::DrawIndirect(Buffer *indirectBuffer, size_t offset, uint32_t drawCount, uint32_t stride)
    {
        vkCmdDrawIndirect(m_apiHandle, indirectBuffer->ApiHandle(), offset, drawCount, stride);
    }

    void CommandBuffer::DrawIndexedIndirect(Buffer *indirectBuffer, size_t offset, uint32_t drawCount, uint32_t stride)
    {
        vkCmdDrawIndexedIndirect(m_apiHandle, indirectBuffer->ApiHandle(), offset, drawCount, stride);
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
        Buffer::Barrier(this, info);
    }

    void CommandBuffer::BufferBarriers(const std::vector<BufferBarrierInfo> &infos)
    {
        Buffer::Barriers(this, infos);
    }

    void CommandBuffer::ImageBarrier(const ImageBarrierInfo &info)
    {
        Image::Barrier(this, info);
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

        vkCmdPipelineBarrier2(m_apiHandle, &dependencyInfo);
    }

    void CommandBuffer::MemoryBarriers(const std::vector<MemoryBarrierInfo> &infos)
    {
        std::vector<VkMemoryBarrier2> barriers(infos.size());

        for (uint32_t i = 0; i < infos.size(); i++)
        {
            const MemoryBarrierInfo &info = infos[i];
            barriers[i].sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
            barriers[i].srcStageMask = Translate<VkPipelineStageFlags2>(info.srcStageMask);
            barriers[i].srcAccessMask = Translate<VkAccessFlags2>(info.srcAccessMask);
            barriers[i].dstStageMask = Translate<VkPipelineStageFlags2>(info.dstStageMask);
            barriers[i].dstAccessMask = Translate<VkAccessFlags2>(info.dstAccessMask);
        }

        VkDependencyInfo dependencyInfo{};
        dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependencyInfo.memoryBarrierCount = static_cast<uint32_t>(barriers.size());
        dependencyInfo.pMemoryBarriers = barriers.data();

        vkCmdPipelineBarrier2(m_apiHandle, &dependencyInfo);
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

    void CommandBuffer::Init(GpuApiHandle gpu, uint32_t countPerFamily)
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

    CommandBuffer *CommandBuffer::GetFree(Queue *queue)
    {
        return GetFree(queue->GetFamilyId());
    }

    void CommandBuffer::Return(CommandBuffer *cmd)
    {
        std::lock_guard<std::mutex> lock(s_returnMutex);

        // CHECKS
        // -------------------------------------------------------------
        PE_ERROR_IF(!cmd || !cmd->m_apiHandle, "CommandBuffer::Return: CommandBuffer is null or invalid");
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
