#include "API/Command.h"
#include "API/Buffer.h"
#include "API/Descriptor.h"
#include "API/Event.h"
#include "API/Framebuffer.h"
#include "API/Image.h"
#include "API/Pipeline.h"
#include "API/Queue.h"
#include "API/RHI.h"
#include "API/RenderPass.h"
#include "API/Semaphore.h"

namespace pe
{
    CommandBuffer::CommandBuffer(CommandPool *commandPool, const std::string &name)
        : m_id{ID::NextID()},
          m_submission{0},
          m_renderPass{nullptr},
          m_boundPipeline{nullptr},
          m_commandPool{commandPool},
          m_event{Event::Create(name + "_event")},
          m_name{name},
          m_threadId{std::this_thread::get_id()}
    {
        vk::CommandBufferAllocateInfo cbai{};
        cbai.commandPool = m_commandPool->ApiHandle();
        cbai.level = vk::CommandBufferLevel::ePrimary;
        cbai.commandBufferCount = 1;

        m_apiHandle = RHII.GetDevice().allocateCommandBuffers(cbai)[0];

        Debug::SetObjectName(m_apiHandle, name);
    }

    CommandBuffer::~CommandBuffer()
    {
        Event::Destroy(m_event);

#if PE_DEBUG_MODE
        for (auto &gpuTimerInfo : m_gpuTimerInfos)
            GpuTimer::Destroy(gpuTimerInfo.timer);
#endif
    }

    void CommandBuffer::Begin()
    {
        PE_ERROR_IF(m_recording, "CommandBuffer::Begin: CommandBuffer is already recording!");
        PE_ERROR_IF(m_threadId != std::this_thread::get_id(), "CommandBuffer::Begin: CommandBuffer is used in a different thread!");

        Reset();

        vk::CommandBufferBeginInfo beginInfo{};
        beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
        m_apiHandle.begin(beginInfo);
        m_recording = true;

        BeginDebugRegion(m_name);
    }

    void CommandBuffer::End()
    {
        EndDebugRegion();

        PE_ERROR_IF(!m_recording, "CommandBuffer::End: CommandBuffer is not in recording state!");
        PE_ERROR_IF(m_threadId != std::this_thread::get_id(), "CommandBuffer::End: CommandBuffer is used in a different thread!");

        m_apiHandle.end();
        m_recording = false;
    }

    void CommandBuffer::Reset()
    {
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

        PE_ERROR_IF(!(m_commandPool->GetFlags() & vk::CommandPoolCreateFlagBits::eResetCommandBuffer), "CommandBuffer::Reset: CommandPool does not have the reset flag!");
        m_apiHandle.reset();
    }

    void CommandBuffer::SetDepthBias(float constantFactor, float clamp, float slopeFactor)
    {
        m_apiHandle.setDepthBias(constantFactor, clamp, slopeFactor);
    }

    void CommandBuffer::BlitImage(Image *src, Image *dst, const vk::ImageBlit &region, vk::Filter filter)
    {
        dst->Blit(this, src, region, filter);
    }

    void CommandBuffer::ClearColors(std::vector<Image *> images)
    {
        std::vector<ImageBarrierInfo> barriers(images.size());
        for (uint32_t i = 0; i < images.size(); i++)
        {
            barriers[i].image = images[i];
            barriers[i].layout = vk::ImageLayout::eTransferDstOptimal;
            barriers[i].stageFlags = vk::PipelineStageFlagBits2::eTransfer;
            barriers[i].accessMask = vk::AccessFlagBits2::eTransferWrite;
        }
        Image::Barriers(this, barriers);

        for (uint32_t i = 0; i < images.size(); i++)
        {
            Image *image = images[i];
            const vec4 &color = image->m_clearColor;

            vk::ClearColorValue clearValue{};
            clearValue.float32[0] = color[0];
            clearValue.float32[1] = color[1];
            clearValue.float32[2] = color[2];
            clearValue.float32[3] = color[3];

            vk::ImageSubresourceRange range{};
            range.aspectMask = VulkanHelpers::GetAspectMask(image->GetFormat());
            range.baseMipLevel = 0;
            range.levelCount = 1;
            range.baseArrayLayer = 0;
            range.layerCount = 1;

            BeginDebugRegion("Clear Color: " + image->m_name);
            m_apiHandle.clearColorImage(image->ApiHandle(), vk::ImageLayout::eTransferDstOptimal, &clearValue, 1, &range);
            EndDebugRegion();
        }
    }

