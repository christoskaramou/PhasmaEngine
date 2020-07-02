#include "vulkanPCH.h"
#include "Swapchain.h"
#include "../VulkanContext/VulkanContext.h"

namespace vm
{
	Swapchain::Swapchain()
	{
		swapchain = make_ref(vk::SwapchainKHR());
	}

	Swapchain::~Swapchain()
	{
	}

	uint32_t Swapchain::aquire(vk::Semaphore semaphore, vk::Fence fence) const
	{
		const auto aquire = VulkanContext::get()->device->acquireNextImageKHR(*swapchain, UINT64_MAX, semaphore, fence);
		if (aquire.result != vk::Result::eSuccess)
			throw std::runtime_error("Aquire Next Image error");

		return aquire.value;
	}

	void Swapchain::present(vk::ArrayProxy<const uint32_t> imageIndices, vk::ArrayProxy<const vk::Semaphore> semaphores, vk::ArrayProxy<const vk::SwapchainKHR> additionalSwapchains) const
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

	void Swapchain::destroy()
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
