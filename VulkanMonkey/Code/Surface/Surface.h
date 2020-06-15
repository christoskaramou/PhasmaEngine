#pragma once
#include <vulkan/vulkan.hpp>

namespace vm
{
	struct Surface
	{
		vk::SurfaceKHR surface;
		vk::Extent2D actualExtent;
		vk::SurfaceCapabilitiesKHR capabilities;
		vk::SurfaceFormatKHR formatKHR{};
		vk::PresentModeKHR presentModeKHR{};
	};
}