    void CommandBuffer::ClearDepthStencils(std::vector<Image *> images)
    {
        std::vector<ImageBarrierInfo> barriers(images.size());
        for (uint32_t i = 0; i < images.size(); i++)
        {
            barriers[i].image = images[i];
            barriers[i].layout = vk::ImageLayout::eTransferDstOptimal;
            barriers[i].stageFlags = vk::PipelineStageFlagBits2::eTransfer;
            barriers[i].accessMask = vk::AccessFlagBits2::eTransferWrite;
        }
        Image::Barriers(this, barriers);

        for (uint32_t i = 0; i < images.size(); i++)
        {
            Image *image = images[i];

            vk::ClearDepthStencilValue clearValue{};
            clearValue.depth = image->m_clearColor[0];
            clearValue.stencil = static_cast<uint32_t>(image->m_clearColor[1]);

            vk::ImageSubresourceRange range{};
            range.aspectMask = VulkanHelpers::GetAspectMask(image->GetFormat());
            range.baseMipLevel = 0;
            range.levelCount = 1;
            range.baseArrayLayer = 0;
            range.layerCount = 1;

            BeginDebugRegion("Clear Depth: " + image->m_name);
            m_apiHandle.clearDepthStencilImage(image->ApiHandle(), vk::ImageLayout::eTransferDstOptimal, &clearValue, 1, &range);
            EndDebugRegion();
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
            std::vector<vk::ImageView> views{};
            views.reserve(count);

            for (uint32_t i = 0; i < count; i++)
            {
                Image *image = attachments[i].image;
                if (!image->HasRTV())
                    image->CreateRTV();
                views.push_back(image->GetRTV());
            }

            std::string name = "Auto_Gen_Framebuffer_" + std::to_string(s_framebuffers.size());
            Framebuffer *newFramebuffer = Framebuffer::Create(attachments[0].image->m_createInfo.extent.width,
                                                              attachments[0].image->m_createInfo.extent.height,
                                                              views,
                                                              renderPass,
                                                              name);
            s_framebuffers[hash] = newFramebuffer;

            return newFramebuffer;
        }
    }

