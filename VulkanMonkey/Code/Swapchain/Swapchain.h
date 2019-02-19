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

		void destroy();
	};
}