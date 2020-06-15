#pragma once

#include <vulkan/vulkan.hpp>
#include "SDL/SDL.h"
#include "SDL/SDL_syswm.h"
#include "SDL/SDL_vulkan.h"
#include <vector>
#include <mutex>
#include "../Core/Base.h"

#define WIDTH VulkanContext::get()->surface->actualExtent.width
#define HEIGHT VulkanContext::get()->surface->actualExtent.height
#define WIDTH_f static_cast<float>(WIDTH)
#define HEIGHT_f static_cast<float>(HEIGHT)

namespace vm
{

	class Image;
	struct Surface;
	class Swapchain;

	class VulkanContext
	{
	public:
		SDL_Window* window = nullptr;
		vk::Instance instance;
		vk::DebugUtilsMessengerEXT debugMessenger;
		Surface* surface = nullptr;
		int graphicsFamilyId{}, computeFamilyId{}, transferFamilyId{};
		vk::PhysicalDevice gpu;
		vk::PhysicalDeviceProperties gpuProperties;
		vk::PhysicalDeviceFeatures gpuFeatures;
		std::vector<vk::QueueFamilyProperties> queueFamilyProperties{};
		vk::Device device;
		vk::Queue graphicsQueue, computeQueue, transferQueue;
		vk::CommandPool commandPool;
		vk::CommandPool commandPool2;
		Swapchain* swapchain = nullptr;
		Image* depth = nullptr;
		std::vector<vk::CommandBuffer> dynamicCmdBuffers{};
		std::vector<vk::CommandBuffer> shadowCmdBuffers{};
		vk::DescriptorPool descriptorPool;
		std::vector<vk::Fence> fences{};
		std::vector<vk::Semaphore> semaphores{};
		vk::DispatchLoaderDynamic dispatchLoaderDynamic;

		// Helpers
		void submit(
			const vk::ArrayProxy<const vk::CommandBuffer> commandBuffers,
			const vk::ArrayProxy<const vk::PipelineStageFlags> waitStages,
			const vk::ArrayProxy<const vk::Semaphore> waitSemaphores,
			const vk::ArrayProxy<const vk::Semaphore> signalSemaphores,
			const vk::Fence signalFence = nullptr) const;
		void waitFences(const vk::ArrayProxy<const vk::Fence> fences) const;
		void submitAndWaitFence(
			const vk::ArrayProxy<const vk::CommandBuffer> commandBuffers,
			const vk::ArrayProxy<const vk::PipelineStageFlags> waitStages,
			const vk::ArrayProxy<const vk::Semaphore> waitSemaphores,
			const vk::ArrayProxy<const vk::Semaphore> signalSemaphores) const;

#ifdef _DEBUG
	template<typename T>
	void SetDebugObjectName(const T& validHandle, const char* name)
	{
		vk::DebugUtilsObjectNameInfoEXT duoni;
		duoni.objectType = validHandle.objectType;
		duoni.objectHandle = reinterpret_cast<uint64_t>(static_cast<void*>(validHandle));
		duoni.pObjectName = name;
		device.setDebugUtilsObjectNameEXT(duoni, dispatchLoaderDynamic);
	}
#else
		void SetDebugObjectName(const void* validHandle, const void* name) {}
#endif
	private:
		std::mutex m_submit_mutex{};
	public:
		void waitAndLockSubmits();
		void unlockSubmits();

		static VulkanContext* get() noexcept;
		static void remove() noexcept;

		VulkanContext(VulkanContext const&) = delete;				// copy constructor
		VulkanContext(VulkanContext&&) noexcept = delete;			// move constructor
		VulkanContext& operator=(VulkanContext const&) = delete;	// copy assignment
		VulkanContext& operator=(VulkanContext&&) = delete;			// move assignment
	private:
		~VulkanContext() = default;									// destructor
		VulkanContext() = default;									// default constructor
	};
}