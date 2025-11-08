#include "API/RenderPass.h"
#include "API/RHI.h"
#include "API/Image.h"
#include "API/Command.h"

namespace pe
{
    // Attachment references must match the attachment number and order in shader
    RenderPass::RenderPass(uint32_t count, Attachment *attachments, const std::string &name)
        : m_name{name}
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
            attachmentDescription.format = attachment.image->GetFormat();
            attachmentDescription.samples = attachment.image->GetSamples();
            attachmentDescription.loadOp = attachment.loadOp;
            attachmentDescription.storeOp = attachment.storeOp;
            attachmentDescription.stencilLoadOp = attachment.stencilLoadOp;
            attachmentDescription.stencilStoreOp = attachment.stencilStoreOp;
            attachmentDescription.initialLayout = vk::ImageLayout::eAttachmentOptimal;
            attachmentDescription.finalLayout = vk::ImageLayout::eAttachmentOptimal;
            attachmentsVK.push_back(attachmentDescription);

            if (VulkanHelpers::HasDepthOrStencil(attachment.image->GetFormat()))
            {
                depthStencilReferenceVK.attachment = attachmentIndex++;
                depthStencilReferenceVK.aspectMask = VulkanHelpers::GetAspectMask(attachment.image->GetFormat());
                depthStencilReferenceVK.layout = vk::ImageLayout::eAttachmentOptimal;
            }
            else
            {
                vk::AttachmentReference2 reference{};
                reference.attachment = attachmentIndex++;
                reference.layout = vk::ImageLayout::eAttachmentOptimal;
                reference.aspectMask = VulkanHelpers::GetAspectMask(attachment.image->GetFormat());
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

        m_apiHandle = RHII.GetDevice().createRenderPass2(renderPassInfo);

        Debug::SetObjectName(m_apiHandle, name);
    }

    RenderPass::~RenderPass()
    {
        if (m_apiHandle)
            RHII.GetDevice().destroyRenderPass(m_apiHandle);
    }
}