    void CommandBuffer::BeginPass(uint32_t count, Attachment *attachments, const std::string &name, bool skipDynamicPass)
    {
        BeginDebugRegion(name + "_pass");
        m_dynamicPass = Settings::Get<GlobalSettings>().dynamic_rendering && !skipDynamicPass;
        m_attachmentCount = count;
        m_attachments = attachments;

        std::vector<ImageBarrierInfo> attachmentBarriers;
        attachmentBarriers.reserve(count);

        if (m_dynamicPass)
        {
            std::vector<vk::RenderingAttachmentInfo> colorInfos;
            colorInfos.reserve(count);
            vk::RenderingAttachmentInfo depthInfo{};
            bool hasDepth = false;
            bool hasStencil = false;

            for (uint32_t i = 0; i < count; i++)
            {
                const Attachment &attachment = attachments[i];

                ImageBarrierInfo barrier{};
                barrier.image = attachment.image;
                barrier.layout = vk::ImageLayout::eAttachmentOptimal;

                if (VulkanHelpers::HasDepthOrStencil(attachment.image->GetFormat()))
                {
                    hasDepth |= VulkanHelpers::HasDepth(attachment.image->GetFormat());
                    hasStencil |= VulkanHelpers::HasStencil(attachment.image->GetFormat());

                    const vec4 &clearColor = attachment.image->m_clearColor;
                    const float clearDepth = clearColor[0];
                    uint32_t clearStencil = static_cast<uint32_t>(clearColor[1]);

                    depthInfo.imageView = attachment.image->GetRTV();
                    depthInfo.imageLayout = vk::ImageLayout::eAttachmentOptimal;
                    depthInfo.loadOp = attachment.loadOp;
                    depthInfo.storeOp = attachment.storeOp;
                    depthInfo.clearValue.depthStencil = vk::ClearDepthStencilValue{clearDepth, clearStencil};

                    barrier.stageFlags = vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests;
                    if (attachment.loadOp == vk::AttachmentLoadOp::eLoad)
                        barrier.accessMask = vk::AccessFlagBits2::eDepthStencilAttachmentRead;
                    else
                        barrier.accessMask = vk::AccessFlagBits2::eDepthStencilAttachmentWrite;
                }
                else
                {
                    const vec4 &clearColor = attachment.image->m_clearColor;

                    vk::RenderingAttachmentInfo colorInfo{};
                    colorInfo.imageView = attachment.image->GetRTV();
                    colorInfo.imageLayout = vk::ImageLayout::eAttachmentOptimal;
                    colorInfo.loadOp = attachment.loadOp;
                    colorInfo.storeOp = attachment.storeOp;
                    colorInfo.clearValue.color = {clearColor[0], clearColor[1], clearColor[2], clearColor[3]};
                    colorInfos.push_back(colorInfo);

                    barrier.stageFlags = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
                    if (attachment.loadOp == vk::AttachmentLoadOp::eLoad)
                        barrier.accessMask = vk::AccessFlagBits2::eColorAttachmentRead;
                    else
                        barrier.accessMask = vk::AccessFlagBits2::eColorAttachmentWrite;
                }

                attachmentBarriers.push_back(barrier);
            }

            Image::Barriers(this, attachmentBarriers);

            vk::RenderingInfo renderingInfo{};
            renderingInfo.renderArea = {0, 0, attachments[0].image->GetWidth(), attachments[0].image->GetHeight()};
            renderingInfo.layerCount = 1;
            renderingInfo.colorAttachmentCount = static_cast<uint32_t>(colorInfos.size());
            renderingInfo.pColorAttachments = colorInfos.data();
            renderingInfo.pDepthAttachment = hasDepth ? &depthInfo : nullptr;
            renderingInfo.pStencilAttachment = hasStencil ? &depthInfo : nullptr;

            m_apiHandle.beginRendering(renderingInfo);
        }
        else
        {
            std::vector<vk::ClearValue> clearValues(count);
            uint32_t clearOps = 0;

            for (uint32_t i = 0; i < count; i++)
            {
                const Attachment &attachment = attachments[i];
                Image *renderTarget = attachment.image;
                if (VulkanHelpers::HasDepthOrStencil(renderTarget->GetFormat()) && (attachment.loadOp == vk::AttachmentLoadOp::eClear || attachment.stencilLoadOp == vk::AttachmentLoadOp::eClear))
                {
                    const float clearDepth = renderTarget->m_clearColor[0];
                    uint32_t clearStencil = static_cast<uint32_t>(renderTarget->m_clearColor[1]);
                    clearValues[clearOps].depthStencil = vk::ClearDepthStencilValue{clearDepth, clearStencil};
                    clearOps++;
                }
                else if (attachment.loadOp == vk::AttachmentLoadOp::eClear)
                {
                    const vec4 &clearColor = renderTarget->m_clearColor;
                    clearValues[clearOps].color = {clearColor[0], clearColor[1], clearColor[2], clearColor[3]};
                    clearOps++;
                }

                ImageBarrierInfo barrier{};
                barrier.image = attachment.image;
                barrier.layout = vk::ImageLayout::eAttachmentOptimal;
                if (VulkanHelpers::HasDepthOrStencil(renderTarget->GetFormat()))
                {
                    barrier.stageFlags = vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests;
                    barrier.accessMask = vk::AccessFlagBits2::eDepthStencilAttachmentWrite;
                }
                else
                {
                    barrier.stageFlags = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
                    barrier.accessMask = vk::AccessFlagBits2::eColorAttachmentWrite;
                }
                attachmentBarriers.push_back(barrier);
            }

            Image::Barriers(this, attachmentBarriers);

            m_renderPass = GetRenderPass(count, attachments);
            m_framebuffer = GetFramebuffer(m_renderPass, count, attachments);

            vk::RenderPassBeginInfo rpi{};
            rpi.renderPass = m_renderPass->ApiHandle();
            rpi.framebuffer = m_framebuffer->ApiHandle();
            rpi.renderArea.offset = vk::Offset2D{0, 0};
            rpi.renderArea.extent = vk::Extent2D{m_framebuffer->GetWidth(), m_framebuffer->GetHeight()};
            rpi.clearValueCount = static_cast<uint32_t>(clearValues.size());
            rpi.pClearValues = clearValues.data();

            vk::SubpassBeginInfo sbi{};
            sbi.contents = vk::SubpassContents::eInline;

            m_apiHandle.beginRenderPass2(rpi, sbi);
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
            if (attachment.loadOp == vk::AttachmentLoadOp::eLoad)
            {
                attachment.image->m_trackInfos[0][0].accessMask = VulkanHelpers::HasDepth(attachment.image->GetFormat())
                                                                      ? vk::AccessFlagBits2::eDepthStencilAttachmentWrite
                                                                      : vk::AccessFlagBits2::eColorAttachmentWrite;
            }
        }

        if (m_dynamicPass)
            m_apiHandle.endRendering();
        else
            m_apiHandle.endRenderPass();

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

    void CommandBuffer::ClearCache()
    {
        for (auto &[hash, renderPass] : s_renderPasses)
        {
            RenderPass::Destroy(renderPass);
        }
        s_renderPasses.clear();

        for (auto &[hash, framebuffer] : s_framebuffers)
        {
            Framebuffer::Destroy(framebuffer);
        }
        s_framebuffers.clear();

        for (auto &[hash, pipeline] : s_pipelines)
        {
            Pipeline::Destroy(pipeline);
        }
        s_pipelines.clear();
    }

    void CommandBuffer::BindGraphicsPipeline()
    {
        m_apiHandle.bindPipeline(vk::PipelineBindPoint::eGraphics, m_boundPipeline->ApiHandle());
    }

    void CommandBuffer::BindComputePipeline()
    {
        m_apiHandle.bindPipeline(vk::PipelineBindPoint::eCompute, m_boundPipeline->ApiHandle());
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

        vk::Buffer &buff = buffer->ApiHandle();
        m_apiHandle.bindVertexBuffers(firstBinding, bindingCount, &buff, &offset);
    }

    void CommandBuffer::BindIndexBuffer(Buffer *buffer, size_t offset)
    {
        if (m_boundIndexBuffer == buffer && m_boundIndexBufferOffset == offset)
            return;

        m_boundIndexBuffer = buffer;
        m_apiHandle.bindIndexBuffer(buffer->ApiHandle(), offset, vk::IndexType::eUint32);
    }

    void CommandBuffer::BindGraphicsDescriptors(uint32_t count, Descriptor *const *descriptors)
    {
        std::vector<vk::DescriptorSet> dsets(count);
        for (uint32_t i = 0; i < count; i++)
        {
            dsets[i] = descriptors[i]->ApiHandle();
        }

        m_apiHandle.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                       m_boundPipeline->m_layout,
                                       0, count, dsets.data(),
                                       0, nullptr);
    }

