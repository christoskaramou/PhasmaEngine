#pragma once

#include "Vulkan.h"
#include "SDL.h"
#include <map>
#include <vector>

namespace vm {

	struct Image;
	struct Surface;
	struct Swapchain;

	struct VulkanContext {
		static VulkanContext& getVulkanContext()
		{
			static VulkanContext VkCTX;
			return VkCTX;
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