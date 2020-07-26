#include "vulkanPCH.h"
#include "Swapchain.h"
#include "../VulkanContext/VulkanContext.h"
#include "../Core/Math.h"
#include "../Context/Context.h"

namespace vm
{
	Swapchain::Swapchain()
	{
		swapchain = make_ref(vk::SwapchainKHR());
	}

	Swapchain::~Swapchain()
	{
	}

	void Swapchain::Create(Context* ctx, uint32_t requestImageCount)
	{
		auto& vulkan = *ctx->GetVKContext();
		const VkExtent2D extent = *vulkan.surface.actualExtent;

		vk::SwapchainCreateInfoKHR swapchainCreateInfo;
		swapchainCreateInfo.surface = *vulkan.surface.surface;
		swapchainCreateInfo.minImageCount = clamp(requestImageCount, vulkan.surface.capabilities->minImageCount, vulkan.surface.capabilities->maxImageCount);
		swapchainCreateInfo.imageFormat = vulkan.surface.formatKHR->format;
		swapchainCreateInfo.imageColorSpace = vulkan.surface.formatKHR->colorSpace;
		swapchainCreateInfo.imageExtent = extent;
		swapchainCreateInfo.imageArrayLayers = 1;
		swapchainCreateInfo.imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst;
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
		if (*swapchain) {
			vulkan.device->destroySwapchainKHR(*swapchain);
			*swapchain = nullptr;
		}

		// get the swapchain image handlers
		std::vector<vk::Image> images = vulkan.device->getSwapchainImagesKHR(*newSwapchain.swapchain);

		newSwapchain.images.resize(images.size());
		for (unsigned i = 0; i < images.size(); i++) {
			newSwapchain.images[i].image = make_ref(images[i]); // hold the image handlers
			newSwapchain.images[i].transitionImageLayout(vk::ImageLayout::eUndefined, vk::ImageLayout::ePresentSrcKHR);
			newSwapchain.images[i].blentAttachment->blendEnable = VK_TRUE;
			newSwapchain.images[i].blentAttachment->srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
			newSwapchain.images[i].blentAttachment->dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
			newSwapchain.images[i].blentAttachment->colorBlendOp = vk::BlendOp::eAdd;
			newSwapchain.images[i].blentAttachment->srcAlphaBlendFactor = vk::BlendFactor::eOne;
			newSwapchain.images[i].blentAttachment->dstAlphaBlendFactor = vk::BlendFactor::eZero;
			newSwapchain.images[i].blentAttachment->alphaBlendOp = vk::BlendOp::eAdd;
			newSwapchain.images[i].blentAttachment->colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
		}

		// create image views for each swapchain image
		for (auto& image : newSwapchain.images) {
			vk::ImageViewCreateInfo imageViewCreateInfo;
			imageViewCreateInfo.image = *image.image;
			imageViewCreateInfo.viewType = vk::ImageViewType::e2D;
			imageViewCreateInfo.format = VulkanContext::get()->surface.formatKHR->format;
			imageViewCreateInfo.subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 };
			image.view = make_ref(VulkanContext::get()->device->createImageView(imageViewCreateInfo));
		}

		*this = newSwapchain;
	}

	uint32_t Swapchain::Aquire(vk::Semaphore semaphore, vk::Fence fence) const
	{
		const auto aquire = VulkanContext::get()->device->acquireNextImageKHR(*swapchain, UINT64_MAX, semaphore, fence);
		if (aquire.result != vk::Result::eSuccess)
			throw std::runtime_error("Aquire Next Image error");

		return aquire.value;
	}

	void Swapchain::Present(vk::ArrayProxy<const uint32_t> imageIndices, vk::ArrayProxy<const vk::Semaphore> semaphores, vk::ArrayProxy<const vk::SwapchainKHR> additionalSwapchains) const
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
		if (VulkanContext::get()->graphicsQueue->presentKHR(pi) != vk::Result::eSuccess)
			throw std::runtime_error("Present error!");
	}

	void Swapchain::Destroy()
	{
		for (auto& image : images) {
			VulkanContext::get()->device->destroyImageView(*image.view);
			*image.view = nullptr;
		}
		if (*swapchain) {
			VulkanContext::get()->device->destroySwapchainKHR(*swapchain);
			*swapchain = nullptr;
		}
	}
}
