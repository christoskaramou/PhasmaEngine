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

#include "Core/Settings.h"

namespace pe
{
	inline size_t NextID()
	{
		static size_t ID = 0;
		return ID++;
	}

	template<class T>
	inline size_t GetTypeID()
	{
		static size_t typeID = NextID();
		return typeID;
	}

	template<class VK_TYPE, class DX_TYPE>
	class ApiHandle final
	{
		static_assert(std::is_pointer_v<VK_TYPE>, "ApiHandle type is not a pointer");
		static_assert(std::is_pointer_v<DX_TYPE>, "ApiHandle type is not a pointer");
	public:
		ApiHandle() : m_handle(nullptr) {};

		ApiHandle(const VK_TYPE& handle) : m_handle(handle) {}

		ApiHandle(const DX_TYPE& handle) : m_handle(handle) {}

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

	using SampleCountFlagBits = uint32_t;
	using Format = uint32_t;
	using ColorSpace = uint32_t;
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
	using PresentMode = uint32_t;

	using CommandBufferHandle = ApiHandle<VkCommandBuffer, Placeholder*>;
	using DescriptorSetLayoutHandle = ApiHandle<VkDescriptorSetLayout, Placeholder*>;
	using DescriptorSetHandle = ApiHandle<VkDescriptorSet, Placeholder*>;
	using FrameBufferHandle = ApiHandle<VkFramebuffer, Placeholder*>;
	using ImageHandle = ApiHandle<VkImage, Placeholder*>;
	using ImageViewHandle = ApiHandle<VkImageView, Placeholder*>;
	using SamplerHandle = ApiHandle<VkSampler, Placeholder*>;
	using RenderPassHandle = ApiHandle<VkRenderPass, Placeholder*>;
	using CommandPoolHandle = ApiHandle<VkCommandPool, Placeholder*>;
	using BufferHandle = ApiHandle<VkBuffer, Placeholder*>;
	using PipelineCacheHandle = ApiHandle<VkPipelineCache, Placeholder*>;
	using PipelineLayoutHandle = ApiHandle<VkPipelineLayout, Placeholder*>;
	using PipelineHandle = ApiHandle<VkPipeline, Placeholder*>;
	using FenceHandle = ApiHandle<VkFence, Placeholder*>;
	using SemaphoreHandle = ApiHandle<VkSemaphore, Placeholder*>;
	using QueryPoolHandle = ApiHandle<VkQueryPool, Placeholder*>;
	using SwapchainHandle = ApiHandle<VkSwapchainKHR, Placeholder*>;
	using DeviceHandle = ApiHandle<VkDevice, Placeholder*>;
	using SurfaceHandle = ApiHandle<VkSurfaceKHR, Placeholder*>;
	using InstanceHandle = ApiHandle<VkInstance, Placeholder*>;
	using GpuHandle = ApiHandle<VkPhysicalDevice, Placeholder*>;
	using DebugMessengerHandle = ApiHandle<VkDebugUtilsMessengerEXT, Placeholder*>;
	using QueueHandle = ApiHandle<VkQueue, Placeholder*>;
	using DescriptorPoolHandle = ApiHandle<VkDescriptorPool, Placeholder*>;
}
