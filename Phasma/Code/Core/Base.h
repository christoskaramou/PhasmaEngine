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

#include <memory>
#include <type_traits>
#include <variant>

struct VkImage_T;
struct VkImageView_T;
struct VkSampler_T;
struct VkCommandBuffer_T;
struct VkDescriptorSetLayout_T;
struct VkDescriptorSet_T;
struct VkPipelineCache_T;
struct VkPipelineLayout_T;
struct VkPipeline_T;
struct VkRenderPass_T;
struct VkFramebuffer_T;
struct VkCommandPool_T;
struct VkBuffer_T;
struct VkFence_T;
struct VkSemaphore_T;
struct VkQueryPool_T;

using SampleCountFlagBits = uint32_t;
using Format = uint32_t;
using ImageLayout = uint32_t;
using ImageTiling = uint32_t;
using Filter = uint32_t;
using ImageViewType = uint32_t;
using SamplerAddressMode = uint32_t;
using BorderColor = uint32_t;
using CompareOp = uint32_t;
using SamplerMipmapMode = uint32_t;
using ImageCreateFlags = uint32_t;
using PipelineStageFlags = uint32_t;
using AccessFlags = uint32_t;
using MemoryPropertyFlags = uint32_t;
using BufferUsageFlags = uint32_t;
using ImageAspectFlags = uint32_t;
using ImageUsageFlags = uint32_t;
using ImageType = uint32_t;
using VertexInputRate = uint32_t;
using DynamicState = uint32_t;
using ShaderStageFlags = uint32_t;
using DescriptorType = uint32_t;
using AttachmentDescriptionFlags = uint32_t;
using AttachmentLoadOp = uint32_t;
using AttachmentStoreOp = uint32_t;
using IndexType = uint32_t;
using BlendFactor = uint32_t;
using BlendOp = uint32_t;
using ColorComponentFlags = uint32_t;

struct SDL_Window;
enum VkDebugUtilsMessageSeverityFlagBitsEXT;
struct VkDebugUtilsMessengerCallbackDataEXT;

namespace vk
{
	class RenderPass;
	enum class Format;
	class ImageView;
	class Framebuffer;
	class CommandBuffer;
	class DescriptorSetLayout;
	class DescriptorSet;
	class Device;
	struct VertexInputBindingDescription;
	struct VertexInputAttributeDescription;
	struct PipelineColorBlendAttachmentState;
	enum class DynamicState;
	class Pipeline;
	class PipelineLayout;
	struct Extent2D;
	class Instance;
	class DebugUtilsMessengerEXT;
	class PhysicalDevice;
	struct PhysicalDeviceProperties;
	struct PhysicalDeviceFeatures;
	class Queue;
	class CommandPool;
	class DescriptorPool;
	class DispatchLoaderDynamic;
	struct QueueFamilyProperties;
	class Fence;
	class Semaphore;
	class Image;
	class ImageView;
	class Sampler;
	enum class ImageLayout;
	enum class ImageTiling;
	enum class Filter;
	enum class ImageViewType;
	enum class SamplerAddressMode;
	enum class BorderColor;
	enum class CompareOp;
	enum class SamplerMipmapMode;
	class Buffer;
	class SurfaceKHR;
	struct SurfaceCapabilitiesKHR;
	struct SurfaceFormatKHR;
	enum class PresentModeKHR;
	class SwapchainKHR;
	class QueryPool;
	class PipelineCache;

	template<class T1> class Flags;
	enum class PipelineStageFlagBits : uint32_t; using PipelineStageFlags = Flags<PipelineStageFlagBits>;
	enum class ImageCreateFlagBits : uint32_t; using ImageCreateFlags = Flags<ImageCreateFlagBits>;
	enum class AccessFlagBits : uint32_t; using AccessFlags = Flags<AccessFlagBits>;
	enum class ImageAspectFlagBits : uint32_t; using ImageAspectFlags = Flags<ImageAspectFlagBits>;
	enum class ImageUsageFlagBits : uint32_t; using ImageUsageFlags = Flags<ImageUsageFlagBits>;
	enum class MemoryPropertyFlagBits : uint32_t; using MemoryPropertyFlags = Flags<MemoryPropertyFlagBits>;
	enum class SampleCountFlagBits : uint32_t; using SampleCountFlags = Flags<SampleCountFlagBits>;

	using Bool32 = uint32_t;

	template <typename T> class ArrayProxy;
}

namespace pe
{
	template<class VK_TYPE, class DX_TYPE>
	class ApiHandle final
	{
		static_assert(std::is_pointer_v<VK_TYPE>, "ApiHandle type is not a pointer");
		static_assert(std::is_pointer_v<DX_TYPE>, "ApiHandle type is not a pointer");
	public:
		ApiHandle() : m_handle(nullptr) {};

		ApiHandle(VK_TYPE handle) : m_handle(handle) {}

		ApiHandle(DX_TYPE handle) : m_handle(handle) {}

