#pragma once
#include "Vulkan.h"
#include "Image.h"

namespace vm {
	struct Swapchain
	{
		vk::SwapchainKHR swapchain;
		std::vector<Image> images{};

		void destroy(vk::Device device);
	};
}