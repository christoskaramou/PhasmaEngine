#pragma once

#include <vector>
#include <mutex>
#include "../Core/Surface.h"
#include "../Swapchain/Swapchain.h"

#define UNIFIED_GRAPHICS_AND_TRANSFER_QUEUE

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

	template <class T1>
	class Flags;
	enum class PipelineStageFlagBits : uint32_t;
	using PipelineStageFlags = Flags<PipelineStageFlagBits>;
}

struct SDL_Window;
enum VkDebugUtilsMessageSeverityFlagBitsEXT;
struct VkDebugUtilsMessengerCallbackDataEXT;

#if defined(_WIN32)
// On Windows, Vulkan commands use the stdcall convention
#define VKAPI_ATTR
#define VKAPI_CALL __stdcall
#define VKAPI_PTR  VKAPI_CALL
#elif defined(__ANDROID__) && defined(__ARM_ARCH) && __ARM_ARCH < 7
#error "Vulkan isn't supported for the 'armeabi' NDK ABI"
#elif defined(__ANDROID__) && defined(__ARM_ARCH) && __ARM_ARCH >= 7 && defined(__ARM_32BIT_STATE)
// On Android 32-bit ARM targets, Vulkan functions use the "hardfloat"
// calling convention, i.e. float parameters are passed in registers. This
// is true even if the rest of the application passes floats on the stack,
// as it does by default when compiling for the armeabi-v7a NDK ABI.
#define VKAPI_ATTR __attribute__((pcs("aapcs-vfp")))
#define VKAPI_CALL
#define VKAPI_PTR  VKAPI_ATTR
#else
// On other platforms, use the default calling convention
#define VKAPI_ATTR
#define VKAPI_CALL
#define VKAPI_PTR
#endif

namespace vm
{
	class Context;

	class VulkanContext
	{
	public:
		VulkanContext();

		~VulkanContext();

		void CreateInstance(SDL_Window* window);

		static VKAPI_ATTR uint32_t VKAPI_CALL MessageCallback(
				VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
				uint32_t messageType,
				const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
				void* pUserData
		);

		void CreateDebugMessenger();

		void DestroyDebugMessenger();

		void CreateSurface(Context* ctx);

		void GetSurfaceProperties(Context* ctx);

		void GetGpu();

		void GetGraphicsFamilyId();

		void GetTransferFamilyId();

		void GetComputeFamilyId();

		void CreateDevice();

		void CreateAllocator();

		void GetGraphicsQueue();

		void GetTransferQueue();

		void GetComputeQueue();

		void GetQueues();

		void CreateSwapchain(Context* ctx, uint32_t requestImageCount);

		void CreateCommandPools();

		void CreateDescriptorPool(uint32_t maxDescriptorSets);

		void CreateCmdBuffers(uint32_t bufferCount = 1);

		void CreateFences(uint32_t fenceCount);

		void CreateSemaphores(uint32_t semaphoresCount);

		void CreateDepth();

		void Init(Context* ctx);

		void Destroy();

		Ref<vk::Instance> instance;
		Ref<vk::DebugUtilsMessengerEXT> debugMessenger;
		Ref<vk::PhysicalDevice> gpu;
		Ref<vk::PhysicalDeviceProperties> gpuProperties;
		Ref<vk::PhysicalDeviceFeatures> gpuFeatures;
		Ref<vk::Device> device;
		Ref<vk::Queue> graphicsQueue, computeQueue, transferQueue;
		Ref<vk::CommandPool> commandPool;
		Ref<vk::CommandPool> commandPool2;
		Ref<vk::DescriptorPool> descriptorPool;
		Ref<vk::DispatchLoaderDynamic> dispatchLoaderDynamic;
		Ref<std::vector<vk::QueueFamilyProperties>> queueFamilyProperties;
		Ref<std::vector<vk::CommandBuffer>> dynamicCmdBuffers;
		Ref<std::vector<vk::CommandBuffer>> shadowCmdBuffers;
		Ref<std::vector<vk::Fence>> fences;
		Ref<std::vector<vk::Semaphore>> semaphores;
		VmaAllocator allocator = nullptr;

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
				const vk::Fence signalFence
		) const;

		void waitFences(const vk::ArrayProxy<const vk::Fence> fences) const;

		void submitAndWaitFence(
				const vk::ArrayProxy<const vk::CommandBuffer> commandBuffers,
				const vk::ArrayProxy<const vk::PipelineStageFlags> waitStages,
				const vk::ArrayProxy<const vk::Semaphore> waitSemaphores,
				const vk::ArrayProxy<const vk::Semaphore> signalSemaphores
		) const;

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

		void SetDebugObjectName(const void* validHandle, const void* name)
		{ }

#endif
	private:
		static inline std::mutex m_submit_mutex {};
	public:
		void waitAndLockSubmits();

		void unlockSubmits();

		static VulkanContext* get() noexcept;

		static void remove() noexcept;
	};
}