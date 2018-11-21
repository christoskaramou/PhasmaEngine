#pragma once

#include "Vulkan.h"
#include "SDL.h"
#include <map>

namespace vm {

	struct Image;
	struct Surface;
	struct Swapchain;

	struct VulkanContext {
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
	};
}