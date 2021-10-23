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

#include "PhasmaPch.h"
#include "Vulkan.h"
#include "ECS/Context.h"
#include "Systems/RendererSystem.h"
#include "Renderer/CommandPool.h"
#include "Renderer/CommandBuffer.h"
#include "Renderer/Fence.h"
#include "Renderer/Semaphore.h"
#include <iostream>

namespace pe
{	
	VulkanContext::VulkanContext()
	{
		device = {};
		queueFamilyProperties = {};
		dynamicCmdBuffers = {};
		shadowCmdBuffers = {};
		fences = {};
		semaphores = {};
		
		window = nullptr;
		graphicsFamilyId = 0;
		computeFamilyId = 0;
		transferFamilyId = 0;
	}
	
	VulkanContext::~VulkanContext()
	{
	
	}
	
	void VulkanContext::CreateInstance(SDL_Window* window)
	{
		std::vector<const char*> instanceExtensions;
		std::vector<const char*> instanceLayers;
		std::vector<VkValidationFeatureEnableEXT> enabledFeatures;
		VkValidationFeaturesEXT validationFeatures{};
		validationFeatures.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;

		// === Extentions ==============================
		unsigned extCount;
		if (!SDL_Vulkan_GetInstanceExtensions(window, &extCount, nullptr))
			throw std::runtime_error(SDL_GetError());
		instanceExtensions.resize(extCount);
		if (!SDL_Vulkan_GetInstanceExtensions(window, &extCount, instanceExtensions.data()))
			throw std::runtime_error(SDL_GetError());
		// =============================================

#ifdef _DEBUG
		// === Debug Extensions ========================
		uint32_t propertyCount;
		vkEnumerateInstanceExtensionProperties(nullptr, &propertyCount, nullptr);

		std::vector<VkExtensionProperties> extensions(propertyCount);
		vkEnumerateInstanceExtensionProperties(nullptr, &propertyCount, extensions.data());

		for (auto& extension : extensions)
		{
			if (std::string(extension.extensionName) == "VK_EXT_debug_utils")
			{
				instanceExtensions.push_back("VK_EXT_debug_utils");
				m_HasDebugUtils = true;
			}
		}
		// =============================================

		// === Debug Layers ============================
		// To use these debug layers, here is assumed VulkanSDK is installed
		// Also VK_LAYER_PATH environmental variable has to be created and target the bin
		// e.g VK_LAYER_PATH = C:\VulkanSDK\1.2.189.1\Bin

		uint32_t layersCount;
		vkEnumerateInstanceLayerProperties(&layersCount, nullptr);

		std::vector<VkLayerProperties> layers(layersCount);
		vkEnumerateInstanceLayerProperties(&layersCount, layers.data());

		for (auto& layer : layers)
		{
			if (std::string(layer.layerName) == "VK_LAYER_KHRONOS_validation")
				instanceLayers.push_back("VK_LAYER_KHRONOS_validation");
		}
		// =============================================

		// === Validation Features =====================
		enabledFeatures.push_back(VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT);
		//enabledFeatures.push_back(VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT);
		//enabledFeatures.push_back(VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_RESERVE_BINDING_SLOT_EXT);

		validationFeatures.enabledValidationFeatureCount = static_cast<uint32_t>(enabledFeatures.size());
		validationFeatures.pEnabledValidationFeatures = enabledFeatures.data();
		// =============================================
#endif
		uint32_t apiVersion;
		vkEnumerateInstanceVersion(&apiVersion);

		VkApplicationInfo appInfo{};
		appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		appInfo.pApplicationName = "VulkanMonkey3D";
		appInfo.pEngineName = "VulkanMonkey3D";
		appInfo.apiVersion = apiVersion;
		
		VkInstanceCreateInfo instInfo{};
		instInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
		instInfo.pNext = &validationFeatures;
		instInfo.pApplicationInfo = &appInfo;
		instInfo.enabledLayerCount = static_cast<uint32_t>(instanceLayers.size());
		instInfo.ppEnabledLayerNames = instanceLayers.data();
		instInfo.enabledExtensionCount = static_cast<uint32_t>(instanceExtensions.size());
		instInfo.ppEnabledExtensionNames = instanceExtensions.data();
		
		vkCreateInstance(&instInfo, nullptr, &instance);
	}
	
#if _DEBUG
	std::string GetDebugMessageString(VkDebugUtilsMessageTypeFlagsEXT value )
	{
		switch (value)
		{
		case VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT: return "General";
		case VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT: return "Validation";
		case VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT: return "Performance";
		}

		return "Unknown";
	}

