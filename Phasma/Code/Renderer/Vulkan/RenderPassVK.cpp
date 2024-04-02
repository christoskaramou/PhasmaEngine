#if PE_VULKAN
#include "Renderer/RenderPass.h"
#include "Renderer/RHI.h"
#include "Renderer/Image.h"

namespace pe
{
    // Target attachments are initialized to match render targets by default
    Attachment::Attachment()
    {
        image = nullptr;
        loadOp = AttachmentLoadOp::Clear;
        storeOp = AttachmentStoreOp::Store;
        stencilLoadOp = AttachmentLoadOp::DontCare;
        stencilStoreOp = AttachmentStoreOp::DontCare;
        initialLayout = ImageLayout::Undefined;
        finalLayout = ImageLayout::Attachment;
    }


    // Attachment references must match the attachment number and order in shader
    RenderPass::RenderPass(const std::vector<Attachment> &attachments, const std::string &name)
    {
        size_t count = attachments.size();

        std::vector<VkAttachmentDescription2> attachmentsVK{};
        attachmentsVK.reserve(count);
        std::vector<VkAttachmentReference2> colorReferencesVK{};
        colorReferencesVK.reserve(count);
        VkAttachmentReference2 depthStencilReferenceVK{};
        depthStencilReferenceVK.attachment = VK_ATTACHMENT_UNUSED;

        uint32_t attachmentIndex = 0;
        for (const auto &attachment : attachments)
        {
            Format format = attachment.image->GetFormat();
            SampleCount samples = attachment.image->GetSamples();

            VkAttachmentDescription2 attachmentDescription{};
            attachmentDescription.sType = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2;
            attachmentDescription.format = Translate<VkFormat>(format);
            attachmentDescription.samples = Translate<VkSampleCountFlagBits>(samples);
            attachmentDescription.loadOp = Translate<VkAttachmentLoadOp>(attachment.loadOp);
            attachmentDescription.storeOp = Translate<VkAttachmentStoreOp>(attachment.storeOp);
            attachmentDescription.stencilLoadOp = Translate<VkAttachmentLoadOp>(attachment.stencilLoadOp);
            attachmentDescription.stencilStoreOp = Translate<VkAttachmentStoreOp>(attachment.stencilStoreOp);
            attachmentDescription.initialLayout = Translate<VkImageLayout>(attachment.initialLayout);
            attachmentDescription.finalLayout = Translate<VkImageLayout>(attachment.finalLayout);
            attachmentsVK.push_back(attachmentDescription);

            if (IsDepthFormat(format))
            {
                depthStencilReferenceVK.sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2;
                depthStencilReferenceVK.attachment = attachmentIndex++;
                depthStencilReferenceVK.layout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
            }
            else
            {
                VkAttachmentReference2 reference{};
                reference.sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2;
                reference.attachment = attachmentIndex++;
                reference.layout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
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
        subpassDependencies[1].dstStageMask = VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
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
        m_handle = renderPass;

        m_name = name;
        Debug::SetObjectName(m_handle, name);
    }

    RenderPass::~RenderPass()
    {
        if (m_handle)
        {
            vkDestroyRenderPass(RHII.GetDevice(), m_handle, nullptr);
            m_handle = {};
        }
    }
}
#endif
