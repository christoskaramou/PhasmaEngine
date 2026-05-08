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

        vk::ImageMemoryBarrier2 barrier{};
        barrier.srcStageMask = ToVkPipelineStageFlags(srcStage);
        barrier.srcAccessMask = ToVkAccessFlags(srcAccess);
        barrier.dstStageMask = ToVkPipelineStageFlags(dstStage);
        barrier.dstAccessMask = ToVkAccessFlags(dstAccess);
        barrier.oldLayout = ToVkImageLayout(srcLayout);
        barrier.newLayout = ToVkImageLayout(dstLayout);
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

        m_set = true;
    }

    void Event::Wait()
    {
        vk::ImageMemoryBarrier2 barrier{};
        barrier.srcStageMask = ToVkPipelineStageFlags(m_infoImage.srcStage);
        barrier.srcAccessMask = ToVkAccessFlags(m_infoImage.srcAccess);
        barrier.dstStageMask = ToVkPipelineStageFlags(m_infoImage.dstStage);
        barrier.dstAccessMask = ToVkAccessFlags(m_infoImage.dstAccess);
        barrier.oldLayout = ToVkImageLayout(m_infoImage.oldLayout);
        barrier.newLayout = ToVkImageLayout(m_infoImage.newLayout);
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

        ImageBarrierInfo info{};
        info.image = m_infoImage.image;
        info.layout = m_infoImage.newLayout;
        info.stageFlags = m_infoImage.dstStage;
        info.accessMask = m_infoImage.dstAccess;
        m_infoImage.image->SetCurrentInfoAll(info);

        const vk::Event event = GetVulkanEvent(this);
        GetVulkanCommandBuffer(m_cmd).waitEvents2(1, &event, &depInfo);
    }

    void Event::Reset(PeBarrierSync resetStage)
    {
        GetVulkanCommandBuffer(m_cmd).resetEvent2(GetVulkanEvent(this), ToVkPipelineStageFlags(resetStage));
        m_cmd = nullptr;
        m_infoImage = {};
        m_set = false;
    }
} // namespace pe
