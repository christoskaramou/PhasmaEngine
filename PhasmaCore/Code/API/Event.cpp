#include "API/Event.h"
#include "API/RHI.h"
#include "API/Command.h"
#include "API/Image.h"

namespace pe
{
    Event::Event(const std::string &name)
    {
        vk::EventCreateInfo ci{};
        m_apiHandle = RHII.GetDevice().createEvent(ci);

        Debug::SetObjectName(m_apiHandle, name);
    }

    Event::~Event()
    {
        if (m_apiHandle)
        {
            RHII.GetDevice().destroyEvent(m_apiHandle);
            m_apiHandle = nullptr;
        }
    }

    void Event::Set(CommandBuffer *cmd, Image *image,
                    vk::ImageLayout srcLayout, vk::ImageLayout dstLayout,
                    vk::PipelineStageFlags2 srcStage, vk::PipelineStageFlags2 dstStage,
                    vk::AccessFlags2 srcAccess, vk::AccessFlags2 dstAccess)
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
        barrier.srcStageMask = srcStage;
        barrier.srcAccessMask = srcAccess;
        barrier.dstStageMask = dstStage;
        barrier.dstAccessMask = dstAccess;
        barrier.oldLayout = srcLayout;
        barrier.newLayout = dstLayout;
        barrier.srcQueueFamilyIndex = cmd->GetFamilyId();
        barrier.dstQueueFamilyIndex = cmd->GetFamilyId();
        barrier.image = image->ApiHandle();
        barrier.subresourceRange.aspectMask = GetAspectMask(image->GetFormat());
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = image->GetMipLevels();
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = image->GetArrayLayers();

        vk::DependencyInfo depInfo{};
        depInfo.imageMemoryBarrierCount = 1;
        depInfo.pImageMemoryBarriers = &barrier;

        cmd->ApiHandle().setEvent2(m_apiHandle, &depInfo);

        m_set = true;
    }

    void Event::Wait()
    {
        vk::ImageMemoryBarrier2 barrier{};
        barrier.srcStageMask = m_infoImage.srcStage;
        barrier.srcAccessMask = m_infoImage.srcAccess;
        barrier.dstStageMask = m_infoImage.dstStage;
        barrier.dstAccessMask = m_infoImage.dstAccess;
        barrier.oldLayout = m_infoImage.oldLayout;
        barrier.newLayout = m_infoImage.newLayout;
        barrier.srcQueueFamilyIndex = m_cmd->GetFamilyId();
        barrier.dstQueueFamilyIndex = m_cmd->GetFamilyId();
        barrier.image = m_infoImage.image->ApiHandle();
        barrier.subresourceRange.aspectMask = GetAspectMask(m_infoImage.image->GetFormat());
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

        m_cmd->ApiHandle().waitEvents2(1, &m_apiHandle, &depInfo);
    }

    void Event::Reset(vk::PipelineStageFlags2 resetStage)
    {
        m_cmd->ApiHandle().resetEvent2(m_apiHandle, resetStage);
        m_cmd = nullptr;
        m_infoImage = {};
        m_set = false;
    }
}