		operator VK_TYPE () { return static_cast<VK_TYPE>(m_handle); }

		operator DX_TYPE () { return static_cast<DX_TYPE>(m_handle); }

		void* raw() { return m_handle; }

		operator bool()
		{
			return m_handle != nullptr;
		}				    
						    
		bool operator!()    
		{				    
			return m_handle == nullptr;
		}

	private:
		void* m_handle;
	};

	template<class T, class VK_TYPE, class DX_TYPE>
	std::vector<T> ApiHandleVectorCreate(std::vector<ApiHandle<VK_TYPE, DX_TYPE>>& apiHandles)
	{
		static_assert(std::is_pointer_v<T>, "T type is not a pointer");
		static_assert(std::is_pointer_v<VK_TYPE>, "ApiHandle type is not a pointer");
		static_assert(std::is_pointer_v<DX_TYPE>, "ApiHandle type is not a pointer");
		static_assert(std::is_same_v<T, VK_TYPE> || std::is_same_v<T, DX_TYPE>, "T does not match any of ApiHandle types");

		std::vector<T> copyVec(apiHandles.size());

		for (int i = 0; i < apiHandles.size(); i++)
			copyVec[i] = apiHandles[i];

		return copyVec;
	}

	template<class T, class VK_TYPE, class DX_TYPE>
	std::vector<T> ApiHandleVectorCreate(uint32_t count, ApiHandle<VK_TYPE, DX_TYPE>* apiHandles)
	{
		static_assert(std::is_pointer_v<T>, "T type is not a pointer");
		static_assert(std::is_pointer_v<VK_TYPE>, "ApiHandle type is not a pointer");
		static_assert(std::is_pointer_v<DX_TYPE>, "ApiHandle type is not a pointer");
		static_assert(std::is_same_v<T, VK_TYPE> || std::is_same_v<T, DX_TYPE>, "T does not match any of ApiHandle types");
		static_assert(sizeof(VK_TYPE) == sizeof(DX_TYPE));

		std::vector<T> copyVec(count);

		for (uint32_t i = 0; i < count; i++)
			copyVec[i] = apiHandles[i];

		return copyVec;
	}

	template<class T>
	using SPtr = std::shared_ptr<T>;

	template<class T>
	SPtr<T> make_sptr(T& obj)
	{
		return std::make_shared<T>(obj);
	}

	template<class T>
	SPtr<T> make_sptr(const T& obj)
	{
		return std::make_shared<T>(obj);
	}

	template<class T>
	SPtr<T> make_sptr(T&& obj)
	{
		return std::make_shared<T>(std::forward<T>(obj));
	}

	template<class T, class ... Params>
	SPtr<T> make_sptr(Params&& ... params)
	{
		return std::make_shared<T>(std::forward<Params>(params)...);
	}

	class NoCopy
	{
	public:
		NoCopy() = default;
		
		NoCopy(const NoCopy&) = delete;
		
		NoCopy& operator=(const NoCopy&) = delete;
		
		NoCopy(NoCopy&&) = default;
		
		NoCopy& operator=(NoCopy&&) = default;
	};
	
	class NoMove
	{
	public:
		NoMove() = default;

		NoMove(const NoMove&) = default;

		NoMove& operator=(const NoMove&) = default;

		NoMove(NoMove&&) = delete;

		NoMove& operator=(NoMove&&) = delete;
	};

	struct Placeholder {};

	class BufferVK;
	using CommandBufferHandle = ApiHandle<VkCommandBuffer_T*, Placeholder*>;
	using DescriptorSetLayoutHandle = ApiHandle<VkDescriptorSetLayout_T*, Placeholder*>;
	using DescriptorSetHandle = ApiHandle<VkDescriptorSet_T*, Placeholder*>;
	using FrameBufferHandle = ApiHandle<VkFramebuffer_T*, Placeholder*>;
	using ImageHandle = ApiHandle<VkImage_T*, Placeholder*>;
	using ImageViewHandle = ApiHandle<VkImageView_T*, Placeholder*>;
	using SamplerHandle = ApiHandle<VkSampler_T*, Placeholder*>;
	using RenderPassHandle = ApiHandle<VkRenderPass_T*, Placeholder*>;
	using CommandPoolHandle = ApiHandle<VkCommandPool_T*, Placeholder*>;
	using BufferHandle = ApiHandle<VkBuffer_T*, Placeholder*>;
	using PipelineCacheHandle = ApiHandle<VkPipelineCache_T*, Placeholder*>;
	using PipelineLayoutHandle = ApiHandle<VkPipelineLayout_T*, Placeholder*>;
	using PipelineHandle = ApiHandle<VkPipeline_T*, Placeholder*>;
	using FenceHandle = ApiHandle<VkFence_T*, Placeholder*>;
	using SemaphoreHandle = ApiHandle<VkSemaphore_T*, Placeholder*>;
	using QueryPoolHandle = ApiHandle<VkQueryPool_T*, Placeholder*>;
}
