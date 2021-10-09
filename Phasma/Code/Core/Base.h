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

struct SDL_Window;
enum VkDebugUtilsMessageSeverityFlagBitsEXT;
struct VkDebugUtilsMessengerCallbackDataEXT;

namespace pe
{
	class BufferVK;
}

namespace pe
{
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
}
