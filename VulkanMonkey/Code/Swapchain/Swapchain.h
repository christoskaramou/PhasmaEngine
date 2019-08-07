#pragma once
#include "../Image/Image.h"
#include <vector>

namespace vm {
	struct Swapchain
	{
		vk::SwapchainKHR swapchain;
		std::vector<Image> images{};

		uint32_t aquire(vk::Semaphore semaphore, vk::Fence fence) const;
		void present(vk::ArrayProxy<const uint32_t> imageIndices, vk::ArrayProxy<const vk::Semaphore> semaphores, vk::ArrayProxy<const vk::SwapchainKHR> additionalSwapchains = nullptr) const;
		void destroy();
	};
}