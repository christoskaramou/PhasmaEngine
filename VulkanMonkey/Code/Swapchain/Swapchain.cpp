#include "Swapchain.h"

using namespace vm;

uint32_t Swapchain::aquire(vk::Semaphore semaphore, vk::Fence fence) const
{
	return vulkan->device.acquireNextImageKHR(swapchain, UINT64_MAX, semaphore, fence).value;
}

void Swapchain::present(vk::ArrayProxy<const uint32_t> imageIndices, vk::ArrayProxy<const vk::Semaphore> semaphores, vk::ArrayProxy<const vk::SwapchainKHR> additionalSwapchains) const
{
	if (imageIndices.size() <= additionalSwapchains.size())
		throw std::runtime_error("Not enough image indices");
	std::vector<vk::SwapchainKHR> swapchains(static_cast<size_t>(additionalSwapchains.size()) + 1);
	swapchains[0] = swapchain;
	if (!additionalSwapchains.empty())
		memcpy(&swapchains[1], additionalSwapchains.data(), additionalSwapchains.size() * sizeof(vk::SwapchainKHR));

	vk::PresentInfoKHR pi;
	pi.waitSemaphoreCount = semaphores.size();
	pi.pWaitSemaphores = semaphores.data();
	pi.swapchainCount = static_cast<uint32_t>(swapchains.size());
	pi.pSwapchains = swapchains.data();
	pi.pImageIndices = imageIndices.data();
	if (vulkan->graphicsQueue.presentKHR(pi) != vk::Result::eSuccess)
		throw std::runtime_error("Present error!");
}

void Swapchain::destroy()
{
	for (auto &image : images) {
		vulkan->device.destroyImageView(image.view);
		image.view = nullptr;
	}
	if (swapchain) {
		vulkan->device.destroySwapchainKHR(swapchain);
		swapchain = nullptr;
	}
}