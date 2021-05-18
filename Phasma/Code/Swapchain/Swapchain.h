#pragma once

#include "../Core/Image.h"
#include <vector>

namespace vk
{
	class SwapchainKHR;
	class Semaphore;
	class Fence;
	template <class T>
	class ArrayProxy;
}

namespace pe
{
	class Context;

	class Swapchain
	{
	public:
		Swapchain();

		~Swapchain();

		void Create(Context* ctx, uint32_t requestImageCount);

		uint32_t Aquire(vk::Semaphore semaphore, vk::Fence fence) const;

		void Present(
				vk::ArrayProxy<const uint32_t> imageIndices, vk::ArrayProxy<const vk::Semaphore> semaphores,
				vk::ArrayProxy<const vk::SwapchainKHR> additionalSwapchains
		) const;

		void Destroy();

		Ref<vk::SwapchainKHR> swapchain;
		std::vector<Image> images {};
	};
}