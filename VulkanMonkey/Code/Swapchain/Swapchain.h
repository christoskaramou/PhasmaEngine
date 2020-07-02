#pragma once
#include "../Core/Image.h"
#include <vector>

namespace vk
{
	class SwapchainKHR;
	class Semaphore;
	class Fence;
	template<class T> class ArrayProxy;
}

namespace vm 
{
	class Swapchain
	{
	public:
		Swapchain();
		~Swapchain();
		Ref<vk::SwapchainKHR> swapchain;
		std::vector<Image> images{};

		uint32_t aquire(vk::Semaphore semaphore, vk::Fence fence) const;
		void present(vk::ArrayProxy<const uint32_t> imageIndices, vk::ArrayProxy<const vk::Semaphore> semaphores, vk::ArrayProxy<const vk::SwapchainKHR> additionalSwapchains) const;
		void destroy();
	};
}