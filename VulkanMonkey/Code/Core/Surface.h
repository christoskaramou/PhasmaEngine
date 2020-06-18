#pragma once
#include "../Core/Base.h"

namespace vk
{
	class SurfaceKHR;
	struct Extent2D;
	struct SurfaceCapabilitiesKHR;
	struct SurfaceFormatKHR;
	enum class PresentModeKHR;
}

namespace vm
{
	class Surface
	{
	public:
		Surface();
		~Surface();
		Ref_t<vk::SurfaceKHR> surface;
		Ref_t<vk::Extent2D> actualExtent;
		Ref_t<vk::SurfaceCapabilitiesKHR> capabilities;
		Ref_t<vk::SurfaceFormatKHR> formatKHR{};
		Ref_t<vk::PresentModeKHR> presentModeKHR{};
	};
}