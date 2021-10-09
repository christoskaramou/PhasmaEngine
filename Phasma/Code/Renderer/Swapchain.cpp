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
#include "RenderApi.h"
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
			newSwapchain.images[i].image = make_sptr(images[i]); // hold the image handlers
			newSwapchain.images[i].transitionImageLayout(vk::ImageLayout::eUndefined, vk::ImageLayout::ePresentSrcKHR);
			newSwapchain.images[i].blentAttachment->blendEnable = VK_TRUE;
			newSwapchain.images[i].blentAttachment->srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
			newSwapchain.images[i].blentAttachment->dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
			newSwapchain.images[i].blentAttachment->colorBlendOp = vk::BlendOp::eAdd;
			newSwapchain.images[i].blentAttachment->srcAlphaBlendFactor = vk::BlendFactor::eOne;
			newSwapchain.images[i].blentAttachment->dstAlphaBlendFactor = vk::BlendFactor::eZero;
			newSwapchain.images[i].blentAttachment->alphaBlendOp = vk::BlendOp::eAdd;
			newSwapchain.images[i].blentAttachment->colorWriteMask =
					vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB |
					vk::ColorComponentFlagBits::eA;
		}
		
		// create image views for each swapchain image
		for (auto& image : newSwapchain.images)
		{
			vk::ImageViewCreateInfo imageViewCreateInfo;
			imageViewCreateInfo.image = *image.image;
			imageViewCreateInfo.viewType = vk::ImageViewType::e2D;
			imageViewCreateInfo.format = VULKAN.surface.formatKHR->format;
			imageViewCreateInfo.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
			image.view = make_sptr(VULKAN.device->createImageView(imageViewCreateInfo));
		}
		
		*this = newSwapchain;
		VULKAN.SetDebugObjectName(*handle, "");
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
			VULKAN.device->destroyImageView(*image.view);
			*image.view = nullptr;
		}
		if (*handle)
		{
			VULKAN.device->destroySwapchainKHR(*handle);
			*handle = nullptr;
		}
	}
}
