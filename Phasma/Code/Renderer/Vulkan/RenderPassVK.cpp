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
        format = VK_FORMAT_UNDEFINED;
        samples = VK_SAMPLE_COUNT_1_BIT;
        loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }

    bool IsDepthFormat(Format format)
    {
        return format == VK_FORMAT_D32_SFLOAT_S8_UINT ||
               format == VK_FORMAT_D32_SFLOAT ||
               format == VK_FORMAT_D24_UNORM_S8_UINT;
    }

    RenderPass::RenderPass(Attachment *attachments, uint32_t count, const std::string &name)
    {
        this->attachments = std::vector<Attachment>(count);

        std::vector<VkAttachmentDescription> _attachments{};
        std::vector<VkAttachmentReference> colorReferences{};
        VkAttachmentReference depthReference{};

        bool hasDepth = false;
        uint32_t attachmentIndex = 0;
        for (uint32_t i = 0; i < count; i++)
        {
            this->attachments[i] = attachments[i];
            Attachment &attachment = attachments[i];
            VkAttachmentDescription attachmentDescription{};

            attachmentDescription.format = (VkFormat)attachment.format;
            attachmentDescription.samples = (VkSampleCountFlagBits)attachment.samples;
            attachmentDescription.loadOp = (VkAttachmentLoadOp)attachment.loadOp;
            attachmentDescription.storeOp = (VkAttachmentStoreOp)attachment.storeOp;
            attachmentDescription.stencilLoadOp = (VkAttachmentLoadOp)attachment.stencilLoadOp;
            attachmentDescription.stencilStoreOp = (VkAttachmentStoreOp)attachment.stencilStoreOp;
            attachmentDescription.initialLayout = (VkImageLayout)attachment.initialLayout;
            attachmentDescription.finalLayout = (VkImageLayout)attachment.finalLayout;
            _attachments.push_back(attachmentDescription);

            if (!IsDepthFormat(attachment.format))
            {
                VkAttachmentReference reference{attachmentIndex++, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
                colorReferences.push_back(reference);
            }
            else
            {
                // Only one depth reference sould make it in here, else there will be an overwrite
                depthReference = {attachmentIndex++, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
                hasDepth = true;
            }
        }

        VkSubpassDescription subpassDescription{};
        subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpassDescription.colorAttachmentCount = static_cast<uint32_t>(colorReferences.size());
        subpassDescription.pColorAttachments = colorReferences.data();
        subpassDescription.pDepthStencilAttachment = hasDepth ? &depthReference : nullptr;

        VkRenderPassCreateInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = static_cast<uint32_t>(_attachments.size());
        renderPassInfo.pAttachments = _attachments.data();
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpassDescription;

        VkRenderPass renderPass;
        PE_CHECK(vkCreateRenderPass(RHII.GetDevice(), &renderPassInfo, nullptr, &renderPass));
        m_handle = renderPass;

        Debug::SetObjectName(m_handle, VK_OBJECT_TYPE_RENDER_PASS, name);
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
