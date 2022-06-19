/*
Copyright (c) 2018-2021 Christos Karamoustos

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

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
        finalLayout = ImageLayout::ColorAttachment;
    }

    RenderPass::RenderPass(Attachment *attachments, uint32_t count, const std::string &name)
    {
        this->attachments = std::vector<Attachment>(count);

        std::vector<VkAttachmentDescription> attachmentsVK{};
        std::vector<VkAttachmentReference> colorReferencesVK{};
        VkAttachmentReference depthReferenceVK{};

        bool hasDepth = false;
        uint32_t attachmentIndex = 0;
        for (uint32_t i = 0; i < count; i++)
        {
            this->attachments[i] = attachments[i];
            Attachment &attachment = attachments[i];
            VkAttachmentDescription attachmentDescription{};

            attachmentDescription.format = GetFormatVK(attachment.format);
            attachmentDescription.samples = GetSampleCountVK(attachment.samples);
            attachmentDescription.loadOp = GetAttachmentLoadOpVK(attachment.loadOp);
            attachmentDescription.storeOp = GetAttachmentStoreOpVK(attachment.storeOp);
            attachmentDescription.stencilLoadOp = GetAttachmentLoadOpVK(attachment.stencilLoadOp);
            attachmentDescription.stencilStoreOp = GetAttachmentStoreOpVK(attachment.stencilStoreOp);
            attachmentDescription.initialLayout = GetImageLayoutVK(attachment.initialLayout);
            attachmentDescription.finalLayout = GetImageLayoutVK(attachment.finalLayout);
            attachmentsVK.push_back(attachmentDescription);

            if (!IsDepthFormat(attachment.format))
            {
                VkAttachmentReference reference{attachmentIndex++, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
                colorReferencesVK.push_back(reference);
            }
            else
            {
                // Only one depth reference sould make it in here, else there will be an overwrite
                depthReferenceVK = {attachmentIndex++, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
                hasDepth = true;
            }
        }

        VkSubpassDescription subpassDescription{};
        subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpassDescription.colorAttachmentCount = static_cast<uint32_t>(colorReferencesVK.size());
        subpassDescription.pColorAttachments = colorReferencesVK.data();
        subpassDescription.pDepthStencilAttachment = hasDepth ? &depthReferenceVK : nullptr;

        VkRenderPassCreateInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = static_cast<uint32_t>(attachmentsVK.size());
        renderPassInfo.pAttachments = attachmentsVK.data();
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpassDescription;

        VkRenderPass renderPass;
        PE_CHECK(vkCreateRenderPass(RHII.GetDevice(), &renderPassInfo, nullptr, &renderPass));
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
