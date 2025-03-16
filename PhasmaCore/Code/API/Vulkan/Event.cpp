#ifdef PE_VULKAN
#include "API/Event.h"
#include "API/RHI.h"
#include "API/Command.h"
#include "API/Image.h"

namespace pe
{
    Event::Event(const std::string &name)
    {
        VkEventCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_EVENT_CREATE_INFO;

        VkEvent event;
        PE_CHECK(vkCreateEvent(RHII.GetDevice(), &ci, nullptr, &event));
        m_apiHandle = event;

        Debug::SetObjectName(m_apiHandle, name);
    }

    Event::~Event()
    {
        if (m_apiHandle)
        {
            vkDestroyEvent(RHII.GetDevice(), m_apiHandle, nullptr);
            m_apiHandle = {};
        }
    }

    void Event::Set(CommandBuffer *cmd, Image *image,
                    ImageLayout scrLayout, ImageLayout dstLayout,
                    PipelineStageFlags srcStage, PipelineStageFlags dstStage,
                    AccessFlags srcAccess, AccessFlags dstAccess)
    {
        m_cmd = cmd;
        m_infoImage.image = image;
        m_infoImage.oldLayout = scrLayout;
        m_infoImage.newLayout = dstLayout;
        m_infoImage.srcStage = srcStage;
        m_infoImage.srcAccess = srcAccess;
        m_infoImage.dstStage = dstStage;
        m_infoImage.dstAccess = dstAccess;

        VkImageMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier.srcStageMask = Translate<VkPipelineStageFlags2>(srcStage);
        barrier.srcAccessMask = Translate<VkAccessFlags2>(srcAccess);
        barrier.dstStageMask = Translate<VkPipelineStageFlags2>(dstStage);
        barrier.dstAccessMask = Translate<VkAccessFlags2>(dstAccess);
        barrier.oldLayout = Translate<VkImageLayout>(scrLayout);
        barrier.newLayout = Translate<VkImageLayout>(dstLayout);
        barrier.srcQueueFamilyIndex = cmd->GetFamilyId();
        barrier.dstQueueFamilyIndex = cmd->GetFamilyId();
        barrier.image = image->ApiHandle();
        barrier.subresourceRange.aspectMask = Translate<VkImageAspectFlags>(GetAspectMask(image->GetFormat()));
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = image->GetMipLevels();
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = image->GetArrayLayers();

        VkDependencyInfo depInfo{};
        depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        depInfo.imageMemoryBarrierCount = 1;
        depInfo.pImageMemoryBarriers = &barrier;

        vkCmdSetEvent2(cmd->ApiHandle(), m_apiHandle, &depInfo);

        m_set = true;
    }

    void Event::Wait()
    {
        VkImageMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier.srcStageMask = Translate<VkPipelineStageFlags2>(m_infoImage.srcStage);
        barrier.srcAccessMask = Translate<VkAccessFlags2>(m_infoImage.srcAccess);
        barrier.dstStageMask = Translate<VkPipelineStageFlags2>(m_infoImage.dstStage);
        barrier.dstAccessMask = Translate<VkAccessFlags2>(m_infoImage.dstAccess);
        barrier.oldLayout = Translate<VkImageLayout>(m_infoImage.oldLayout);
        barrier.newLayout = Translate<VkImageLayout>(m_infoImage.newLayout);
        barrier.srcQueueFamilyIndex = m_cmd->GetFamilyId();
        barrier.dstQueueFamilyIndex = m_cmd->GetFamilyId();
        barrier.image = m_infoImage.image->ApiHandle();
        barrier.subresourceRange.aspectMask = Translate<VkImageAspectFlags>(GetAspectMask(m_infoImage.image->GetFormat()));
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = m_infoImage.image->GetMipLevels();
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = m_infoImage.image->GetArrayLayers();

        VkDependencyInfo depInfo{};
        depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        depInfo.imageMemoryBarrierCount = 1;
        depInfo.pImageMemoryBarriers = &barrier;
        
        ImageBarrierInfo info{};
        info.image = m_infoImage.image;
        info.layout = m_infoImage.newLayout;
        info.stageFlags = m_infoImage.dstStage;
        info.accessMask = m_infoImage.dstAccess;
        m_infoImage.image->SetCurrentInfoAll(info);

        VkEvent eventVK = m_apiHandle;
        vkCmdWaitEvents2(m_cmd->ApiHandle(), 1, &eventVK, &depInfo);
    }

    void Event::Reset(PipelineStageFlags resetStage)
    {
        vkCmdResetEvent2(m_cmd->ApiHandle(), m_apiHandle, Translate<VkPipelineStageFlags2>(resetStage));
        m_cmd = nullptr;
        m_infoImage = {};
        m_set = false;
    }
}
#endif
