#if PE_VULKAN
#include "Renderer/RenderPass.h"
#include "Renderer/RHI.h"

namespace pe
{
    // Target attachments are initialized to match render targets by default
    Attachment::Attachment()
    {
        flags = {};
        format = Format::Undefined;
        samples = SampleCount::Count1;
        loadOp = AttachmentLoadOp::Clear;
        storeOp = AttachmentStoreOp::Store;
        stencilLoadOp = AttachmentLoadOp::DontCare;
        stencilStoreOp = AttachmentStoreOp::DontCare;
        initialLayout = ImageLayout::Undefined;
        finalLayout = ImageLayout::Attachment;
    }

    RenderPass::RenderPass(uint32_t count, Attachment *attachments, const std::string &name)
    {
        this->attachments = std::vector<Attachment>(count);

        std::vector<VkAttachmentDescription2> attachmentsVK{};
        attachmentsVK.reserve(count);

        std::vector<VkAttachmentReference2> colorReferencesVK{};
        colorReferencesVK.reserve(count); // may be 1 less if depth is included

        VkAttachmentReference2 depthReferenceVK{};

        bool hasDepth = false;
        uint32_t attachmentIndex = 0;
        for (uint32_t i = 0; i < count; i++)
        {
            this->attachments[i] = attachments[i];
            Attachment &attachment = attachments[i];
            VkAttachmentDescription2 attachmentDescription{};
            attachmentDescription.sType = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2;
            attachmentDescription.format = Translate<VkFormat>(attachment.format);
            attachmentDescription.samples = Translate<VkSampleCountFlagBits>(attachment.samples);
            attachmentDescription.loadOp = Translate<VkAttachmentLoadOp>(attachment.loadOp);
            attachmentDescription.storeOp = Translate<VkAttachmentStoreOp>(attachment.storeOp);
            attachmentDescription.stencilLoadOp = Translate<VkAttachmentLoadOp>(attachment.stencilLoadOp);
            attachmentDescription.stencilStoreOp = Translate<VkAttachmentStoreOp>(attachment.stencilStoreOp);
            attachmentDescription.initialLayout = Translate<VkImageLayout>(attachment.initialLayout);
            attachmentDescription.finalLayout = Translate<VkImageLayout>(attachment.finalLayout);

            attachmentsVK.push_back(attachmentDescription);

            if (!IsDepthFormat(attachment.format))
            {
                VkAttachmentReference2 reference{};
                reference.sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2;
                reference.attachment = attachmentIndex++;
                reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                colorReferencesVK.push_back(reference);
            }
            else
            {
                // Only one depth reference sould make it in here, else there will be an overwrite
                depthReferenceVK.sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2;
                depthReferenceVK.attachment = attachmentIndex++;
                depthReferenceVK.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                hasDepth = true;
            }
        }

        VkSubpassDescription2 subpassDescription{};
        subpassDescription.sType = VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2;
        subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpassDescription.colorAttachmentCount = static_cast<uint32_t>(colorReferencesVK.size());
        subpassDescription.pColorAttachments = colorReferencesVK.data();
        subpassDescription.pDepthStencilAttachment = hasDepth ? &depthReferenceVK : nullptr;

        VkRenderPassCreateInfo2 renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2;
        renderPassInfo.attachmentCount = static_cast<uint32_t>(attachmentsVK.size());
        renderPassInfo.pAttachments = attachmentsVK.data();
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpassDescription;

        VkRenderPass renderPass;
        PE_CHECK(vkCreateRenderPass2(RHII.GetDevice(), &renderPassInfo, nullptr, &renderPass));
        m_handle = renderPass;

        Debug::SetObjectName(m_handle, ObjectType::RenderPass, name);
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
