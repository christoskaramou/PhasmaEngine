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
		Ref<vk::SurfaceKHR> surface;
		Ref<vk::Extent2D> actualExtent;
		Ref<vk::SurfaceCapabilitiesKHR> capabilities;
		Ref<vk::SurfaceFormatKHR> formatKHR{};
		Ref<vk::PresentModeKHR> presentModeKHR{};
	};
}