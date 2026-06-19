#include "API/Vulkan/VulkanRenderPassImpl.h"
#include "API/Command.h"
#include "API/Image.h"
#include "API/Vulkan/Helpers_Vulkan.h"
#include "API/Vulkan/RHI_Vulkan.h"
#include "API/Vulkan/VulkanImageImpl.h"
#include "API/Vulkan/VulkanRHITypeUtils.h"

namespace pe
{
    // Attachment references must match the attachment number and order in shader
    VulkanRenderPassImpl::VulkanRenderPassImpl(RenderPass *owner, uint32_t count, Attachment *attachments, const std::string &name)
        : m_owner{owner}
    {
        std::vector<vk::AttachmentDescription2> attachmentsVK{};
        attachmentsVK.reserve(count);
        std::vector<vk::AttachmentReference2> colorReferencesVK{};
        colorReferencesVK.reserve(count);
        vk::AttachmentReference2 depthStencilReferenceVK{};
        depthStencilReferenceVK.attachment = vk::AttachmentUnused;

        uint32_t attachmentIndex = 0;
        for (uint32_t i = 0; i < count; i++)
        {
            const Attachment &attachment = attachments[i];

            vk::AttachmentDescription2 attachmentDescription{};
            attachmentDescription.format = pe::ToVkFormat(attachment.image->GetFormat());
            attachmentDescription.samples = pe::ToVkSampleCount(attachment.image->GetSamples());
            attachmentDescription.loadOp = ToVkLoadOp(attachment.loadOp);
            attachmentDescription.storeOp = ToVkStoreOp(attachment.storeOp);
            attachmentDescription.stencilLoadOp = ToVkLoadOp(attachment.stencilLoadOp);
            attachmentDescription.stencilStoreOp = ToVkStoreOp(attachment.stencilStoreOp);

            const vk::Format vkFormat = pe::ToVkFormat(attachment.image->GetFormat());
            if (VulkanHelpers::HasDepthOrStencil(vkFormat))
            {
                attachmentDescription.initialLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
                attachmentDescription.finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
                attachmentsVK.push_back(attachmentDescription);

                depthStencilReferenceVK.attachment = attachmentIndex++;
                depthStencilReferenceVK.aspectMask = VulkanHelpers::GetAspectMask(vkFormat);
                depthStencilReferenceVK.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
            }
            else
            {
                attachmentDescription.initialLayout = vk::ImageLayout::eColorAttachmentOptimal;
                attachmentDescription.finalLayout = vk::ImageLayout::eColorAttachmentOptimal;
                attachmentsVK.push_back(attachmentDescription);

                vk::AttachmentReference2 reference{};
                reference.attachment = attachmentIndex++;
                reference.layout = vk::ImageLayout::eColorAttachmentOptimal;
                reference.aspectMask = VulkanHelpers::GetAspectMask(vkFormat);
                colorReferencesVK.push_back(reference);
            }
        }

        vk::SubpassDescription2 subpassDescription{};
        subpassDescription.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
        subpassDescription.colorAttachmentCount = static_cast<uint32_t>(colorReferencesVK.size());
        subpassDescription.pColorAttachments = colorReferencesVK.data();
        subpassDescription.pDepthStencilAttachment = depthStencilReferenceVK.attachment != vk::AttachmentUnused ? &depthStencilReferenceVK : nullptr;

        vk::PipelineStageFlags srcStageMask = vk::PipelineStageFlagBits::eTopOfPipe;
        vk::PipelineStageFlags dstStageMask = vk::PipelineStageFlagBits::eBottomOfPipe;
        vk::AccessFlags srcAccessMask = vk::AccessFlagBits::eNone;
        vk::AccessFlags dstAccessMask = vk::AccessFlagBits::eNone;
        if (!colorReferencesVK.empty())
        {
            srcStageMask |= vk::PipelineStageFlagBits::eColorAttachmentOutput;
            dstStageMask |= vk::PipelineStageFlagBits::eColorAttachmentOutput;
            dstAccessMask |= vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;
        }
        if (depthStencilReferenceVK.attachment != vk::AttachmentUnused)
        {
            srcStageMask |= vk::PipelineStageFlagBits::eLateFragmentTests;
            dstStageMask |= vk::PipelineStageFlagBits::eEarlyFragmentTests;
            dstAccessMask |= vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite;
        }

        vk::SubpassDependency2 subpassDependencies[2];

        subpassDependencies[0] = vk::SubpassDependency2{};
        subpassDependencies[0].srcSubpass = vk::SubpassExternal;
        subpassDependencies[0].dstSubpass = 0;
        subpassDependencies[0].srcStageMask = srcStageMask;
        subpassDependencies[0].dstStageMask = dstStageMask;
        subpassDependencies[0].srcAccessMask = srcAccessMask;
        subpassDependencies[0].dstAccessMask = dstAccessMask;
        subpassDependencies[0].dependencyFlags = vk::DependencyFlagBits::eByRegion;

        subpassDependencies[1] = vk::SubpassDependency2{};
        subpassDependencies[1].sType = vk::StructureType::eSubpassDependency2;
        subpassDependencies[1].srcSubpass = 0;
        subpassDependencies[1].dstSubpass = vk::SubpassExternal;
        subpassDependencies[1].srcStageMask = dstStageMask;
        subpassDependencies[1].dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        subpassDependencies[1].srcAccessMask = dstAccessMask;
        subpassDependencies[1].dstAccessMask = vk::AccessFlagBits::eNone;
        subpassDependencies[1].dependencyFlags = vk::DependencyFlagBits::eByRegion;

        vk::RenderPassCreateInfo2 renderPassInfo{};
        renderPassInfo.attachmentCount = static_cast<uint32_t>(attachmentsVK.size());
        renderPassInfo.pAttachments = attachmentsVK.data();
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpassDescription;
        renderPassInfo.dependencyCount = 2;
        renderPassInfo.pDependencies = subpassDependencies;

        m_apiHandle = VulkanRhi::Device().createRenderPass2(renderPassInfo);

        Debug::SetObjectName(m_apiHandle, name);
    }

    VulkanRenderPassImpl::~VulkanRenderPassImpl()
    {
        if (m_apiHandle)
            VulkanRhi::Device().destroyRenderPass(m_apiHandle);
    }
} // namespace pe