    void CommandBuffer::BindComputeDescriptors(uint32_t count, Descriptor *const *descriptors)
    {
        std::vector<vk::DescriptorSet> dsets(count);
        for (uint32_t i = 0; i < count; i++)
        {
            dsets[i] = descriptors[i]->ApiHandle();
        }

        m_apiHandle.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                                       m_boundPipeline->m_layout,
                                       0, count, dsets.data(),
                                       0, nullptr);
    }

    void CommandBuffer::BindDescriptors(uint32_t count, Descriptor *const *descriptors)
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

        std::vector<vk::WriteDescriptorSet> writes{};
        std::vector<std::vector<vk::DescriptorImageInfo>> imageInfo{};
        std::vector<std::vector<vk::DescriptorBufferInfo>> bufferInfo{};
        imageInfo.resize(info.size());
        bufferInfo.resize(info.size());
        writes.resize(info.size());

        for (uint32_t i = 0; i < info.size(); i++)
        {
            writes[i] = vk::WriteDescriptorSet{};
            writes[i].dstBinding = info[i].binding;
            writes[i].dstArrayElement = 0;
            writes[i].descriptorType = info[i].type;
            if (info[i].views.size() > 0)
            {
                imageInfo[i].resize(info[i].views.size());
                for (uint32_t j = 0; j < info[i].views.size(); j++)
                {
                    imageInfo[i][j] = vk::DescriptorImageInfo{};
                    imageInfo[i][j].imageView = info[i].views[j];
                    imageInfo[i][j].imageLayout = info[i].layouts[j];
                    imageInfo[i][j].sampler = info[i].type == vk::DescriptorType::eCombinedImageSampler ? info[i].samplers[j] : vk::Sampler{};
                }

                writes[i].descriptorCount = static_cast<uint32_t>(imageInfo[i].size());
                writes[i].pImageInfo = imageInfo[i].data();
            }
            else if (info[i].buffers.size() > 0)
            {
                bufferInfo[i].resize(info[i].buffers.size());
                for (uint32_t j = 0; j < info[i].buffers.size(); j++)
                {
                    bufferInfo[i][j] = vk::DescriptorBufferInfo{};
                    bufferInfo[i][j].buffer = info[i].buffers[j]->ApiHandle();
                    bufferInfo[i][j].offset = info[i].offsets.size() > 0 ? info[i].offsets[j] : 0;
                    bufferInfo[i][j].range = info[i].ranges.size() > 0 ? info[i].ranges[j] : vk::WholeSize;
                }

                writes[i].descriptorCount = static_cast<uint32_t>(bufferInfo[i].size());
                writes[i].pBufferInfo = bufferInfo[i].data();
            }
            else if (info[i].samplers.size() > 0 && info[i].type != vk::DescriptorType::eCombinedImageSampler)
            {
                imageInfo[i].resize(info[i].samplers.size());
                for (uint32_t j = 0; j < info[i].samplers.size(); j++)
                {
                    imageInfo[i][j] = vk::DescriptorImageInfo{};
                    imageInfo[i][j].imageView = nullptr;
                    imageInfo[i][j].imageLayout = vk::ImageLayout::eUndefined;
                    imageInfo[i][j].sampler = info[i].samplers[j];
                }

                writes[i].descriptorCount = static_cast<uint32_t>(imageInfo[i].size());
                writes[i].pImageInfo = imageInfo[i].data();
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
                                  reinterpret_cast<const VkWriteDescriptorSet *>(writes.data()));
    }

    void CommandBuffer::SetViewport(float x, float y, float width, float height)
    {
        vk::Viewport viewport{};
        viewport.x = x;
        viewport.y = y;
        viewport.width = width;
        viewport.height = height;
        viewport.minDepth = 0.f;
        viewport.maxDepth = 1.f;

        m_apiHandle.setViewport(0, 1, &viewport);
    }

    void CommandBuffer::SetScissor(int x, int y, uint32_t width, uint32_t height)
    {
        vk::Rect2D scissor{};
        scissor.offset = vk::Offset2D{x, y};
        scissor.extent = vk::Extent2D{width, height};

        m_apiHandle.setScissor(0, 1, &scissor);
    }

    void CommandBuffer::SetLineWidth(float width)
    {
        m_apiHandle.setLineWidth(width);
    }

    void CommandBuffer::SetDepthTestEnable(uint32_t enable)
    {
        m_apiHandle.setDepthTestEnable(enable);
    }

    void CommandBuffer::SetDepthWriteEnable(uint32_t enable)
    {
        m_apiHandle.setDepthWriteEnable(enable);
    }

    void CommandBuffer::Dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
    {
        m_apiHandle.dispatch(groupCountX, groupCountY, groupCountZ);
    }

    void CommandBuffer::PushConstants()
    {
        PE_ERROR_IF(!m_boundPipeline, "CommandBuffer::PushConstants: No bound pipeline found!");

        const auto &stages = m_boundPipeline->m_info.m_pushConstantStages;
        const auto &offsets = m_boundPipeline->m_info.m_pushConstantOffsets;
        const auto &sizes = m_boundPipeline->m_info.m_pushConstantSizes;

        for (size_t i = 0; i < stages.size(); i++)
        {
            m_apiHandle.pushConstants(m_boundPipeline->m_layout,
                                      stages[i],
                                      offsets[i],
                                      sizes[i],
                                      m_pushConstants.Data());
        }
    }

    void CommandBuffer::Draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance)
    {
        m_apiHandle.draw(vertexCount, instanceCount, firstVertex, firstInstance);
    }

    void CommandBuffer::DrawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance)
    {
        m_apiHandle.drawIndexed(indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
    }

    void CommandBuffer::DrawIndirect(Buffer *indirectBuffer, size_t offset, uint32_t drawCount, uint32_t stride)
    {
        m_apiHandle.drawIndirect(indirectBuffer->ApiHandle(), offset, drawCount, stride);
    }

    void CommandBuffer::DrawIndexedIndirect(Buffer *indirectBuffer, size_t offset, uint32_t drawCount, uint32_t stride)
    {
        m_apiHandle.drawIndexedIndirect(indirectBuffer->ApiHandle(), offset, drawCount, stride);
    }

    void CommandBuffer::BufferBarrier(const vk::BufferMemoryBarrier2 &info)
    {
        Buffer::Barrier(this, info);
    }

    void CommandBuffer::BufferBarriers(const std::vector<vk::BufferMemoryBarrier2> &infos)
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

    void CommandBuffer::MemoryBarrier(const vk::MemoryBarrier2 &info)
    {
        vk::DependencyInfo dependencyInfo{};
        dependencyInfo.memoryBarrierCount = 1;
        dependencyInfo.pMemoryBarriers = &info;

        m_apiHandle.pipelineBarrier2(dependencyInfo);
    }

    void CommandBuffer::MemoryBarriers(const std::vector<vk::MemoryBarrier2> &infos)
    {
        vk::DependencyInfo dependencyInfo{};
        dependencyInfo.memoryBarrierCount = static_cast<uint32_t>(infos.size());
        dependencyInfo.pMemoryBarriers = infos.data();

        m_apiHandle.pipelineBarrier2(dependencyInfo);
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

    void CommandBuffer::SetEvent(Image *image,
                                 vk::ImageLayout srcLayout, vk::ImageLayout dstLayout,
                                 vk::PipelineStageFlags2 srcStage, vk::PipelineStageFlags2 dstStage,
                                 vk::AccessFlags2 srcAccess, vk::AccessFlags2 dstAccess)
    {
        m_event->Set(this, image, srcLayout, dstLayout, srcStage, dstStage, srcAccess, dstAccess);
    }

    void CommandBuffer::WaitEvent()
    {
        m_event->Wait();
    }

    void CommandBuffer::ResetEvent(vk::PipelineStageFlags2 resetStage)
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

    Queue *CommandBuffer::GetQueue() const
    {
        return m_commandPool->GetQueue();
    }

    uint32_t CommandBuffer::GetFamilyId() const
    {
        return GetQueue()->GetFamilyId();
    }

    void CommandBuffer::Wait()
    {
        std::lock_guard<std::mutex> lock(m_WaitMutex);

        PE_ERROR_IF(m_recording, "CommandBuffer::Wait: CommandBuffer is still recording!");

        GetQueue()->GetSubmissionsSemaphore()->Wait(m_submission);

        if (!m_afterWaitCallbacks.IsEmpty())
        {
            m_afterWaitCallbacks.ReverseInvoke();
            m_afterWaitCallbacks.Clear();
        }

#if PE_DEBUG_MODE
        std::vector<GpuTimerSample> samples;
        samples.reserve(m_gpuTimerInfosCount);
        for (uint32_t i = 0; i < m_gpuTimerInfosCount; ++i)
        {
            const auto &info = m_gpuTimerInfos[i];
            GpuTimerSample sample{};
            sample.name = info.name;
            sample.depth = info.depth;
            sample.timeMs = info.timer ? info.timer->GetTime() : 0.0f;
            samples.emplace_back(std::move(sample));
        }

        EventSystem::DispatchEvent(EventType::AfterCommandWait, std::any{std::move(samples)});
        m_gpuTimerInfosCount = 0;
#endif
    }

    void CommandBuffer::Return()
    {
        GetQueue()->ReturnCommandBuffer(this);
    }

    void CommandBuffer::AddAfterWaitCallback(Delegate<>::FunctionType &&func)
    {
        m_afterWaitCallbacks += std::forward<Delegate<>::FunctionType>(func);
    }
} // namespace pe
