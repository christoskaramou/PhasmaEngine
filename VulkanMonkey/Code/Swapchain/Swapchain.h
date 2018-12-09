#pragma once
#include "../VulkanContext/VulkanContext.h"
#include "../Image/Image.h"
#include <vector>

namespace vm {
	struct Swapchain
	{
		VulkanContext* vulkan = &VulkanContext::getVulkanContext();

		vk::SwapchainKHR swapchain;
		std::vector<Image> images{};

		void destroy();
	};
}