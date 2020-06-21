#pragma once
#include <vector>
#include <mutex>
#include "../Core/Surface.h"
#include "../Swapchain/Swapchain.h"

#define WIDTH VulkanContext::get()->surface.actualExtent->width
#define HEIGHT VulkanContext::get()->surface.actualExtent->height
#define WIDTH_f static_cast<float>(WIDTH)
#define HEIGHT_f static_cast<float>(HEIGHT)

namespace vk
{
	class Instance;
	class DebugUtilsMessengerEXT;
	class PhysicalDevice;
	struct PhysicalDeviceProperties;
	struct PhysicalDeviceFeatures;
	struct QueueFamilyProperties;
	class Device;
	class Queue;
	class CommandPool;
	class CommandBuffer;
	class DescriptorPool;
	class Fence;
	class Semaphore;
	class DispatchLoaderDynamic;

	template<class T1, class T2> class Flags;
	enum class PipelineStageFlagBits;
	using PipelineStageFlags = Flags<PipelineStageFlagBits, uint32_t>;
}

struct SDL_Window;

namespace vm
{
	class VulkanContext
	{
	public:
		Ref_t<vk::Instance> instance;
		Ref_t<vk::DebugUtilsMessengerEXT> debugMessenger;
		Ref_t<vk::PhysicalDevice> gpu;
		Ref_t<vk::PhysicalDeviceProperties> gpuProperties;
		Ref_t<vk::PhysicalDeviceFeatures> gpuFeatures;
		Ref_t<vk::Device> device;
		Ref_t<vk::Queue> graphicsQueue, computeQueue, transferQueue;
		Ref_t<vk::CommandPool> commandPool;
		Ref_t<vk::CommandPool> commandPool2;
		Ref_t<vk::DescriptorPool> descriptorPool;
		Ref_t<vk::DispatchLoaderDynamic> dispatchLoaderDynamic;
		Ref_t<std::vector<vk::QueueFamilyProperties>> queueFamilyProperties;
		Ref_t<std::vector<vk::CommandBuffer>> dynamicCmdBuffers;
		Ref_t<std::vector<vk::CommandBuffer>> shadowCmdBuffers;
		Ref_t<std::vector<vk::Fence>> fences;
		Ref_t<std::vector<vk::Semaphore>> semaphores;

		SDL_Window* window;
		Surface surface;
		Swapchain swapchain;
		Image depth;
		int graphicsFamilyId, computeFamilyId, transferFamilyId;

		// Helpers
		void submit(
			const vk::ArrayProxy<const vk::CommandBuffer> commandBuffers,
			const vk::ArrayProxy<const vk::PipelineStageFlags> waitStages,
			const vk::ArrayProxy<const vk::Semaphore> waitSemaphores,
			const vk::ArrayProxy<const vk::Semaphore> signalSemaphores,
			const vk::Fence signalFence) const;
		void waitFences(const vk::ArrayProxy<const vk::Fence> fences) const;
		void submitAndWaitFence(
			const vk::ArrayProxy<const vk::CommandBuffer> commandBuffers,
			const vk::ArrayProxy<const vk::PipelineStageFlags> waitStages,
			const vk::ArrayProxy<const vk::Semaphore> waitSemaphores,
			const vk::ArrayProxy<const vk::Semaphore> signalSemaphores) const;

#ifdef NOT_USED
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
		VulkanContext();											// default constructor
		~VulkanContext() = default;									// destructor
	};
}