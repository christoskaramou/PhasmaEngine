#pragma once
#include "Vulkan.h"

namespace vm {
	struct Surface
	{
		vk::SurfaceKHR surface;
		VkExtent2D actualExtent;
		vk::SurfaceCapabilitiesKHR capabilities;
		vk::SurfaceFormatKHR formatKHR;
		vk::PresentModeKHR presentModeKHR;
	};
}