	std::string GetDebugSeverityString(VkDebugUtilsMessageSeverityFlagBitsEXT value)
	{
		switch (value)
		{
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT: return "Verbose";
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT: return "Info";
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT: return "Warning";
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT: return "Error";
		}

		return "Unknown";
	}

	VKAPI_ATTR uint32_t VKAPI_CALL VulkanContext::MessageCallback(
			VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
			uint32_t messageType,
			const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
			void* pUserData
	)
	{
		if (messageSeverity > VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
			std::cerr
					<< GetDebugMessageString(messageType) << " "
					<< GetDebugSeverityString(messageSeverity) << " from \""
					<< pCallbackData->pMessageIdName << "\": "
					<< pCallbackData->pMessage << std::endl;
		
		return VK_FALSE;
	}
	
	void VulkanContext::CreateDebugMessenger()
	{		
		VkDebugUtilsMessengerCreateInfoEXT dumci{};
		dumci.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
		dumci.messageSeverity =
			VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
			VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
			VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
			VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
		dumci.messageType =
			VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
			VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
			VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
		dumci.pfnUserCallback = VulkanContext::MessageCallback;

		auto vkCreateDebugUtilsMessengerEXT = (PFN_vkCreateDebugUtilsMessengerEXT) vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
		vkCreateDebugUtilsMessengerEXT(instance, &dumci, nullptr, &debugMessenger);
	}
	
	void VulkanContext::DestroyDebugMessenger()
	{
		auto vkDestroyDebugUtilsMessengerEXT = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
		vkDestroyDebugUtilsMessengerEXT(instance, debugMessenger, nullptr);
	}
#endif
	
	void VulkanContext::CreateSurface()
	{
		surface.Create(window);
	}
	
	void VulkanContext::GetSurfaceProperties()
	{
		surface.FindProperties();
	}
	
	void VulkanContext::GetGpu()
	{
		uint32_t gpuCount;
		vkEnumeratePhysicalDevices(instance, &gpuCount, nullptr);

		std::vector<VkPhysicalDevice> gpuList(gpuCount);
		vkEnumeratePhysicalDevices(instance, &gpuCount, gpuList.data());

		std::vector<VkPhysicalDevice> validGpuList{};
		VkPhysicalDevice discreteGpu;
		
		for (auto& GPU : gpuList)
		{
			uint32_t queueFamPropCount;
			vkGetPhysicalDeviceQueueFamilyProperties(GPU, &queueFamPropCount, nullptr);

			queueFamilyProperties.resize(queueFamPropCount);
			vkGetPhysicalDeviceQueueFamilyProperties(GPU, &queueFamPropCount, queueFamilyProperties.data());

			VkQueueFlags flags{};
			
			for (auto& qfp : queueFamilyProperties)
			{
				if (qfp.queueFlags & VK_QUEUE_GRAPHICS_BIT)
					flags |= VK_QUEUE_GRAPHICS_BIT;
				if (qfp.queueFlags & VK_QUEUE_COMPUTE_BIT)
					flags |= VK_QUEUE_COMPUTE_BIT;
				if (qfp.queueFlags & VK_QUEUE_TRANSFER_BIT)
					flags |= VK_QUEUE_TRANSFER_BIT;
			}
			
			if (flags & VK_QUEUE_GRAPHICS_BIT &&
			    flags & VK_QUEUE_COMPUTE_BIT &&
			    flags & VK_QUEUE_TRANSFER_BIT)
			{
				VkPhysicalDeviceProperties properties;
				vkGetPhysicalDeviceProperties(GPU, &properties);
				if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
				{
					discreteGpu = GPU;
					break;
				}

				validGpuList.push_back(GPU);
			}
		}

		gpu = discreteGpu ? discreteGpu : validGpuList[0];
		GetGraphicsFamilyId();
		GetComputeFamilyId();
		GetTransferFamilyId();
		vkGetPhysicalDeviceProperties(gpu, &gpuProperties);
		vkGetPhysicalDeviceFeatures(gpu, &gpuFeatures);
	}
	
	void VulkanContext::GetGraphicsFamilyId()
	{
#ifdef UNIFIED_GRAPHICS_AND_TRANSFER_QUEUE
		VkQueueFlags flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_TRANSFER_BIT;
#else
		VkQueueFlags flags = VK_QUEUE_GRAPHICS_BIT;
#endif
		auto& properties = queueFamilyProperties;
		for (uint32_t i = 0; i < properties.size(); i++)
		{
			//find graphics queue family index
			VkBool32 suported;
			vkGetPhysicalDeviceSurfaceSupportKHR(gpu, i, surface.surface, &suported);
			if (properties[i].queueFlags & flags && suported)
			{
				graphicsFamilyId = i;
				return;
			}
		}
		graphicsFamilyId = -1;
	}
	
	void VulkanContext::GetTransferFamilyId()
	{
#ifdef UNIFIED_GRAPHICS_AND_TRANSFER_QUEUE1
		transferFamilyId = graphicsFamilyId;
#else
		VkQueueFlags flags = VK_QUEUE_TRANSFER_BIT;
		auto& properties = queueFamilyProperties;
		// prefer different families for different queue types, thus the reverse check
		for (int i = static_cast<int>(properties.size()) - 1; i >= 0; --i)
		{
			//find transfer queue family index
			if (properties[i].queueFlags & flags)
			{
				transferFamilyId = i;
				return;
			}
		}
		transferFamilyId = -1;
#endif
	}
	
	void VulkanContext::GetComputeFamilyId()
	{
		VkQueueFlags flags = VK_QUEUE_TRANSFER_BIT;
		auto& properties = queueFamilyProperties;
		// prefer different families for different queue types, thus the reverse check
		for (int i = static_cast<int>(properties.size()) - 1; i >= 0; --i)
		{
			//find compute queue family index
			if (properties[i].queueFlags & flags)
			{
				computeFamilyId = i;
				return;
			}
		}
		computeFamilyId = -1;
	}
	
	
	void VulkanContext::CreateDevice()
	{
		uint32_t propsCount;
		vkEnumerateDeviceExtensionProperties(gpu, nullptr, &propsCount, nullptr);

		std::vector<VkExtensionProperties> extensionProperties(propsCount);
		vkEnumerateDeviceExtensionProperties(gpu, nullptr, &propsCount, extensionProperties.data());
		
		std::vector<const char*> deviceExtensions {};
		for (auto& extProps : extensionProperties)
		{
			if (std::string(extProps.extensionName) == VK_KHR_SWAPCHAIN_EXTENSION_NAME)
				deviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
		}
		float priorities[] {1.0f}; // range : [0.0, 1.0]
		
		std::vector<VkDeviceQueueCreateInfo> queueCreateInfos {};
		
		// graphics queue
		VkDeviceQueueCreateInfo queueCreateInfo{};
		queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queueCreateInfo.queueFamilyIndex = graphicsFamilyId;
		queueCreateInfo.queueCount = 1;
		queueCreateInfo.pQueuePriorities = priorities;
		queueCreateInfos.push_back(queueCreateInfo);
		
		// compute queue
		if (computeFamilyId != graphicsFamilyId)
		{
			queueCreateInfo.queueFamilyIndex = computeFamilyId;
			queueCreateInfo.queueCount = 1;
			queueCreateInfo.pQueuePriorities = priorities;
			queueCreateInfos.push_back(queueCreateInfo);
		}
		
		// transfer queue
		if (transferFamilyId != graphicsFamilyId && transferFamilyId != computeFamilyId)
		{
			queueCreateInfo.queueFamilyIndex = transferFamilyId;
			queueCreateInfo.queueCount = 1;
			queueCreateInfo.pQueuePriorities = priorities;
			queueCreateInfos.push_back(queueCreateInfo);
		}
		
		VkDeviceCreateInfo deviceCreateInfo{};
		deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		deviceCreateInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
		deviceCreateInfo.pQueueCreateInfos = queueCreateInfos.data();
		deviceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
		deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions.data();
		deviceCreateInfo.pEnabledFeatures = &gpuFeatures;
		
		vkCreateDevice(gpu, &deviceCreateInfo, nullptr, &device);
	}
	
	void VulkanContext::CreateAllocator()
	{
		uint32_t apiVersion;
		vkEnumerateInstanceVersion(&apiVersion);

		VmaAllocatorCreateInfo allocator_info = {};
		allocator_info.physicalDevice = gpu;
		allocator_info.device = device;
		allocator_info.instance = instance;
		allocator_info.vulkanApiVersion = apiVersion;
		
		vmaCreateAllocator(&allocator_info, &allocator);
	}
	
	void VulkanContext::GetGraphicsQueue()
	{
		vkGetDeviceQueue(device, graphicsFamilyId, 0, &graphicsQueue);
	}
	
	void VulkanContext::GetTransferQueue()
	{
		vkGetDeviceQueue(device, transferFamilyId, 0, &transferQueue);
	}
	
	void VulkanContext::GetComputeQueue()
	{
		vkGetDeviceQueue(device, computeFamilyId, 0, &computeQueue);
	}
	
	void VulkanContext::GetQueues()
	{
		GetGraphicsQueue();
		GetTransferQueue();
		GetComputeQueue();
	}
	
	void VulkanContext::CreateSwapchain(Surface* surface)
	{
		swapchain.Create(surface);
	}
	
	void VulkanContext::CreateCommandPools()
	{
		commandPool.Create(graphicsFamilyId);
		commandPool2.Create(graphicsFamilyId);
	}
	
	void VulkanContext::CreateDescriptorPool(uint32_t maxDescriptorSets)
	{
		std::vector<VkDescriptorPoolSize> descPoolsize(4);
		descPoolsize[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		descPoolsize[0].descriptorCount = maxDescriptorSets;
		descPoolsize[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		descPoolsize[1].descriptorCount = maxDescriptorSets;
		descPoolsize[2].type = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
		descPoolsize[2].descriptorCount = maxDescriptorSets;
		descPoolsize[3].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		descPoolsize[3].descriptorCount = maxDescriptorSets;
		
		VkDescriptorPoolCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		createInfo.poolSizeCount = static_cast<uint32_t>(descPoolsize.size());
		createInfo.pPoolSizes = descPoolsize.data();
		createInfo.maxSets = maxDescriptorSets;
		
		vkCreateDescriptorPool(device, &createInfo, nullptr, &descriptorPool);
	}
	
	void VulkanContext::CreateCmdBuffers(uint32_t bufferCount)
	{
		dynamicCmdBuffers.resize(bufferCount);
		for (uint32_t i = 0; i < bufferCount; i++)
			dynamicCmdBuffers[i].Create(commandPool);

		shadowCmdBuffers.resize(bufferCount * SHADOWMAP_CASCADES);
		for (uint32_t i = 0; i < bufferCount * SHADOWMAP_CASCADES; i++)
			shadowCmdBuffers[i].Create(commandPool);
	}
	
	void VulkanContext::CreateFences(uint32_t fenceCount)
	{
		fences.resize(fenceCount);
		for (uint32_t i = 0; i < fenceCount; i++)
			fences[i].Create();
	}
	
	void VulkanContext::CreateSemaphores(uint32_t semaphoresCount)
	{
		semaphores.resize(semaphoresCount);
		for (uint32_t i = 0; i < semaphoresCount; i++)
			semaphores[i].Create();
	}
	
	void VulkanContext::CreateDepth()
	{
		depth.format = VK_FORMAT_UNDEFINED;
		std::vector<VkFormat> candidates =
		{
			VK_FORMAT_D32_SFLOAT_S8_UINT,
			VK_FORMAT_D32_SFLOAT,
			VK_FORMAT_D24_UNORM_S8_UINT
		};

		for (auto& format : candidates)
		{
			VkFormatProperties props;
			vkGetPhysicalDeviceFormatProperties(gpu, format, &props);
			if ((props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) ==
				VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
			{
				depth.format = (Format)format;
				break;
			}
		}
		if (depth.format == VK_FORMAT_UNDEFINED)
			throw std::runtime_error("Depth format is undefined");
		
		depth.CreateImage(
			static_cast<uint32_t>(WIDTH_f * GUI::renderTargetsScale),
			static_cast<uint32_t>(HEIGHT_f * GUI::renderTargetsScale),
			VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
		);
		depth.CreateImageView(VK_IMAGE_ASPECT_DEPTH_BIT);
		
		depth.addressMode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		depth.maxAnisotropy = 1.f;
		depth.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
		depth.samplerCompareEnable = VK_TRUE;
		depth.CreateSampler();
		depth.name = "DepthImage";
		
		depth.TransitionImageLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
	}
	
	void VulkanContext::Init(SDL_Window* window)
	{
		this->window = window;

		CreateInstance(window);
#ifdef _DEBUG
		CreateDebugMessenger();
#endif
		CreateSurface();
		GetGpu();
		GetSurfaceProperties();
		CreateDevice();
		CreateAllocator();
		GetQueues();
		CreateCommandPools();
		CreateSwapchain(&surface);
		CreateDescriptorPool(15000); // max number of all descriptor sets to allocate
		CreateCmdBuffers(SWAPCHAIN_IMAGES);
		CreateSemaphores(SWAPCHAIN_IMAGES * 3);
		CreateFences(SWAPCHAIN_IMAGES);
		CreateDepth();
	}
	
	void VulkanContext::Destroy()
	{
		waitDeviceIdle();
		
		for (auto& fence : fences)
			fence.Destroy();

		for (auto& semaphore : semaphores)
			semaphore.Destroy();
		
		depth.Destroy();
		
		if (descriptorPool)
		{
			vkDestroyDescriptorPool(device, descriptorPool, nullptr);
			descriptorPool = nullptr;
		}

		commandPool.Destroy();
		commandPool2.Destroy();
		
		swapchain.Destroy();
		
		if (device)
		{
			vkDestroyDevice(device, nullptr);
			device = nullptr;
		}
		
		surface.Destroy();

#ifdef _DEBUG
		DestroyDebugMessenger();
#endif
		
		if (instance)
			vkDestroyInstance(instance, nullptr);
	}
	
	void VulkanContext::submit(
			uint32_t commandBuffersCount, CommandBuffer* commandBuffers,
			PipelineStageFlags* waitStages,
			uint32_t waitSemaphoresCount, Semaphore* waitSemaphores,
			uint32_t signalSemaphoresCount, Semaphore* signalSemaphores,
			Fence* signalFence
	)
	{
		std::vector<VkCommandBuffer> commandBuffersVK(commandBuffersCount);
		for (uint32_t i = 0; i < commandBuffersCount; i++)
			commandBuffersVK[i] = commandBuffers[i].Handle();

		std::vector<VkSemaphore> waitSemaphoresVK(waitSemaphoresCount);
		for (uint32_t i = 0; i < waitSemaphoresCount; i++)
			waitSemaphoresVK[i] = waitSemaphores[i].handle;

		std::vector<VkSemaphore> signalSemaphoresVK(signalSemaphoresCount);
		for (uint32_t i = 0; i < signalSemaphoresCount; i++)
			signalSemaphoresVK[i] = signalSemaphores[i].handle;

		VkFence fence = nullptr;
		if (signalFence)
			fence = signalFence->handle;

		VkSubmitInfo si{};
		si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		si.waitSemaphoreCount = waitSemaphoresCount;
		si.pWaitSemaphores = waitSemaphoresVK.data();
		si.pWaitDstStageMask = waitStages;
		si.commandBufferCount = commandBuffersCount;
		si.pCommandBuffers = commandBuffersVK.data();
		si.signalSemaphoreCount = signalSemaphoresCount;
		si.pSignalSemaphores = signalSemaphoresVK.data();

		vkQueueSubmit(graphicsQueue, 1, &si, fence);
	}
	
	void VulkanContext::waitFence(Fence* fence)
	{
		VkFence fenceVK = fence->handle;
		if (vkWaitForFences(device, 1, &fenceVK, VK_TRUE, UINT64_MAX) != VK_SUCCESS)
			throw std::runtime_error("wait fences error!");
		vkResetFences(device, 1, &fenceVK);
	}
	
	void VulkanContext::submitAndWaitFence(
		uint32_t commandBuffersCount, CommandBuffer* commandBuffers,
		PipelineStageFlags* waitStages,
		uint32_t waitSemaphoresCount, Semaphore* waitSemaphores,
		uint32_t signalSemaphoresCount, Semaphore* signalSemaphores
	)
	{
		Fence fence;
		fence.Create(false);
		
		submit(
			commandBuffersCount, commandBuffers,
			waitStages,
			waitSemaphoresCount, waitSemaphores,
			signalSemaphoresCount, signalSemaphores,
			&fence);
		
		VkFence fenceVK = fence.handle;
		if (vkWaitForFences(device, 1, &fenceVK, VK_TRUE, UINT64_MAX) != VK_SUCCESS)
			throw std::runtime_error("wait fences error!");
		vkDestroyFence(device, fence.handle, nullptr);
	}

	void VulkanContext::Present(
		uint32_t swapchainCount, Swapchain* swapchains,
		uint32_t* imageIndices,
		uint32_t waitSemaphoreCount, Semaphore* waitSemaphores)
	{
		std::vector<VkSwapchainKHR> swapchainsVK(swapchainCount);
		for (uint32_t i = 0; i < swapchainCount; i++)
			swapchainsVK[i] = swapchains[i].handle;

		std::vector<VkSemaphore> waitSemaphoresVK(waitSemaphoreCount);
		for (uint32_t i = 0; i < waitSemaphoreCount; i++)
			waitSemaphoresVK[i] = waitSemaphores[i].handle;

		VkPresentInfoKHR pi{};
		pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		pi.waitSemaphoreCount = waitSemaphoreCount;
		pi.pWaitSemaphores = waitSemaphoresVK.data();
		pi.swapchainCount = swapchainCount;
		pi.pSwapchains = swapchainsVK.data();
		pi.pImageIndices = imageIndices;

		if (vkQueuePresentKHR(graphicsQueue, &pi) != VK_SUCCESS)
			throw std::runtime_error("Present error!");
	}
	
	void VulkanContext::waitAndLockSubmits()
	{
		m_submit_mutex.lock();
	}
	
	void VulkanContext::unlockSubmits()
	{
		m_submit_mutex.unlock();
	}
	
	VulkanContext* VulkanContext::Get() noexcept
	{
		static auto VkCTX = new VulkanContext();
		return VkCTX;
	}

	void VulkanContext::waitDeviceIdle()
	{
		vkDeviceWaitIdle(device);
	}

	void VulkanContext::waitGraphicsQueue()
	{
		vkQueueWaitIdle(graphicsQueue);
	}
	
	void VulkanContext::Remove() noexcept
	{
		if (Get())
			delete Get();
	}
}
