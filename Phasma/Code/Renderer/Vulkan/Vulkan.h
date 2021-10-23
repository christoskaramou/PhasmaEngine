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

#include "Renderer/Surface.h"
#include "Renderer/Swapchain.h"
#include "Renderer/CommandPool.h"
#include "Renderer/CommandBuffer.h"
#include "Renderer/Fence.h"
#include "Renderer/Semaphore.h"

#define UNIFIED_GRAPHICS_AND_TRANSFER_QUEUE

#define VULKAN (*VulkanContext::Get())
#define WIDTH VULKAN.surface.actualExtent.width
#define HEIGHT VULKAN.surface.actualExtent.height
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
	class Surface;
	
	class VulkanContext : public NoCopy, public NoMove
	{
	private:
		VulkanContext();
	
	public:
		~VulkanContext();
		
		void CreateInstance(SDL_Window* window);
		
		void CreateSurface();
		
		void GetSurfaceProperties();
		
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
		
		void CreateSwapchain(Surface* surface);
		
		void CreateCommandPools();
		
		void CreateDescriptorPool(uint32_t maxDescriptorSets);
		
		void CreateCmdBuffers(uint32_t bufferCount = 1);
		
		void CreateFences(uint32_t fenceCount);
		
		void CreateSemaphores(uint32_t semaphoresCount);
		
		void CreateDepth();
		
		void Init(SDL_Window* window);
		
		void Destroy();
		
		VkInstance instance;
		VkDebugUtilsMessengerEXT debugMessenger;
		VkPhysicalDevice gpu;
		VkPhysicalDeviceProperties gpuProperties;
		VkPhysicalDeviceFeatures gpuFeatures;
		VkDevice device;
		VkQueue graphicsQueue, computeQueue, transferQueue;
		CommandPool commandPool;
		CommandPool commandPool2;
		VkDescriptorPool descriptorPool;
		std::vector<VkQueueFamilyProperties> queueFamilyProperties;
		std::vector<CommandBuffer> dynamicCmdBuffers;
		std::vector<CommandBuffer> shadowCmdBuffers;
		std::vector<Fence> fences;
		std::vector<Semaphore> semaphores;
		VmaAllocator allocator = nullptr;
		
		SDL_Window* window;
		Surface surface;
		Swapchain swapchain;
		Image depth;
		int graphicsFamilyId, computeFamilyId, transferFamilyId;
		
		// Helpers
		void submit(
			uint32_t commandBufferCount, CommandBuffer* commandBuffer,
			PipelineStageFlags* waitStage,
			uint32_t waitSemaphoreCount, Semaphore* waitSemaphore,
			uint32_t signalSemaphoreCount, Semaphore* signalSemaphore,
			Fence* signalFence);
		
		void waitFence(Fence* fence);
		
		void submitAndWaitFence(
			uint32_t commandBuffersCount, CommandBuffer* commandBuffers,
			PipelineStageFlags* waitStages,
			uint32_t waitSemaphoresCount, Semaphore* waitSemaphores,
			uint32_t signalSemaphoresCount, Semaphore* signalSemaphores);

		void Present(
			uint32_t swapchainCount, Swapchain* swapchains,
			uint32_t* imageIndices,
			uint32_t semaphorescount, Semaphore* semaphores);

#if _DEBUG
		static VKAPI_ATTR uint32_t VKAPI_CALL MessageCallback(
		VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
			uint32_t messageType,
			const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
			void* pUserData);

		void CreateDebugMessenger();

		void DestroyDebugMessenger();
#endif
	private:
		static inline std::mutex m_submit_mutex{};
		bool m_HasDebugUtils = false;
	public:
		void waitAndLockSubmits();
		
		void waitDeviceIdle();

		void waitGraphicsQueue();
		
		void unlockSubmits();
		
		static VulkanContext* Get() noexcept;
		
		static void Remove() noexcept;
	};
}