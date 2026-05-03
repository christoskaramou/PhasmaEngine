#include "API/Vulkan/VulkanCommandBufferImpl.h"
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
#include "API/Vulkan/Helpers_Vulkan.h"
#include "API/Vulkan/RHI_Vulkan.h"
#include "API/Vulkan/VulkanBufferImpl.h"
#include "API/Vulkan/VulkanDescriptorImpl.h"
#include "API/Vulkan/VulkanImageImpl.h"
#include "API/Vulkan/VulkanImageViewImpl.h"
#include "API/Vulkan/VulkanPipelineImpl.h"
#include "API/Vulkan/VulkanRHITypeUtils.h"
#include "API/Vulkan/VulkanSamplerImpl.h"

namespace pe
{
    VulkanCommandBufferImpl::VulkanCommandBufferImpl(CommandBuffer *owner, CommandPool *commandPool, const std::string &name)
        : m_owner{owner}
    {
        vk::CommandBufferAllocateInfo cbai{};
        cbai.commandPool = commandPool->ApiHandle();
        cbai.level = vk::CommandBufferLevel::ePrimary;
        cbai.commandBufferCount = 1;

        m_apiHandle = VulkanRhi::Device().allocateCommandBuffers(cbai)[0];
        m_owner->m_apiHandle = m_apiHandle;

        Debug::SetObjectName(m_apiHandle, name);
    }

    VulkanCommandBufferImpl::~VulkanCommandBufferImpl()
    {
        // Vulkan command buffers are freed when their pool is destroyed.
    }

    void VulkanCommandBufferImpl::Begin()
    {
        PE_ERROR_IF(m_owner->m_recording, "CommandBuffer::Begin: CommandBuffer is already recording!");
        PE_ERROR_IF(m_owner->m_threadId != std::this_thread::get_id(), "CommandBuffer::Begin: CommandBuffer is used in a different thread!");

        Reset();

        vk::CommandBufferBeginInfo beginInfo{};
        beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
        m_apiHandle.begin(beginInfo);
        m_owner->m_recording = true;

        BeginDebugRegion(m_owner->m_name);
    }

    void VulkanCommandBufferImpl::End()
    {
        EndDebugRegion();

        PE_ERROR_IF(!m_owner->m_recording, "CommandBuffer::End: CommandBuffer is not in recording state!");
        PE_ERROR_IF(m_owner->m_threadId != std::this_thread::get_id(), "CommandBuffer::End: CommandBuffer is used in a different thread!");

        m_apiHandle.end();
        m_owner->m_recording = false;
    }

    void VulkanCommandBufferImpl::Reset()
    {
        m_owner->m_attachmentCount = 0;
        m_owner->m_attachments = nullptr;
        m_owner->m_renderPass = nullptr;
        m_owner->m_framebuffer = nullptr;
        m_owner->m_dynamicPass = false;
        m_owner->m_boundPipeline = nullptr;
        m_owner->m_boundVertexBuffer = nullptr;
        m_owner->m_boundVertexBufferOffset = -1;
        m_owner->m_boundVertexBufferFirstBinding = UINT32_MAX;
        m_owner->m_boundVertexBufferBindingCount = UINT32_MAX;
        m_owner->m_boundIndexBuffer = nullptr;
        m_owner->m_boundIndexBufferOffset = -1;

        // A buffer reaching Reset() with pending callbacks means the prior recording was
        // abandoned without Wait() — never submitted, or submitted but never waited and
        // now being recycled. Either way the GPU isn't using whatever those callbacks
        // hold (staging allocations, refcounts), so run them before reuse to avoid
        // leaking pinned resources into the next recording's lifetime.
        if (!m_owner->m_afterWaitCallbacks.IsEmpty())
        {
            m_owner->m_afterWaitCallbacks.ReverseInvoke();
            m_owner->m_afterWaitCallbacks.Clear();
        }

        PE_ERROR_IF(!(m_owner->m_commandPool->GetFlags() & vk::CommandPoolCreateFlagBits::eResetCommandBuffer), "CommandBuffer::Reset: CommandPool does not have the reset flag!");
        m_apiHandle.reset();

#if PE_DEBUG_MODE
        m_owner->m_gpuTimerInfosCount = 0;
        while (!m_owner->m_gpuTimerIdsStack.empty())
            m_owner->m_gpuTimerIdsStack.pop();
        // Cached timers may carry m_inUse=true from an abandoned recording.
        for (auto &info : m_owner->m_gpuTimerInfos)
            if (info.timer)
                info.timer->ResetState();
#endif
    }

