#include "vulkanPCH.h"
#include "Surface.h"

namespace vm
{
	Surface::Surface()
	{
		surface = make_ref(vk::SurfaceKHR());
		actualExtent = make_ref(vk::Extent2D());
		capabilities = make_ref(vk::SurfaceCapabilitiesKHR());
		formatKHR = make_ref(vk::SurfaceFormatKHR());
		presentModeKHR = make_ref(vk::PresentModeKHR::eFifo);
	}

	Surface::~Surface()
	{
	}
}
