/*
Copyright (c) 2018-2021 Christos Karamoustos

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#pragma once

#include <vector>
#include <mutex>
#include "../Surface.h"
#include "../Swapchain.h"

#define UNIFIED_GRAPHICS_AND_TRANSFER_QUEUE

#define WIDTH VulkanContext::Get()->surface.actualExtent->width
#define HEIGHT VulkanContext::Get()->surface.actualExtent->height
#define WIDTH_f static_cast<float>(WIDTH)
#define HEIGHT_f static_cast<float>(HEIGHT)

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

namespace pe
{
	class Context;
	
	class VulkanContext : public NoCopy, public NoMove
	{
	private:
		VulkanContext();
	
	public:
		~VulkanContext();
		
		void CreateInstance(SDL_Window* window);
		
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
		);

#if _DEBUG
		static VKAPI_ATTR uint32_t VKAPI_CALL MessageCallback(
		VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
			uint32_t messageType,
			const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
			void* pUserData
			);

		void CreateDebugMessenger();

		void DestroyDebugMessenger();

		template<typename T>
		void SetDebugObjectName(const T& validHandle, const std::string& debugName)
		{
			if (!m_HasDebugUtils)
				return;

			std::string name = vk::to_string(validHandle.objectType) + "-" + debugName;

			vk::DebugUtilsObjectNameInfoEXT duoni;
			duoni.objectType = validHandle.objectType;
			duoni.objectHandle = reinterpret_cast<uint64_t>(static_cast<void*>(validHandle));
			duoni.pObjectName = name.c_str();
			device->setDebugUtilsObjectNameEXT(duoni, *dispatchLoaderDynamic);
		}
#else
		template<typename T>
		void SetDebugObjectName(const T& validHandle, const std::string& debugName)
		{}
#endif
	private:
		static inline std::mutex m_submit_mutex{};
		bool m_HasDebugUtils = false;
	public:
		void waitAndLockSubmits();
		
		void unlockSubmits();
		
		static VulkanContext* Get() noexcept;
		
		static void Remove() noexcept;
	};
}