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
#include "Renderer/Fence.h"
#include "Core/Math.h"
#include "ECS/Context.h"

namespace pe
{
	Swapchain::Swapchain(Surface* surface)
	{
		VkSurfaceCapabilitiesKHR capabilities;
		vkGetPhysicalDeviceSurfaceCapabilitiesKHR(RHII.gpu, surface->surface, &capabilities);

		VkExtent2D extent;
		extent.width = surface->actualExtent.width;
		extent.height = surface->actualExtent.height;
		
		VkSwapchainCreateInfoKHR swapchainCreateInfo{};
		swapchainCreateInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
		swapchainCreateInfo.surface = surface->surface;
		swapchainCreateInfo.minImageCount = clamp(
			SWAPCHAIN_IMAGES,
			capabilities.minImageCount,
			capabilities.maxImageCount
		);
		swapchainCreateInfo.imageFormat = (VkFormat)surface->format;
		swapchainCreateInfo.imageColorSpace = (VkColorSpaceKHR)surface->colorSpace;
		swapchainCreateInfo.imageExtent = extent;
		swapchainCreateInfo.imageArrayLayers = 1;
		swapchainCreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		swapchainCreateInfo.preTransform = capabilities.currentTransform;
		swapchainCreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
		swapchainCreateInfo.presentMode = (VkPresentModeKHR)surface->presentMode;
		swapchainCreateInfo.clipped = VK_TRUE;
		if (m_apiHandle)
			swapchainCreateInfo.oldSwapchain = m_apiHandle;
		
		// new swapchain with old create info
		VkSwapchainKHR schain;
		vkCreateSwapchainKHR(RHII.device, &swapchainCreateInfo, nullptr, &schain);
		
		// TODO: Maybe check the result in a loop?
		uint32_t swapchainImageCount;
		vkGetSwapchainImagesKHR(RHII.device, schain, &swapchainImageCount, nullptr);

		std::vector<VkImage> imagesVK(swapchainImageCount);
		vkGetSwapchainImagesKHR(RHII.device, schain, &swapchainImageCount, imagesVK.data());

		for (auto* image : images)
		{
			vkDestroyImageView(RHII.device, image->view, nullptr);
			image->view = {};
			delete image;
		}
		
		images.resize(imagesVK.size());
		for (unsigned i = 0; i < images.size(); i++)
		{
			images[i] = new Image();
			images[i]->Handle() = imagesVK[i];
			images[i]->TransitionImageLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
			images[i]->blendAttachment.blendEnable = VK_TRUE;
			images[i]->blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
			images[i]->blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			images[i]->blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
			images[i]->blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
			images[i]->blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
			images[i]->blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
			images[i]->blendAttachment.colorWriteMask =
				VK_COLOR_COMPONENT_R_BIT |
				VK_COLOR_COMPONENT_G_BIT |
				VK_COLOR_COMPONENT_B_BIT |
				VK_COLOR_COMPONENT_A_BIT;
		}
		
		// create image views for each swapchain image
		for (auto* image : images)
		{
			VkImageViewCreateInfo imageViewCreateInfo{};
			imageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			imageViewCreateInfo.image = image->Handle();
			imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
			imageViewCreateInfo.format = (VkFormat)RHII.surface.format;
			imageViewCreateInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

			VkImageView imageView;
			vkCreateImageView(RHII.device, &imageViewCreateInfo, nullptr, &imageView);
			image->view = imageView;
		}
		
		if (m_apiHandle)
		{
			vkDestroySwapchainKHR(RHII.device, m_apiHandle, nullptr);
			m_apiHandle = {};
		}

		m_apiHandle = schain;
	}

	Swapchain::~Swapchain()
	{
		if (m_apiHandle)
		{
			vkDestroySwapchainKHR(RHII.device, m_apiHandle, nullptr);
			m_apiHandle = {};
		}

		for (auto* image : images)
		{
			vkDestroyImageView(RHII.device, image->view, nullptr);
			image->view = {};
			delete image;
		}
	}
	
	uint32_t Swapchain::Aquire(Semaphore* semaphore, Fence* fence)
	{
		VkFence fenceVK = nullptr;
		if (fence)
			fenceVK = fence->Handle();

		uint32_t imageIndex = 0;
		VkResult result = vkAcquireNextImageKHR(RHII.device, m_apiHandle, UINT64_MAX, semaphore->Handle(), fenceVK, &imageIndex);

		if (result != VK_SUCCESS)
			throw std::runtime_error("Aquire Next Image error");
		
		return imageIndex;
	}
}
#endif
