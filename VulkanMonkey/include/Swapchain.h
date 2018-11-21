#pragma once
#include "VulkanContext.h"
#include "Image.h"

namespace vm {
	struct Swapchain
	{
		VulkanContext* vulkan;
		Swapchain(VulkanContext* vulkan);

		vk::SwapchainKHR swapchain;
		std::vector<Image> images{};

		void destroy();
	};
}