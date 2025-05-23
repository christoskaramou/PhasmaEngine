#ifdef PE_VULKAN
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
        std::vector<VkAttachmentDescription2> attachmentsVK{};
        attachmentsVK.reserve(count);
        std::vector<VkAttachmentReference2> colorReferencesVK{};
        colorReferencesVK.reserve(count);
        VkAttachmentReference2 depthStencilReferenceVK{};
        depthStencilReferenceVK.attachment = VK_ATTACHMENT_UNUSED;

        uint32_t attachmentIndex = 0;
        for (uint32_t i = 0; i < count; i++)
        {
            const Attachment &attachment = attachments[i];
            SampleCount samples = attachment.image->GetSamples();

            VkAttachmentDescription2 attachmentDescription{};
            attachmentDescription.sType = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2;
            attachmentDescription.format = Translate<VkFormat>(attachment.image->GetFormat());
            attachmentDescription.samples = Translate<VkSampleCountFlagBits>(samples);
            attachmentDescription.loadOp = Translate<VkAttachmentLoadOp>(attachment.loadOp);
            attachmentDescription.storeOp = Translate<VkAttachmentStoreOp>(attachment.storeOp);
            attachmentDescription.stencilLoadOp = Translate<VkAttachmentLoadOp>(attachment.stencilLoadOp);
            attachmentDescription.stencilStoreOp = Translate<VkAttachmentStoreOp>(attachment.stencilStoreOp);
            attachmentDescription.initialLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
            attachmentDescription.finalLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
            attachmentsVK.push_back(attachmentDescription);

            if (HasDepth(attachment.image->GetFormat()))
            {
                depthStencilReferenceVK.sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2;
                depthStencilReferenceVK.attachment = attachmentIndex++;
                depthStencilReferenceVK.aspectMask = GetAspectMask(attachment.image->GetFormat());
                depthStencilReferenceVK.layout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
            }
            else
            {
                VkAttachmentReference2 reference{};
                reference.sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2;
                reference.attachment = attachmentIndex++;
                reference.layout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
                reference.aspectMask = GetAspectMask(attachment.image->GetFormat());
                colorReferencesVK.push_back(reference);
            }
        }

        VkSubpassDescription2 subpassDescription{};
        subpassDescription.sType = VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2;
        subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpassDescription.colorAttachmentCount = static_cast<uint32_t>(colorReferencesVK.size());
        subpassDescription.pColorAttachments = colorReferencesVK.data();
        subpassDescription.pDepthStencilAttachment = depthStencilReferenceVK.attachment != VK_ATTACHMENT_UNUSED ? &depthStencilReferenceVK : nullptr;

        VkPipelineStageFlags srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        VkPipelineStageFlags dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        VkAccessFlags srcAccessMask = VK_ACCESS_NONE;
        VkAccessFlags dstAccessMask = VK_ACCESS_NONE;
        if (!colorReferencesVK.empty())
        {
            srcStageMask |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dstStageMask |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dstAccessMask |= VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        }
        if (depthStencilReferenceVK.attachment != VK_ATTACHMENT_UNUSED)
        {
            srcStageMask |= VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            dstStageMask |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
            dstAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        }

        VkSubpassDependency2 subpassDependencies[2];

        subpassDependencies[0] = {};
        subpassDependencies[0].sType = VK_STRUCTURE_TYPE_SUBPASS_DEPENDENCY_2;
        subpassDependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        subpassDependencies[0].dstSubpass = 0;
        subpassDependencies[0].srcStageMask = srcStageMask;
        subpassDependencies[0].dstStageMask = dstStageMask;
        subpassDependencies[0].srcAccessMask = srcAccessMask;
        subpassDependencies[0].dstAccessMask = dstAccessMask;
        subpassDependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        subpassDependencies[1] = {};
        subpassDependencies[1].sType = VK_STRUCTURE_TYPE_SUBPASS_DEPENDENCY_2;
        subpassDependencies[1].srcSubpass = 0;
        subpassDependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        subpassDependencies[1].srcStageMask = dstStageMask;
        subpassDependencies[1].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        subpassDependencies[1].srcAccessMask = dstAccessMask;
        subpassDependencies[1].dstAccessMask = VK_ACCESS_NONE;
        subpassDependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        VkRenderPassCreateInfo2 renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2;
        renderPassInfo.attachmentCount = static_cast<uint32_t>(attachmentsVK.size());
        renderPassInfo.pAttachments = attachmentsVK.data();
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpassDescription;
        renderPassInfo.dependencyCount = 2;
        renderPassInfo.pDependencies = subpassDependencies;

        VkRenderPass renderPass;
        PE_CHECK(vkCreateRenderPass2(RHII.GetDevice(), &renderPassInfo, nullptr, &renderPass));
        m_apiHandle = renderPass;

        Debug::SetObjectName(m_apiHandle, name);
    }

    RenderPass::~RenderPass()
    {
        if (m_apiHandle)
        {
            vkDestroyRenderPass(RHII.GetDevice(), m_apiHandle, nullptr);
            m_apiHandle = {};
        }
    }
}
#endif
