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
#include "../Core/Math.h"
#include "../ECS/Context.h"

namespace pe
{
	Swapchain::Swapchain()
	{
		swapchain = make_ref(vk::SwapchainKHR());
	}
	
	Swapchain::~Swapchain()
	{
	}
	
	void Swapchain::Create(uint32_t requestImageCount)
	{
		auto& vulkan = *Context::Get()->GetVKContext();
		const VkExtent2D extent = *vulkan.surface.actualExtent;
		
		vk::SwapchainCreateInfoKHR swapchainCreateInfo;
		swapchainCreateInfo.surface = *vulkan.surface.surface;
		swapchainCreateInfo.minImageCount = clamp(
				requestImageCount, vulkan.surface.capabilities->minImageCount,
				vulkan.surface.capabilities->maxImageCount
		);
		swapchainCreateInfo.imageFormat = vulkan.surface.formatKHR->format;
		swapchainCreateInfo.imageColorSpace = vulkan.surface.formatKHR->colorSpace;
		swapchainCreateInfo.imageExtent = extent;
		swapchainCreateInfo.imageArrayLayers = 1;
		swapchainCreateInfo.imageUsage =
				vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst;
		swapchainCreateInfo.preTransform = vulkan.surface.capabilities->currentTransform;
		swapchainCreateInfo.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
		swapchainCreateInfo.presentMode = *vulkan.surface.presentModeKHR;
		swapchainCreateInfo.clipped = VK_TRUE;
		swapchainCreateInfo.oldSwapchain =
				*swapchain ?
				*swapchain :
				nullptr;
		
		// new swapchain with old create info
		Swapchain newSwapchain;
		newSwapchain.swapchain = make_ref(vulkan.device->createSwapchainKHR(swapchainCreateInfo));
		
		// destroy old swapchain
		if (*swapchain)
		{
			vulkan.device->destroySwapchainKHR(*swapchain);
			*swapchain = nullptr;
		}
		
		// get the swapchain image handlers
		std::vector<vk::Image> images = vulkan.device->getSwapchainImagesKHR(*newSwapchain.swapchain);
		
		newSwapchain.images.resize(images.size());
		for (unsigned i = 0; i < images.size(); i++)
		{
			newSwapchain.images[i].image = make_ref(images[i]); // hold the image handlers
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
			imageViewCreateInfo.format = VulkanContext::Get()->surface.formatKHR->format;
			imageViewCreateInfo.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
			image.view = make_ref(VulkanContext::Get()->device->createImageView(imageViewCreateInfo));
		}
		
		*this = newSwapchain;
		vulkan.SetDebugObjectName(*swapchain, "");
	}
	
	uint32_t Swapchain::Aquire(vk::Semaphore semaphore, vk::Fence fence) const
	{
		const auto aquire = VulkanContext::Get()->device->acquireNextImageKHR(*swapchain, UINT64_MAX, semaphore, fence);
		if (aquire.result != vk::Result::eSuccess)
			throw std::runtime_error("Aquire Next Image error");
		
		return aquire.value;
	}
	
	void Swapchain::Present(
			vk::ArrayProxy<const uint32_t> imageIndices, vk::ArrayProxy<const vk::Semaphore> semaphores,
			vk::ArrayProxy<const vk::SwapchainKHR> additionalSwapchains
	) const
	{
		if (imageIndices.size() <= additionalSwapchains.size())
			throw std::runtime_error("Not enough image indices");
		std::vector<vk::SwapchainKHR> swapchains(static_cast<size_t>(additionalSwapchains.size()) + 1);
		swapchains[0] = *swapchain;
		if (!additionalSwapchains.empty())
			memcpy(&swapchains[1], additionalSwapchains.data(), additionalSwapchains.size() * sizeof(vk::SwapchainKHR));
		
		vk::PresentInfoKHR pi;
		pi.waitSemaphoreCount = semaphores.size();
		pi.pWaitSemaphores = semaphores.data();
		pi.swapchainCount = static_cast<uint32_t>(swapchains.size());
		pi.pSwapchains = swapchains.data();
		pi.pImageIndices = imageIndices.data();
		if (VulkanContext::Get()->graphicsQueue->presentKHR(pi) != vk::Result::eSuccess)
			throw std::runtime_error("Present error!");
	}
	
	void Swapchain::Destroy()
	{
		for (auto& image : images)
		{
			VulkanContext::Get()->device->destroyImageView(*image.view);
			*image.view = nullptr;
		}
		if (*swapchain)
		{
			VulkanContext::Get()->device->destroySwapchainKHR(*swapchain);
			*swapchain = nullptr;
		}
	}
}
