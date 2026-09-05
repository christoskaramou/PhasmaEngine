#include "API/Event.h"
#include "API/Command.h"
#include "API/Image.h"
#include "API/RHI.h"
#include "API/Vulkan/RHI_Vulkan.h"
#include "API/Vulkan/Helpers_Vulkan.h"
#include "API/Vulkan/VulkanCommandBufferImpl.h"
#include "API/Vulkan/VulkanEventImpl.h"
#include "API/Vulkan/VulkanImageImpl.h"
#include "API/Vulkan/VulkanRHITypeUtils.h"

namespace pe
{
    namespace
    {
        vk::ImageLayout ToVkImageLayoutForEvent(PeImageLayout layout, Image *image)
        {
            if (layout == PE_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL && image)
            {
                const vk::Format format = pe::ToVkFormat(image->GetFormat());
                return VulkanHelpers::HasDepthOrStencil(format)
                           ? vk::ImageLayout::eDepthStencilAttachmentOptimal
                           : vk::ImageLayout::eColorAttachmentOptimal;
            }
            return ToVkImageLayout(layout);
        }
    } // namespace

    VulkanEventImpl::VulkanEventImpl(const std::string &name)
    {
        vk::EventCreateInfo ci{};
        m_apiHandle = VulkanRhi::Device().createEvent(ci);

        Debug::SetObjectName(m_apiHandle, name);
    }

    VulkanEventImpl::~VulkanEventImpl()
    {
        if (m_apiHandle)
            VulkanRhi::Device().destroyEvent(m_apiHandle);
    }

    Event::Event(const std::string &name)
    {
        m_impl = new VulkanEventImpl(name);
    }

    Event::~Event()
    {
        delete m_impl;
        m_impl = nullptr;
    }

    void Event::Set(CommandBuffer *cmd, Image *image,
                    PeImageLayout srcLayout, PeImageLayout dstLayout,
                    PeBarrierSync srcStage, PeBarrierSync dstStage,
                    PeBarrierAccess srcAccess, PeBarrierAccess dstAccess)
    {
        m_cmd = cmd;
        m_infoImage.image = image;
        m_infoImage.oldLayout = srcLayout;
        m_infoImage.newLayout = dstLayout;
        m_infoImage.srcStage = srcStage;
        m_infoImage.srcAccess = srcAccess;
        m_infoImage.dstStage = dstStage;
        m_infoImage.dstAccess = dstAccess;

        // Batched barriers must land before the event signal, not after it.
        VulkanCommandBufferImpl::From(cmd)->FlushBarriers();

        if (RHII.GetCaps().sync2)
        {
            vk::ImageMemoryBarrier2 barrier{};
            barrier.srcStageMask = ToVkPipelineStageFlags(srcStage);
            barrier.srcAccessMask = ToVkAccessFlags(srcAccess);
            barrier.dstStageMask = ToVkPipelineStageFlags(dstStage);
            barrier.dstAccessMask = ToVkAccessFlags(dstAccess);
            barrier.oldLayout = ToVkImageLayoutForEvent(srcLayout, image);
            barrier.newLayout = ToVkImageLayoutForEvent(dstLayout, image);
            barrier.srcQueueFamilyIndex = cmd->GetFamilyId();
            barrier.dstQueueFamilyIndex = cmd->GetFamilyId();
            barrier.image = pe::GetVulkanImage(image);
            barrier.subresourceRange.aspectMask = VulkanHelpers::GetAspectMask(pe::ToVkFormat(image->GetFormat()));
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = image->GetMipLevels();
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = image->GetArrayLayers();

            vk::DependencyInfo depInfo{};
            depInfo.imageMemoryBarrierCount = 1;
            depInfo.pImageMemoryBarriers = &barrier;

            GetVulkanCommandBuffer(cmd).setEvent2(GetVulkanEvent(this), &depInfo);
        }
        else
        {
            vk::PipelineStageFlags legacyStage = ToVkPipelineStageFlagsLegacy(srcStage);
            if (!legacyStage)
                legacyStage = vk::PipelineStageFlagBits::eTopOfPipe;
            GetVulkanCommandBuffer(cmd).setEvent(GetVulkanEvent(this), legacyStage);
        }

        m_set = true;
    }

