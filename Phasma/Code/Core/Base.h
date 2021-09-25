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
	using Ref = std::shared_ptr<T>;

	template<class T>
	Ref<T> make_ref(T& obj)
	{
		return std::make_shared<T>(obj);
	}

	template<class T>
	Ref<T> make_ref(const T& obj)
	{
		return std::make_shared<T>(obj);
	}

	template<class T>
	Ref<T> make_ref(T&& obj)
	{
		return std::make_shared<T>(std::forward<T>(obj));
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

	//template<class T>
	//class Imposter : public T
	//{
	//	using BaseType = T;
	//};
	//
	//using ImpRenderPass = Imposter<vk::RenderPass>;
	//using ImpFormat = Imposter<vk::Format>;
	//using ImpImageView = Imposter<vk::ImageView>;
	//using ImpFramebuffer = Imposter<vk::Framebuffer>;
	//using ImpCommandBuffer = Imposter<vk::CommandBuffer>;
	//using ImpDescriptorSetLayout = Imposter<vk::DescriptorSetLayout>;
	//using ImpDescriptorSet = Imposter<vk::DescriptorSet>;
	//using ImpDevice = Imposter<vk::Device>;
	//using ImpVertexInputBindingDescription = Imposter<vk::VertexInputBindingDescription>;
	//using ImpVertexInputAttributeDescription = Imposter<vk::VertexInputAttributeDescription>;
	//using ImpPipelineColorBlendAttachmentState = Imposter<vk::PipelineColorBlendAttachmentState>;
	//using ImpDynamicState = Imposter<vk::DynamicState>;
	//using ImpPipeline = Imposter<vk::Pipeline>;
	//using ImpPipelineLayout = Imposter<vk::PipelineLayout>;
	//using ImpExtent2D = Imposter<vk::Extent2D>;
	//using ImpInstance = Imposter<vk::Instance>;
	//using ImpDebugUtilsMessengerEXT = Imposter<vk::DebugUtilsMessengerEXT>;
	//using ImpPhysicalDevice = Imposter<vk::PhysicalDevice>;
	//using ImpPhysicalDeviceProperties = Imposter<vk::PhysicalDeviceProperties>;
	//using ImpPhysicalDeviceFeatures = Imposter<vk::PhysicalDeviceFeatures>;
	//using ImpQueue = Imposter<vk::Queue>;
	//using ImpCommandPool = Imposter<vk::CommandPool>;
	//using ImpDescriptorPool = Imposter<vk::DescriptorPool>;
	//using ImpDispatchLoaderDynamic = Imposter<vk::DispatchLoaderDynamic>;
	//using ImpQueueFamilyProperties = Imposter<vk::QueueFamilyProperties>;
	//using ImpFence = Imposter<vk::Fence>;
	//using ImpSemaphore = Imposter<vk::Semaphore>;
	//using ImpPipelineStageFlags = Imposter<vk::PipelineStageFlags>;
	//using ImpImage = Imposter<vk::Image>;
	//using ImpImageView = Imposter<vk::ImageView>;
	//using ImpSampler = Imposter<vk::Sampler>;
	//using ImpImageLayout = Imposter<vk::ImageLayout>;
	//using ImpImageTiling = Imposter<vk::ImageTiling>;
	//using ImpFilter = Imposter<vk::Filter>;
	//using ImpImageCreateFlags = Imposter<vk::ImageCreateFlags>;
	//using ImpImageViewType = Imposter<vk::ImageViewType>;
	//using ImpSamplerAddressMode = Imposter<vk::SamplerAddressMode>;
	//using ImpBorderColor = Imposter<vk::BorderColor>;
	//using ImpCompareOp = Imposter<vk::CompareOp>;
	//using ImpSamplerMipmapMode = Imposter<vk::SamplerMipmapMode>;
	//using ImpAccessFlags = Imposter<vk::AccessFlags>;
	//using ImpImageAspectFlags = Imposter<vk::ImageAspectFlags>;
	//using ImpImageUsageFlags = Imposter<vk::ImageUsageFlags>;
	//using ImpMemoryPropertyFlags = Imposter<vk::MemoryPropertyFlags>;
	//using ImpBuffer = Imposter<vk::Buffer>;
	//using ImpSurfaceKHR = Imposter<vk::SurfaceKHR>;
	//using ImpSurfaceCapabilitiesKHR = Imposter<vk::SurfaceCapabilitiesKHR>;
	//using ImpSurfaceFormatKHR = Imposter<vk::SurfaceFormatKHR>;
	//using ImpPresentModeKHR = Imposter<vk::PresentModeKHR>;
	//using ImpSwapchainKHR = Imposter<vk::SwapchainKHR>;
	//using ImpSampleCountFlagBits = Imposter<vk::SampleCountFlagBits>;
}
