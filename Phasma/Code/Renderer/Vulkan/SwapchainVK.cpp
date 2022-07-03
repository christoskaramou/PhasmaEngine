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
#include "Renderer/Swapchain.h"
#include "Renderer/RHI.h"
#include "Renderer/Image.h"
#include "Renderer/Semaphore.h"
#include "Renderer/Surface.h"

namespace pe
{
    Swapchain::Swapchain(Surface *surface, const std::string &name)
    {
        VkSurfaceCapabilitiesKHR capabilities;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(RHII.GetGpu(), surface->Handle(), &capabilities);

        VkExtent2D extent;
        extent.width = surface->actualExtent.width;
        extent.height = surface->actualExtent.height;

        VkSwapchainCreateInfoKHR swapchainCreateInfo{};
        swapchainCreateInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        swapchainCreateInfo.surface = surface->Handle();
        swapchainCreateInfo.minImageCount = clamp(
            SWAPCHAIN_IMAGES,
            capabilities.minImageCount,
            capabilities.maxImageCount);
        if (SWAPCHAIN_IMAGES != swapchainCreateInfo.minImageCount)
            PE_ERROR("Swapchain image count error");
        swapchainCreateInfo.imageFormat = Translate<VkFormat>(surface->format);
        swapchainCreateInfo.imageColorSpace = Translate<VkColorSpaceKHR>(surface->colorSpace);
        swapchainCreateInfo.imageExtent = extent;
        swapchainCreateInfo.imageArrayLayers = 1;
        swapchainCreateInfo.imageUsage = Translate<VkImageUsageFlags>(ImageUsage::ColorAttachmentBit | ImageUsage::TransferDstBit);
        swapchainCreateInfo.preTransform = capabilities.currentTransform;
        swapchainCreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        swapchainCreateInfo.presentMode = Translate<VkPresentModeKHR>(surface->presentMode);
        swapchainCreateInfo.clipped = VK_TRUE;
        if (m_handle)
            swapchainCreateInfo.oldSwapchain = m_handle;

        // new swapchain with old create info
        VkSwapchainKHR schain;
        PE_CHECK(vkCreateSwapchainKHR(RHII.GetDevice(), &swapchainCreateInfo, nullptr, &schain));

        // TODO: Maybe check the result in a loop?
        uint32_t swapchainImageCount;
        PE_CHECK(vkGetSwapchainImagesKHR(RHII.GetDevice(), schain, &swapchainImageCount, nullptr));

        std::vector<VkImage> imagesVK(swapchainImageCount);
        PE_CHECK(vkGetSwapchainImagesKHR(RHII.GetDevice(), schain, &swapchainImageCount, imagesVK.data()));

        for (auto *image : images)
            Image::Destroy(image);

        images.resize(imagesVK.size());
        for (unsigned i = 0; i < images.size(); i++)
        {
            images[i] = new Image();
            images[i]->Handle() = imagesVK[i];
            images[i]->m_layouts = {{ImageLayout::Undefined}};
            images[i]->blendAttachment.blendEnable = 1;
            images[i]->blendAttachment.srcColorBlendFactor = BlendFactor::SrcAlpha;
            images[i]->blendAttachment.dstColorBlendFactor = BlendFactor::OneMinusSrcAlpha;
            images[i]->blendAttachment.colorBlendOp = BlendOp::Add;
            images[i]->blendAttachment.srcAlphaBlendFactor = BlendFactor::One;
            images[i]->blendAttachment.dstAlphaBlendFactor = BlendFactor::Zero;
            images[i]->blendAttachment.alphaBlendOp = BlendOp::Add;
            images[i]->blendAttachment.colorWriteMask = ColorComponent::RGBABit;
        }

        // create image views for each swapchain image
        for (int i = 0; i < images.size(); i++)
        {
            VkImageViewCreateInfo imageViewCreateInfo{};
            imageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            imageViewCreateInfo.image = images[i]->Handle();
            imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            imageViewCreateInfo.format =  Translate<VkFormat>(RHII.GetSurface()->format);
            imageViewCreateInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

            VkImageView imageView;
            PE_CHECK(vkCreateImageView(RHII.GetDevice(), &imageViewCreateInfo, nullptr, &imageView));
            images[i]->view = imageView;

            Debug::SetObjectName(images[i]->Handle(), ObjectType::Image, "Swapchain_image" + std::to_string(i));
            Debug::SetObjectName(images[i]->view, ObjectType::ImageView, "Swapchain_image_view" + std::to_string(i));
        }

        if (m_handle)
        {
            vkDestroySwapchainKHR(RHII.GetDevice(), m_handle, nullptr);
            m_handle = {};
        }

        m_handle = schain;

        Debug::SetObjectName(m_handle, ObjectType::Swapchain, name);
    }

    Swapchain::~Swapchain()
    {
        if (m_handle)
        {
            vkDestroySwapchainKHR(RHII.GetDevice(), m_handle, nullptr);
            m_handle = {};
        }

        for (auto *image : images)
        {
            // Invalidate the image handle, swapchain destroys it
            image->Handle() = {};
            delete image;
        }
    }

    uint32_t Swapchain::Aquire(Semaphore *semaphore)
    {
        uint32_t imageIndex = 0;
        PE_CHECK(vkAcquireNextImageKHR(RHII.GetDevice(), m_handle, UINT64_MAX, semaphore->Handle(), nullptr, &imageIndex));

        return imageIndex;
    }
}
#endif
