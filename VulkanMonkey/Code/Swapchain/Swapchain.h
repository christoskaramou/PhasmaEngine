#pragma once
#include "../VulkanContext/VulkanContext.h"
#include "../Image/Image.h"
#include <vector>

namespace vm {
	struct Swapchain
	{
		VulkanContext* vulkan = &VulkanContext::get();

		vk::SwapchainKHR swapchain;
		std::vector<Image> images{};

		uint32_t aquire(vk::Semaphore semaphore, vk::Fence fence) const;
		void present(vk::ArrayProxy<const uint32_t> imageIndices, vk::ArrayProxy<const vk::Semaphore> semaphores, vk::ArrayProxy<const vk::SwapchainKHR> additionalSwapchains = nullptr) const;
		void destroy();
	};
}