    void Event::Wait()
    {
        ImageBarrierInfo info{};
        info.image = m_infoImage.image;
        info.layout = m_infoImage.newLayout;
        info.stageFlags = m_infoImage.dstStage;
        info.accessMask = m_infoImage.dstAccess;
        m_infoImage.image->SetCurrentInfoAll(info);

        const vk::Event event = GetVulkanEvent(this);
        if (RHII.GetCaps().sync2)
        {
            vk::ImageMemoryBarrier2 barrier{};
            barrier.srcStageMask = ToVkPipelineStageFlags(m_infoImage.srcStage);
            barrier.srcAccessMask = ToVkAccessFlags(m_infoImage.srcAccess);
            barrier.dstStageMask = ToVkPipelineStageFlags(m_infoImage.dstStage);
            barrier.dstAccessMask = ToVkAccessFlags(m_infoImage.dstAccess);
            barrier.oldLayout = ToVkImageLayoutForEvent(m_infoImage.oldLayout, m_infoImage.image);
            barrier.newLayout = ToVkImageLayoutForEvent(m_infoImage.newLayout, m_infoImage.image);
            barrier.srcQueueFamilyIndex = m_cmd->GetFamilyId();
            barrier.dstQueueFamilyIndex = m_cmd->GetFamilyId();
            barrier.image = pe::GetVulkanImage(m_infoImage.image);
            barrier.subresourceRange.aspectMask = VulkanHelpers::GetAspectMask(pe::ToVkFormat(m_infoImage.image->GetFormat()));
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = m_infoImage.image->GetMipLevels();
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = m_infoImage.image->GetArrayLayers();

            vk::DependencyInfo depInfo{};
            depInfo.imageMemoryBarrierCount = 1;
            depInfo.pImageMemoryBarriers = &barrier;

            GetVulkanCommandBuffer(m_cmd).waitEvents2(1, &event, &depInfo);
        }
        else
        {
            vk::ImageMemoryBarrier barrier{};
            barrier.srcAccessMask = ToVkAccessFlagsLegacy(m_infoImage.srcAccess);
            barrier.dstAccessMask = ToVkAccessFlagsLegacy(m_infoImage.dstAccess);
            barrier.oldLayout = ToVkImageLayoutForEvent(m_infoImage.oldLayout, m_infoImage.image);
            barrier.newLayout = ToVkImageLayoutForEvent(m_infoImage.newLayout, m_infoImage.image);
            barrier.srcQueueFamilyIndex = m_cmd->GetFamilyId();
            barrier.dstQueueFamilyIndex = m_cmd->GetFamilyId();
            barrier.image = pe::GetVulkanImage(m_infoImage.image);
            barrier.subresourceRange.aspectMask = VulkanHelpers::GetAspectMask(pe::ToVkFormat(m_infoImage.image->GetFormat()));
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = m_infoImage.image->GetMipLevels();
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = m_infoImage.image->GetArrayLayers();

            vk::PipelineStageFlags srcStageMask = ToVkPipelineStageFlagsLegacy(m_infoImage.srcStage);
            vk::PipelineStageFlags dstStageMask = ToVkPipelineStageFlagsLegacy(m_infoImage.dstStage);
            if (!srcStageMask)
                srcStageMask = vk::PipelineStageFlagBits::eTopOfPipe;
            if (!dstStageMask)
                dstStageMask = vk::PipelineStageFlagBits::eBottomOfPipe;
            GetVulkanCommandBuffer(m_cmd).waitEvents(1, &event, srcStageMask, dstStageMask, 0, nullptr, 0, nullptr, 1, &barrier);
        }
    }

    void Event::Reset(PeBarrierSync resetStage)
    {
        if (RHII.GetCaps().sync2)
        {
            GetVulkanCommandBuffer(m_cmd).resetEvent2(GetVulkanEvent(this), ToVkPipelineStageFlags(resetStage));
        }
        else
        {
            vk::PipelineStageFlags stageMask = ToVkPipelineStageFlagsLegacy(resetStage);
            if (!stageMask)
                stageMask = vk::PipelineStageFlagBits::eTopOfPipe;
            GetVulkanCommandBuffer(m_cmd).resetEvent(GetVulkanEvent(this), stageMask);
        }
        m_cmd = nullptr;
        m_infoImage = {};
        m_set = false;
    }
} // namespace pe
