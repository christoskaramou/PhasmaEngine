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

#include "PhasmaPch.h"
#include "Swapchain.h"
#include "Renderer/Vulkan/Vulkan.h"
#include "Core/Math.h"
#include "ECS/Context.h"

namespace pe
{
	Swapchain::Swapchain()
	{
		handle = make_sptr(vk::SwapchainKHR());
	}
	
	Swapchain::~Swapchain()
	{
	}
	
	void Swapchain::Create(Surface* surface)
	{
		const VkExtent2D extent = *surface->actualExtent;
		
		vk::SwapchainCreateInfoKHR swapchainCreateInfo;
		swapchainCreateInfo.surface = *surface->surface;
		swapchainCreateInfo.minImageCount = clamp(
			SWAPCHAIN_IMAGES, surface->capabilities->minImageCount,
			surface->capabilities->maxImageCount
		);
		swapchainCreateInfo.imageFormat = surface->formatKHR->format;
		swapchainCreateInfo.imageColorSpace = surface->formatKHR->colorSpace;
		swapchainCreateInfo.imageExtent = extent;
		swapchainCreateInfo.imageArrayLayers = 1;
		swapchainCreateInfo.imageUsage =
				vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst;
		swapchainCreateInfo.preTransform = surface->capabilities->currentTransform;
		swapchainCreateInfo.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
		swapchainCreateInfo.presentMode = *surface->presentModeKHR;
		swapchainCreateInfo.clipped = VK_TRUE;
		swapchainCreateInfo.oldSwapchain =
				*handle ?
				*handle :
				nullptr;
		
		// new swapchain with old create info
		Swapchain newSwapchain;
		newSwapchain.handle = make_sptr(VULKAN.device->createSwapchainKHR(swapchainCreateInfo));
		
		// destroy old swapchain
		if (*handle)
		{
			VULKAN.device->destroySwapchainKHR(*handle);
			*handle = nullptr;
		}
		
		// get the swapchain image handlers
		std::vector<vk::Image> images = VULKAN.device->getSwapchainImagesKHR(*newSwapchain.handle);
		
		newSwapchain.images.resize(images.size());
		for (unsigned i = 0; i < images.size(); i++)
		{
			newSwapchain.images[i].image = VkImage(images[i]); // hold the image handlers
			newSwapchain.images[i].TransitionImageLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
			newSwapchain.images[i].blendAttachment.blendEnable = VK_TRUE;
			newSwapchain.images[i].blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
			newSwapchain.images[i].blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			newSwapchain.images[i].blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
			newSwapchain.images[i].blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
			newSwapchain.images[i].blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
			newSwapchain.images[i].blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
			newSwapchain.images[i].blendAttachment.colorWriteMask =
				VK_COLOR_COMPONENT_R_BIT |
				VK_COLOR_COMPONENT_G_BIT |
				VK_COLOR_COMPONENT_B_BIT |
				VK_COLOR_COMPONENT_A_BIT;
		}
		
		// create image views for each swapchain image
		for (auto& image : newSwapchain.images)
		{
			vk::ImageViewCreateInfo imageViewCreateInfo;
			imageViewCreateInfo.image = image.image;
			imageViewCreateInfo.viewType = vk::ImageViewType::e2D;
			imageViewCreateInfo.format = VULKAN.surface.formatKHR->format;
			imageViewCreateInfo.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
			image.view = VkImageView(VULKAN.device->createImageView(imageViewCreateInfo));
		}
		
		*this = newSwapchain;
	}
	
	uint32_t Swapchain::Aquire(vk::Semaphore semaphore, vk::Fence fence) const
	{
		const auto aquire = VULKAN.device->acquireNextImageKHR(*handle, UINT64_MAX, semaphore, fence);
		if (aquire.result != vk::Result::eSuccess)
			throw std::runtime_error("Aquire Next Image error");
		
		return aquire.value;
	}
	//vk::ArrayProxy<const vk::SwapchainKHR> additionalSwapchains
	void Swapchain::Present(uint32_t imageIndex, vk::ArrayProxy<const vk::Semaphore> semaphores) const
	{		
		VULKAN.Present(*handle, imageIndex, semaphores);
	}
	
	void Swapchain::Destroy()
	{
		for (auto& image : images)
		{
			vkDestroyImageView(*VULKAN.device, image.view, nullptr);
			image.view = {};
		}
		if (*handle)
		{
			VULKAN.device->destroySwapchainKHR(*handle);
			*handle = nullptr;
		}
	}
}
