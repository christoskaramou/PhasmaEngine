#include "Surface.h"
#include <vulkan/vulkan.hpp>

namespace vm
{
	Surface::Surface()
	{
		surface = vk::SurfaceKHR();
		actualExtent = vk::Extent2D();
		capabilities = vk::SurfaceCapabilitiesKHR();
		formatKHR = vk::SurfaceFormatKHR();
		presentModeKHR = vk::PresentModeKHR();
	}

	Surface::~Surface()
	{
	}
}