    void VulkanCommandBufferImpl::SetDepthBias(float constantFactor, float clamp, float slopeFactor)
    {
        m_apiHandle.setDepthBias(constantFactor, clamp, slopeFactor);
    }

    void VulkanCommandBufferImpl::BlitImage(Image *src, Image *dst, const vk::ImageBlit &region, vk::Filter filter)
    {
        dst->Blit(m_owner, src, region, filter);
    }

    void VulkanCommandBufferImpl::ClearColors(std::vector<Image *> images)
    {
        std::vector<ImageBarrierInfo> barriers(images.size());
        for (uint32_t i = 0; i < images.size(); i++)
        {
            barriers[i].image = images[i];
            barriers[i].layout = PE_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barriers[i].stageFlags = PE_STAGE_TRANSFER;
            barriers[i].accessMask = PE_ACCESS_TRANSFER_WRITE;
        }
        Image::Barriers(m_owner, barriers);

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
            range.aspectMask = VulkanHelpers::GetAspectMask(pe::ToVkFormat(image->GetFormat()));
            range.baseMipLevel = 0;
            range.levelCount = 1;
            range.baseArrayLayer = 0;
            range.layerCount = 1;

            BeginDebugRegion("Clear Color: " + image->m_name);
            m_apiHandle.clearColorImage(pe::GetVulkanImage(image), vk::ImageLayout::eTransferDstOptimal, &clearValue, 1, &range);
            EndDebugRegion();
        }
    }

    void VulkanCommandBufferImpl::ClearDepthStencils(std::vector<Image *> images)
    {
        std::vector<ImageBarrierInfo> barriers(images.size());
        for (uint32_t i = 0; i < images.size(); i++)
        {
            barriers[i].image = images[i];
            barriers[i].layout = PE_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barriers[i].stageFlags = PE_STAGE_TRANSFER;
            barriers[i].accessMask = PE_ACCESS_TRANSFER_WRITE;
        }
        Image::Barriers(m_owner, barriers);

        for (uint32_t i = 0; i < images.size(); i++)
        {
            Image *image = images[i];

            vk::ClearDepthStencilValue clearValue{};
            clearValue.depth = image->m_clearColor[0];
            clearValue.stencil = static_cast<uint32_t>(image->m_clearColor[1]);

            vk::ImageSubresourceRange range{};
            range.aspectMask = VulkanHelpers::GetAspectMask(pe::ToVkFormat(image->GetFormat()));
            range.baseMipLevel = 0;
            range.levelCount = 1;
            range.baseArrayLayer = 0;
            range.layerCount = 1;

            BeginDebugRegion("Clear Depth: " + image->m_name);
            m_apiHandle.clearDepthStencilImage(pe::GetVulkanImage(image), vk::ImageLayout::eTransferDstOptimal, &clearValue, 1, &range);
            EndDebugRegion();
        }
    }

    void VulkanCommandBufferImpl::BeginPass(uint32_t count, Attachment *attachments, const std::string &name, bool skipDynamicPass)
    {
        BeginDebugRegion(name + "_pass");
        m_owner->m_dynamicPass = Settings::Get<GlobalSettings>().dynamic_rendering && !skipDynamicPass;
        m_owner->m_attachmentCount = count;
        m_owner->m_attachments = attachments;

        std::vector<ImageBarrierInfo> attachmentBarriers;
        attachmentBarriers.reserve(count);

        if (m_owner->m_dynamicPass)
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
                barrier.layout = PE_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;

                const vk::Format attVkFormat = pe::ToVkFormat(attachment.image->GetFormat());
                if (VulkanHelpers::HasDepthOrStencil(attVkFormat))
                {
                    hasDepth |= VulkanHelpers::HasDepth(attVkFormat);
                    hasStencil |= VulkanHelpers::HasStencil(attVkFormat);

                    const vec4 &clearColor = attachment.image->m_clearColor;
                    const float clearDepth = clearColor[0];
                    uint32_t clearStencil = static_cast<uint32_t>(clearColor[1]);

                    depthInfo.imageView = pe::GetVulkanImageView(attachment.image->GetRTV());
                    depthInfo.imageLayout = vk::ImageLayout::eAttachmentOptimal;
                    depthInfo.loadOp = ToVkLoadOp(attachment.loadOp);
                    depthInfo.storeOp = ToVkStoreOp(attachment.storeOp);
                    depthInfo.clearValue.depthStencil = vk::ClearDepthStencilValue{clearDepth, clearStencil};

                    barrier.stageFlags = PE_STAGE_EARLY_FRAGMENT_TESTS | PE_STAGE_LATE_FRAGMENT_TESTS;
                    if (attachment.loadOp == PE_LOAD_OP_LOAD)
                        barrier.accessMask = PE_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ;
                    else
                        barrier.accessMask = PE_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE;
                }
                else
                {
                    const vec4 &clearColor = attachment.image->m_clearColor;

                    vk::RenderingAttachmentInfo colorInfo{};
                    colorInfo.imageView = pe::GetVulkanImageView(attachment.image->GetRTV());
                    colorInfo.imageLayout = vk::ImageLayout::eAttachmentOptimal;
                    colorInfo.loadOp = ToVkLoadOp(attachment.loadOp);
                    colorInfo.storeOp = ToVkStoreOp(attachment.storeOp);
                    colorInfo.clearValue.color = {clearColor[0], clearColor[1], clearColor[2], clearColor[3]};
                    colorInfos.push_back(colorInfo);

                    barrier.stageFlags = PE_STAGE_COLOR_ATTACHMENT_OUTPUT;
                    if (attachment.loadOp == PE_LOAD_OP_LOAD)
                        barrier.accessMask = PE_ACCESS_COLOR_ATTACHMENT_READ;
                    else
                        barrier.accessMask = PE_ACCESS_COLOR_ATTACHMENT_WRITE;
                }

                attachmentBarriers.push_back(barrier);
            }

            Image::Barriers(m_owner, attachmentBarriers);

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
                if (VulkanHelpers::HasDepthOrStencil(pe::ToVkFormat(renderTarget->GetFormat())) && (attachment.loadOp == PE_LOAD_OP_CLEAR || attachment.stencilLoadOp == PE_LOAD_OP_CLEAR))
                {
                    const float clearDepth = renderTarget->m_clearColor[0];
                    uint32_t clearStencil = static_cast<uint32_t>(renderTarget->m_clearColor[1]);
                    clearValues[clearOps].depthStencil = vk::ClearDepthStencilValue{clearDepth, clearStencil};
                    clearOps++;
                }
                else if (attachment.loadOp == PE_LOAD_OP_CLEAR)
                {
                    const vec4 &clearColor = renderTarget->m_clearColor;
                    clearValues[clearOps].color = {clearColor[0], clearColor[1], clearColor[2], clearColor[3]};
                    clearOps++;
                }

                ImageBarrierInfo barrier{};
                barrier.image = attachment.image;
                barrier.layout = PE_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
                if (VulkanHelpers::HasDepthOrStencil(pe::ToVkFormat(renderTarget->GetFormat())))
                {
                    barrier.stageFlags = PE_STAGE_EARLY_FRAGMENT_TESTS | PE_STAGE_LATE_FRAGMENT_TESTS;
                    barrier.accessMask = PE_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE;
                }
                else
                {
                    barrier.stageFlags = PE_STAGE_COLOR_ATTACHMENT_OUTPUT;
                    barrier.accessMask = PE_ACCESS_COLOR_ATTACHMENT_WRITE;
                }
                attachmentBarriers.push_back(barrier);
            }

            Image::Barriers(m_owner, attachmentBarriers);

            m_owner->m_renderPass = CommandBuffer::GetRenderPass(count, attachments);
            m_owner->m_framebuffer = CommandBuffer::GetFramebuffer(m_owner->m_renderPass, count, attachments);

            vk::RenderPassBeginInfo rpi{};
            rpi.renderPass = m_owner->m_renderPass->ApiHandle();
            rpi.framebuffer = m_owner->m_framebuffer->ApiHandle();
            rpi.renderArea.offset = vk::Offset2D{0, 0};
            rpi.renderArea.extent = vk::Extent2D{m_owner->m_framebuffer->GetWidth(), m_owner->m_framebuffer->GetHeight()};
            rpi.clearValueCount = static_cast<uint32_t>(clearValues.size());
            rpi.pClearValues = clearValues.data();

            vk::SubpassBeginInfo sbi{};
            sbi.contents = vk::SubpassContents::eInline;

            m_apiHandle.beginRenderPass2(rpi, sbi);
        }
    }

    void VulkanCommandBufferImpl::EndPass()
    {
        // in favor of tracking image layout, stage and access, we need to set the correct access mask for the last operation
        // attachments with AttachmentLoadOp::Load will have the ReadBit access mask and it is used for waiting to read/load the attachment
        // but after the render pass is done, the correct access mask should be the WriteBit since it is the last operation
        // so set the correct track info access mask here before any other access operation happen
        for (uint32_t i = 0; i < m_owner->m_attachmentCount; i++)
        {
            const Attachment &attachment = m_owner->m_attachments[i];
            if (attachment.loadOp == PE_LOAD_OP_LOAD)
            {
                attachment.image->m_trackInfos[0][0].accessMask = VulkanHelpers::HasDepth(pe::ToVkFormat(attachment.image->GetFormat()))
                                                                      ? PE_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE
                                                                      : PE_ACCESS_COLOR_ATTACHMENT_WRITE;
            }
        }

        if (m_owner->m_dynamicPass)
            m_apiHandle.endRendering();
        else
            m_apiHandle.endRenderPass();

        EndDebugRegion();

        m_owner->m_attachmentCount = 0;
        m_owner->m_attachments = nullptr;
        m_owner->m_renderPass = nullptr;
        m_owner->m_framebuffer = nullptr;
        m_owner->m_dynamicPass = false;
        m_owner->m_boundPipeline = nullptr;
        m_owner->m_boundVertexBuffer = nullptr;
        m_owner->m_boundVertexBufferOffset = -1;
        m_owner->m_boundVertexBufferFirstBinding = UINT32_MAX;
        m_owner->m_boundVertexBufferBindingCount = UINT32_MAX;
        m_owner->m_boundIndexBuffer = nullptr;
        m_owner->m_boundIndexBufferOffset = -1;
    }

    static void BatchBindDescriptorsVk(vk::CommandBuffer apiHandle, Pipeline *boundPipeline, vk::PipelineBindPoint point, uint32_t count, Descriptor *const *descriptors)
    {
        std::vector<vk::DescriptorSet> dsets;
        dsets.reserve(count);

        uint32_t start = 0;
        while (start < count)
        {
            // skip nulls
            while (start < count && !descriptors[start])
                ++start;

            if (start >= count)
                break;

            dsets.clear();

            uint32_t end = start;
            while (end < count && descriptors[end])
            {
                dsets.push_back(pe::GetVulkanDescriptorSet(descriptors[end]));
                ++end;
            }

            apiHandle.bindDescriptorSets(
                point,
                GetVulkanPipelineLayout(boundPipeline),
                start,
                static_cast<uint32_t>(dsets.size()),
                dsets.data(),
                0, nullptr);

            start = end;
        }
    }

    void VulkanCommandBufferImpl::BindPipeline(PassInfo &passInfo, bool bindDescriptors)
    {
        Pipeline *pipeline = CommandBuffer::GetPipeline(m_owner->m_renderPass, passInfo);
        if (pipeline == m_owner->m_boundPipeline)
            return;

        m_owner->m_boundPipeline = pipeline;

        if (passInfo.pCompShader)
        {
            m_apiHandle.bindPipeline(vk::PipelineBindPoint::eCompute, GetVulkanPipeline(m_owner->m_boundPipeline));
        }
        else if (passInfo.acceleration.rayGen)
        {
            m_apiHandle.bindPipeline(vk::PipelineBindPoint::eRayTracingKHR, GetVulkanPipeline(m_owner->m_boundPipeline));
        }
        else
        {
            m_apiHandle.bindPipeline(vk::PipelineBindPoint::eGraphics, GetVulkanPipeline(m_owner->m_boundPipeline));
        }

        if (bindDescriptors)
        {
            // auto bind descriptor sets
            auto descriptors = passInfo.GetDescriptors(RHII.GetFrameIndex());
            uint32_t count = static_cast<uint32_t>(descriptors.size());
            BindDescriptors(count, descriptors.data());
        }
    }

    void VulkanCommandBufferImpl::BindVertexBuffer(Buffer *buffer, size_t offset, uint32_t firstBinding, uint32_t bindingCount)
    {
        if (m_owner->m_boundVertexBuffer == buffer &&
            m_owner->m_boundVertexBufferOffset == offset &&
            m_owner->m_boundVertexBufferFirstBinding == firstBinding &&
            m_owner->m_boundVertexBufferBindingCount == bindingCount)
            return;

        m_owner->m_boundVertexBuffer = buffer;
        m_owner->m_boundVertexBufferOffset = offset;
        m_owner->m_boundVertexBufferFirstBinding = firstBinding;
        m_owner->m_boundVertexBufferBindingCount = bindingCount;

        vk::Buffer buff = pe::GetVulkanBuffer(buffer);
        m_apiHandle.bindVertexBuffers(firstBinding, bindingCount, &buff, &offset);
    }

    void VulkanCommandBufferImpl::BindIndexBuffer(Buffer *buffer, size_t offset)
    {
        if (m_owner->m_boundIndexBuffer == buffer && m_owner->m_boundIndexBufferOffset == offset)
            return;

        m_owner->m_boundIndexBuffer = buffer;
        m_apiHandle.bindIndexBuffer(pe::GetVulkanBuffer(buffer), offset, vk::IndexType::eUint32);
    }

    void VulkanCommandBufferImpl::BindDescriptors(uint32_t count, Descriptor *const *descriptors)
    {
        PE_ERROR_IF(!m_owner->m_boundPipeline, "CommandBuffer::BindDescriptors: No bound pipeline found!");

        vk::PipelineBindPoint point;
        if (m_owner->m_boundPipeline->m_info.pCompShader)
            point = vk::PipelineBindPoint::eCompute;
        else if (m_owner->m_boundPipeline->m_info.acceleration.rayGen)
            point = vk::PipelineBindPoint::eRayTracingKHR;
        else
            point = vk::PipelineBindPoint::eGraphics;

        BatchBindDescriptorsVk(m_apiHandle, m_owner->m_boundPipeline, point, count, descriptors);
    }

    void VulkanCommandBufferImpl::PushDescriptor(uint32_t set, const std::vector<PushDescriptorInfo> &info)
    {
        PE_ERROR_IF(!m_owner->m_boundPipeline, "CommandBuffer::PushDescriptor: No bound pipeline found!");

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
            writes[i].descriptorType = ToVkDescriptorType(info[i].type);
            if (info[i].views.size() > 0)
            {
                imageInfo[i].resize(info[i].views.size());
                for (uint32_t j = 0; j < info[i].views.size(); j++)
                {
                    imageInfo[i][j] = vk::DescriptorImageInfo{};
                    imageInfo[i][j].imageView = pe::GetVulkanImageView(info[i].views[j]);
                    imageInfo[i][j].imageLayout = ToVkImageLayout(info[i].imageLayout);
                    imageInfo[i][j].sampler = info[i].type == PE_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ? pe::GetVulkanSampler(info[i].samplers[j]) : vk::Sampler{};
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
                    bufferInfo[i][j].buffer = pe::GetVulkanBuffer(info[i].buffers[j]);
                    bufferInfo[i][j].offset = info[i].offsets.size() > 0 ? info[i].offsets[j] : 0;
                    bufferInfo[i][j].range = info[i].ranges.size() > 0 ? info[i].ranges[j] : vk::WholeSize;
                }

                writes[i].descriptorCount = static_cast<uint32_t>(bufferInfo[i].size());
                writes[i].pBufferInfo = bufferInfo[i].data();
            }
            else if (info[i].samplers.size() > 0 && info[i].type != PE_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
            {
                imageInfo[i].resize(info[i].samplers.size());
                for (uint32_t j = 0; j < info[i].samplers.size(); j++)
                {
                    imageInfo[i][j] = vk::DescriptorImageInfo{};
                    imageInfo[i][j].imageView = nullptr;
                    imageInfo[i][j].imageLayout = vk::ImageLayout::eUndefined;
                    imageInfo[i][j].sampler = pe::GetVulkanSampler(info[i].samplers[j]);
                }

                writes[i].descriptorCount = static_cast<uint32_t>(imageInfo[i].size());
                writes[i].pImageInfo = imageInfo[i].data();
            }
            else
            {
                PE_ERROR("[Command] PushDescriptor: No views or buffers or samplers found!");
            }
        }

        // vkCmdPushDescriptorSetKHR is part of VK_KHR_push_descriptor extension, has to be loaded
        static auto vkCmdPushDescriptorSetKHR = (PFN_vkCmdPushDescriptorSetKHR)vkGetDeviceProcAddr(VulkanRhi::Device(), "vkCmdPushDescriptorSetKHR");
        PE_ERROR_IF(!vkCmdPushDescriptorSetKHR, "CommandBuffer::PushDescriptor: vkCmdPushDescriptorSetKHR not found!");

        vkCmdPushDescriptorSetKHR(m_apiHandle,
                                  m_owner->m_boundPipeline->m_info.pCompShader ? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  GetVulkanPipelineLayout(m_owner->m_boundPipeline),
                                  set,
                                  static_cast<uint32_t>(writes.size()),
                                  reinterpret_cast<const VkWriteDescriptorSet *>(writes.data()));
    }

    void VulkanCommandBufferImpl::SetViewport(float x, float y, float width, float height)
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

    void VulkanCommandBufferImpl::SetScissor(int x, int y, uint32_t width, uint32_t height)
    {
        vk::Rect2D scissor{};
        scissor.offset = vk::Offset2D{x, y};
        scissor.extent = vk::Extent2D{width, height};

        m_apiHandle.setScissor(0, 1, &scissor);
    }

    void VulkanCommandBufferImpl::SetLineWidth(float width)
    {
        m_apiHandle.setLineWidth(width);
    }

    void VulkanCommandBufferImpl::SetDepthTestEnable(uint32_t enable)
    {
        m_apiHandle.setDepthTestEnable(enable);
    }

    void VulkanCommandBufferImpl::SetDepthWriteEnable(uint32_t enable)
    {
        m_apiHandle.setDepthWriteEnable(enable);
    }

    void VulkanCommandBufferImpl::Dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
    {
        m_apiHandle.dispatch(groupCountX, groupCountY, groupCountZ);
    }

    void VulkanCommandBufferImpl::PushConstants(const PushConstantsBlock<128> &constants)
    {
        PE_ERROR_IF(!m_owner->m_boundPipeline, "CommandBuffer::PushConstants: No bound pipeline found!");

        const auto &stages = m_owner->m_boundPipeline->m_info.m_pushConstantStages;
        const auto &offsets = m_owner->m_boundPipeline->m_info.m_pushConstantOffsets;
        const auto &sizes = m_owner->m_boundPipeline->m_info.m_pushConstantSizes;

        for (size_t i = 0; i < stages.size(); i++)
        {
            m_apiHandle.pushConstants(GetVulkanPipelineLayout(m_owner->m_boundPipeline),
                                      stages[i],
                                      offsets[i],
                                      sizes[i],
                                      constants.Data());
        }
    }

    void VulkanCommandBufferImpl::Draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance)
    {
        m_apiHandle.draw(vertexCount, instanceCount, firstVertex, firstInstance);
    }

    void VulkanCommandBufferImpl::DrawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance)
    {
        m_apiHandle.drawIndexed(indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
    }

    void VulkanCommandBufferImpl::DrawIndirect(Buffer *indirectBuffer, size_t offset, uint32_t drawCount, uint32_t stride)
    {
        m_apiHandle.drawIndirect(pe::GetVulkanBuffer(indirectBuffer), offset, drawCount, stride);
    }

    void VulkanCommandBufferImpl::DrawIndexedIndirect(Buffer *indirectBuffer, size_t offset, uint32_t drawCount, uint32_t stride)
    {
        m_apiHandle.drawIndexedIndirect(pe::GetVulkanBuffer(indirectBuffer), offset, drawCount, stride);
    }

    void VulkanCommandBufferImpl::DrawIndexedIndirectCount(Buffer *indirectBuffer, size_t offset, Buffer *countBuffer, size_t countBufferOffset, uint32_t maxDrawCount, uint32_t stride)
    {
        m_apiHandle.drawIndexedIndirectCount(pe::GetVulkanBuffer(indirectBuffer), offset, pe::GetVulkanBuffer(countBuffer), countBufferOffset, maxDrawCount, stride);
    }

    void VulkanCommandBufferImpl::FillBuffer(Buffer *buffer, size_t offset, size_t size, uint32_t data)
    {
        m_apiHandle.fillBuffer(pe::GetVulkanBuffer(buffer), offset, size, data);

        // Record the transfer write so the next Buffer::Barrier emits srcStage=Clear,
        // srcAccess=TransferWrite. Without this, subsequent barriers inherit a stale
        // src state and readers at compute stage race with the fill.
        BufferTrackInfo &trackInfo = buffer->GetTrackInfo();
        trackInfo.stageMask = PE_STAGE_CLEAR;
        trackInfo.accessMask = PE_ACCESS_TRANSFER_WRITE;
    }

    void VulkanCommandBufferImpl::TraceRays(uint32_t width, uint32_t height, uint32_t depth)
    {
        PE_ERROR_IF(!m_owner->m_boundPipeline, "CommandBuffer::TraceRays: No bound pipeline found!");
        m_apiHandle.traceRaysKHR(GetVulkanRgenRegion(m_owner->m_boundPipeline),
                                 GetVulkanMissRegion(m_owner->m_boundPipeline),
                                 GetVulkanHitRegion(m_owner->m_boundPipeline),
                                 GetVulkanCallRegion(m_owner->m_boundPipeline),
                                 width, height, depth);
    }

    void VulkanCommandBufferImpl::BuildAccelerationStructures(uint32_t infoCount, const vk::AccelerationStructureBuildGeometryInfoKHR *pInfos, const vk::AccelerationStructureBuildRangeInfoKHR **ppBuildRangeInfos)
    {
        m_apiHandle.buildAccelerationStructuresKHR(infoCount, pInfos, ppBuildRangeInfos);
    }

    void VulkanCommandBufferImpl::BufferBarrier(const BufferBarrierInfo &info)
    {
        Buffer::Barrier(m_owner, info);
    }

    void VulkanCommandBufferImpl::BufferBarriers(const std::vector<BufferBarrierInfo> &infos)
    {
        Buffer::Barriers(m_owner, infos);
    }

    void VulkanCommandBufferImpl::ImageBarrier(const ImageBarrierInfo &info)
    {
        Image::Barrier(m_owner, info);
    }

    void VulkanCommandBufferImpl::ImageBarriers(const std::vector<ImageBarrierInfo> &infos)
    {
        Image::Barriers(m_owner, infos);
    }

    void VulkanCommandBufferImpl::MemoryBarrier(const MemoryBarrierInfo &info)
    {
        vk::MemoryBarrier2 barrier{};
        barrier.srcStageMask = ToVkPipelineStageFlags(info.srcStageMask);
        barrier.srcAccessMask = ToVkAccessFlags(info.srcAccessMask);
        barrier.dstStageMask = ToVkPipelineStageFlags(info.dstStageMask);
        barrier.dstAccessMask = ToVkAccessFlags(info.dstAccessMask);

        vk::DependencyInfo dependencyInfo{};
        dependencyInfo.memoryBarrierCount = 1;
        dependencyInfo.pMemoryBarriers = &barrier;

        m_apiHandle.pipelineBarrier2(dependencyInfo);
    }

    void VulkanCommandBufferImpl::MemoryBarriers(const std::vector<MemoryBarrierInfo> &infos)
    {
        std::vector<vk::MemoryBarrier2> barriers(infos.size());
        for (size_t i = 0; i < infos.size(); ++i)
        {
            barriers[i].srcStageMask = ToVkPipelineStageFlags(infos[i].srcStageMask);
            barriers[i].srcAccessMask = ToVkAccessFlags(infos[i].srcAccessMask);
            barriers[i].dstStageMask = ToVkPipelineStageFlags(infos[i].dstStageMask);
            barriers[i].dstAccessMask = ToVkAccessFlags(infos[i].dstAccessMask);
        }

        vk::DependencyInfo dependencyInfo{};
        dependencyInfo.memoryBarrierCount = static_cast<uint32_t>(barriers.size());
        dependencyInfo.pMemoryBarriers = barriers.data();

        m_apiHandle.pipelineBarrier2(dependencyInfo);
    }

    void VulkanCommandBufferImpl::CopyBuffer(Buffer *src, Buffer *dst, size_t size, size_t srcOffset, size_t dstOffset)
    {
        dst->CopyBuffer(m_owner, src, size, srcOffset, dstOffset);
    }

    void VulkanCommandBufferImpl::CopyBufferStaged(Buffer *buffer, void *data, size_t size, size_t dstOffset)
    {
        buffer->CopyBufferStaged(m_owner, data, size, dstOffset);
    }

    void VulkanCommandBufferImpl::CopyDataToImageStaged(Image *image, void *data, size_t size, uint32_t baseArrayLayer, uint32_t layerCount, uint32_t mipLevel)
    {
        image->CopyDataToImageStaged(m_owner, data, size, baseArrayLayer, layerCount, mipLevel);
    }

    void VulkanCommandBufferImpl::CopyImage(Image *src, Image *dst)
    {
        dst->CopyImage(m_owner, src);
    }

    void VulkanCommandBufferImpl::CopyImageToBuffer(Image *src, Buffer *dst)
    {
        src->CopyToBuffer(m_owner, dst);
    }

    void VulkanCommandBufferImpl::GenerateMipMaps(Image *image)
    {
        image->GenerateMipMaps(m_owner);
    }

    void VulkanCommandBufferImpl::SetEvent(Event *event, Image *image,
                                           PeImageLayout srcLayout, PeImageLayout dstLayout,
                                           PeBarrierSync srcStage, PeBarrierSync dstStage,
                                           PeBarrierAccess srcAccess, PeBarrierAccess dstAccess)
    {
        event->Set(m_owner, image, srcLayout, dstLayout, srcStage, dstStage, srcAccess, dstAccess);
    }

    void VulkanCommandBufferImpl::BeginDebugRegion(const std::string &name)
    {
        Debug::BeginCmdRegion(m_owner, name);
    }

    void VulkanCommandBufferImpl::InsertDebugLabel(const std::string &name)
    {
        Debug::InsertCmdLabel(m_owner, name);
    }

    void VulkanCommandBufferImpl::EndDebugRegion()
    {
        Debug::EndCmdRegion(m_owner);
    }

    CommandBuffer::Impl *CreateCommandBufferImpl(CommandBuffer *owner, CommandPool *commandPool, const std::string &name)
    {
        if (RHII.GetApi() == PE_GRAPHICS_API_DX12)
            return nullptr; // T10b will introduce Dx12CommandBufferImpl; raw bypass continues meanwhile

        return new VulkanCommandBufferImpl(owner, commandPool, name);
    }
} // namespace pe
