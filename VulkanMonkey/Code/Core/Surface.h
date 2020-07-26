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
	class Context;

	class Surface
	{
	public:
		Surface();
		~Surface();

		void Create(Context* ctx);
		void FindCapabilities(Context* ctx);
		void FindFormat(Context* ctx);
		void FindPresentationMode(Context* ctx);
		void FindProperties(Context* ctx);

		Ref<vk::SurfaceKHR> surface;
		Ref<vk::Extent2D> actualExtent;
		Ref<vk::SurfaceCapabilitiesKHR> capabilities;
		Ref<vk::SurfaceFormatKHR> formatKHR{};
		Ref<vk::PresentModeKHR> presentModeKHR{};
	};
}