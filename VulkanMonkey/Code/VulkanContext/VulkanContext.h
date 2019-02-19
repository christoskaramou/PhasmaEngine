#pragma once

#include "../../include/Vulkan.h"
#include "../../include/SDL.h"

constexpr auto VULKAN_CONTEXT_INSTANCES = 2;

#define WIDTH VulkanContext::get().surface->actualExtent.width
#define HEIGHT VulkanContext::get().surface->actualExtent.height
#define WIDTH_f static_cast<float>(WIDTH)
#define HEIGHT_f static_cast<float>(HEIGHT)

namespace vm {

	struct Image;
	struct Surface;
	struct Swapchain;

	struct VulkanContext {
		static VulkanContext& get(uint32_t index = 0)
		{
			static VulkanContext VkCTX[VULKAN_CONTEXT_INSTANCES];

			assert(index < VULKAN_CONTEXT_INSTANCES);
			return VkCTX[index];
		}

		SDL_Window* window;
		vk::Instance instance;
		Surface* surface;
		int graphicsFamilyId, presentFamilyId, computeFamilyId;
		vk::PhysicalDevice gpu;
		vk::PhysicalDeviceProperties gpuProperties;
		vk::PhysicalDeviceFeatures gpuFeatures;
		vk::Device device;
		vk::Queue graphicsQueue, presentQueue, computeQueue;
		vk::CommandPool commandPool, commandPoolCompute;
		vk::SampleCountFlagBits sampleCount = vk::SampleCountFlagBits::e4;
		Swapchain* swapchain;
		Image* depth;
		vk::CommandBuffer dynamicCmdBuffer, computeCmdBuffer, shadowCmdBuffer;
		vk::DescriptorPool descriptorPool;
		std::vector<vk::Fence> fences{};
		std::vector<vk::Semaphore> semaphores{};

	private:
		VulkanContext() {};
		VulkanContext(VulkanContext const&) {};
		VulkanContext& operator=(VulkanContext const&) {};
		~VulkanContext() {};
	};
}