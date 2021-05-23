#include "PhasmaPch.h"
#include "Vulkan.h"
#include "../../ECS/Context.h"
#include "../../Renderer/Renderer.h"
#include <iostream>

namespace pe
{
	constexpr uint32_t SWAPCHAIN_IMAGES = 3;
	
	VulkanContext::VulkanContext()
	{
		instance = make_ref(vk::Instance());
		debugMessenger = make_ref(vk::DebugUtilsMessengerEXT());
		gpu = make_ref(vk::PhysicalDevice());
		gpuProperties = make_ref(vk::PhysicalDeviceProperties());
		gpuFeatures = make_ref(vk::PhysicalDeviceFeatures());
		device = make_ref(vk::Device());
		graphicsQueue = make_ref(vk::Queue());
		computeQueue = make_ref(vk::Queue());
		transferQueue = make_ref(vk::Queue());
		commandPool = make_ref(vk::CommandPool());
		commandPool2 = make_ref(vk::CommandPool());
		descriptorPool = make_ref(vk::DescriptorPool());
		dispatchLoaderDynamic = make_ref(vk::DispatchLoaderDynamic());
		queueFamilyProperties = make_ref(std::vector<vk::QueueFamilyProperties>());
		dynamicCmdBuffers = make_ref(std::vector<vk::CommandBuffer>());
		shadowCmdBuffers = make_ref(std::vector<vk::CommandBuffer>());
		fences = make_ref(std::vector<vk::Fence>());
		semaphores = make_ref(std::vector<vk::Semaphore>());
		
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
		vk::ValidationFeaturesEXT validationFeatures;
		std::vector<vk::ValidationFeatureEnableEXT> enabledFeatures;
		
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
		auto extensions = vk::enumerateInstanceExtensionProperties();
		for (auto& extension : extensions)
		{
			if (std::string(extension.extensionName.data()) == "VK_EXT_debug_utils")
				instanceExtensions.push_back("VK_EXT_debug_utils");
		}
		// =============================================
		
		// === Debug Layers ============================
		// To use these debug layers, here is assumed VulkanSDK is installed and Bin is in enviromental path
		auto layers = vk::enumerateInstanceLayerProperties();
		for (auto layer : layers)
		{
			if (std::string(layer.layerName.data()) == "VK_LAYER_KHRONOS_validation")
				instanceLayers.push_back("VK_LAYER_KHRONOS_validation");
		}
		// =============================================
		
		// === Validation Features =====================
		enabledFeatures.push_back(vk::ValidationFeatureEnableEXT::eBestPractices);
		//enabledFeatures.push_back(vk::ValidationFeatureEnableEXT::eGpuAssisted);
		//enabledFeatures.push_back(vk::ValidationFeatureEnableEXT::eGpuAssistedReserveBindingSlot);
		
		validationFeatures.enabledValidationFeatureCount = static_cast<uint32_t>(enabledFeatures.size());
		validationFeatures.pEnabledValidationFeatures = enabledFeatures.data();
		// =============================================
#endif
		
		vk::ApplicationInfo appInfo;
		appInfo.pApplicationName = "VulkanMonkey3D";
		appInfo.pEngineName = "VulkanMonkey3D";
		appInfo.apiVersion = vk::enumerateInstanceVersion();
		
		vk::InstanceCreateInfo instInfo;
		instInfo.pApplicationInfo = &appInfo;
		instInfo.enabledLayerCount = static_cast<uint32_t>(instanceLayers.size());
		instInfo.ppEnabledLayerNames = instanceLayers.data();
		instInfo.enabledExtensionCount = static_cast<uint32_t>(instanceExtensions.size());
		instInfo.ppEnabledExtensionNames = instanceExtensions.data();
		instInfo.pNext = &validationFeatures;
		
		instance = make_ref(vk::createInstance(instInfo));
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
					<< to_string(vk::DebugUtilsMessageTypeFlagBitsEXT(messageType)) << " "
					<< to_string(vk::DebugUtilsMessageSeverityFlagBitsEXT(messageSeverity)) << " from \""
					<< pCallbackData->pMessageIdName << "\": "
					<< pCallbackData->pMessage << std::endl;
		
		return VK_FALSE;
	}
	
	void VulkanContext::CreateDebugMessenger()
	{
		dispatchLoaderDynamic->init(*instance, vk::Device());
		
		vk::DebugUtilsMessengerCreateInfoEXT dumci;
		dumci.messageSeverity =
				vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose |
				vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo |
				vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
				vk::DebugUtilsMessageSeverityFlagBitsEXT::eError;
		dumci.messageType =
				vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
				vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance |
				vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation;
		dumci.pfnUserCallback = VulkanContext::MessageCallback;
		
		debugMessenger = make_ref(instance->createDebugUtilsMessengerEXT(dumci, nullptr, *dispatchLoaderDynamic));
	}
	
	void VulkanContext::DestroyDebugMessenger()
	{
		instance->destroyDebugUtilsMessengerEXT(*debugMessenger, nullptr, *dispatchLoaderDynamic);
	}
	
	void VulkanContext::CreateSurface(Context* ctx)
	{
		surface.Create(ctx);
	}
	
	void VulkanContext::GetSurfaceProperties(Context* ctx)
	{
		surface.FindProperties(ctx);
	}
	
	void VulkanContext::GetGpu()
	{
		std::vector<vk::PhysicalDevice> gpuList = instance->enumeratePhysicalDevices();
		
		for (auto& GPU : gpuList)
		{
			queueFamilyProperties = make_ref(GPU.getQueueFamilyProperties());
			vk::QueueFlags flags;
			
			for (auto& qfp : *queueFamilyProperties)
			{
				if (qfp.queueFlags & vk::QueueFlagBits::eGraphics)
					flags |= vk::QueueFlagBits::eGraphics;
				if (qfp.queueFlags & vk::QueueFlagBits::eCompute)
					flags |= vk::QueueFlagBits::eCompute;
				if (qfp.queueFlags & vk::QueueFlagBits::eTransfer)
					flags |= vk::QueueFlagBits::eTransfer;
			}
			
			if (flags & vk::QueueFlagBits::eGraphics &&
			    flags & vk::QueueFlagBits::eCompute &&
			    flags & vk::QueueFlagBits::eTransfer)
			{
				gpu = make_ref(GPU);
				
				GetGraphicsFamilyId();
				GetComputeFamilyId();
				GetTransferFamilyId();
				gpuProperties = make_ref(gpu->getProperties());
				gpuFeatures = make_ref(gpu->getFeatures());
				
				return;
			}
		}
		gpu = nullptr;
	}
	
	void VulkanContext::GetGraphicsFamilyId()
	{
#ifdef UNIFIED_GRAPHICS_AND_TRANSFER_QUEUE
		const vk::QueueFlags flags = vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eTransfer;
#else
		const vk::QueueFlags flags = vk::QueueFlagBits::eGraphics;
#endif
		auto& properties = *queueFamilyProperties;
		for (uint32_t i = 0; i < properties.size(); i++)
		{
			//find graphics queue family index
			if (properties[i].queueFlags & flags && gpu->getSurfaceSupportKHR(i, *surface.surface))
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
		vk::QueueFlags flags = vk::QueueFlagBits::eTransfer;
		auto& properties = *queueFamilyProperties;
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
		const vk::QueueFlags flags = vk::QueueFlagBits::eCompute;
		auto& properties = *queueFamilyProperties;
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
		auto extensionProperties = gpu->enumerateDeviceExtensionProperties();
		
		std::vector<const char*> deviceExtensions {};
		for (auto& i : extensionProperties)
		{
			if (std::string(i.extensionName.data()) == VK_KHR_SWAPCHAIN_EXTENSION_NAME)
				deviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
		}
		float priorities[] {1.0f}; // range : [0.0, 1.0]
		
		std::vector<vk::DeviceQueueCreateInfo> queueCreateInfos {};
		
		// graphics queue
		queueCreateInfos.emplace_back();
		queueCreateInfos.back().queueFamilyIndex = graphicsFamilyId;
		queueCreateInfos.back().queueCount = 1;
		queueCreateInfos.back().pQueuePriorities = priorities;
		
		// compute queue
		if (computeFamilyId != graphicsFamilyId)
		{
			queueCreateInfos.emplace_back();
			queueCreateInfos.back().queueFamilyIndex = computeFamilyId;
			queueCreateInfos.back().queueCount = 1;
			queueCreateInfos.back().pQueuePriorities = priorities;
		}
		
		// transer queue
		if (transferFamilyId != graphicsFamilyId && transferFamilyId != computeFamilyId)
		{
			queueCreateInfos.emplace_back();
			queueCreateInfos.back().queueFamilyIndex = transferFamilyId;
			queueCreateInfos.back().queueCount = 1;
			queueCreateInfos.back().pQueuePriorities = priorities;
		}
		
		vk::DeviceCreateInfo deviceCreateInfo;
		deviceCreateInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
		deviceCreateInfo.pQueueCreateInfos = queueCreateInfos.data();
		deviceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
		deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions.data();
		deviceCreateInfo.pEnabledFeatures = &*gpuFeatures;
		
		device = make_ref(gpu->createDevice(deviceCreateInfo));
	}
	
	void VulkanContext::CreateAllocator()
	{
		VmaAllocatorCreateInfo allocator_info = {};
		allocator_info.physicalDevice = VkPhysicalDevice(*gpu);
		allocator_info.device = VkDevice(*device);
		allocator_info.instance = VkInstance(*instance);
		allocator_info.vulkanApiVersion = vk::enumerateInstanceVersion();
		
		vmaCreateAllocator(&allocator_info, &allocator);
	}
	
	void VulkanContext::GetGraphicsQueue()
	{
		graphicsQueue = make_ref(device->getQueue(graphicsFamilyId, 0));
	}
	
	void VulkanContext::GetTransferQueue()
	{
		transferQueue = make_ref(device->getQueue(transferFamilyId, 0));
	}
	
	void VulkanContext::GetComputeQueue()
	{
		computeQueue = make_ref(device->getQueue(computeFamilyId, 0));
	}
	
	void VulkanContext::GetQueues()
	{
		GetGraphicsQueue();
		GetTransferQueue();
		GetComputeQueue();
	}
	
	void VulkanContext::CreateSwapchain(Context* ctx, uint32_t requestImageCount)
	{
		swapchain.Create(ctx, requestImageCount);
	}
	
	void VulkanContext::CreateCommandPools()
	{
		vk::CommandPoolCreateInfo cpci;
		cpci.queueFamilyIndex = graphicsFamilyId;
		cpci.flags = vk::CommandPoolCreateFlagBits::eTransient | vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
		
		commandPool = make_ref(device->createCommandPool(cpci));
		commandPool2 = make_ref(device->createCommandPool(cpci));
	}
	
	void VulkanContext::CreateDescriptorPool(uint32_t maxDescriptorSets)
	{
		std::vector<vk::DescriptorPoolSize> descPoolsize(4);
		descPoolsize[0].type = vk::DescriptorType::eUniformBuffer;
		descPoolsize[0].descriptorCount = maxDescriptorSets;
		descPoolsize[1].type = vk::DescriptorType::eStorageBuffer;
		descPoolsize[1].descriptorCount = maxDescriptorSets;
		descPoolsize[2].type = vk::DescriptorType::eInputAttachment;
		descPoolsize[2].descriptorCount = maxDescriptorSets;
		descPoolsize[3].type = vk::DescriptorType::eCombinedImageSampler;
		descPoolsize[3].descriptorCount = maxDescriptorSets;
		
		vk::DescriptorPoolCreateInfo createInfo;
		createInfo.poolSizeCount = static_cast<uint32_t>(descPoolsize.size());
		createInfo.pPoolSizes = descPoolsize.data();
		createInfo.maxSets = maxDescriptorSets;
		
		descriptorPool = make_ref(device->createDescriptorPool(createInfo));
	}
	
	void VulkanContext::CreateCmdBuffers(uint32_t bufferCount)
	{
		vk::CommandBufferAllocateInfo cbai;
		cbai.commandPool = *commandPool;
		cbai.level = vk::CommandBufferLevel::ePrimary;
		cbai.commandBufferCount = bufferCount;
		dynamicCmdBuffers = make_ref(device->allocateCommandBuffers(cbai));
		
		cbai.commandBufferCount = bufferCount * 3;
		shadowCmdBuffers = make_ref(device->allocateCommandBuffers(cbai));
	}
	
	void VulkanContext::CreateFences(uint32_t fenceCount)
	{
		std::vector<vk::Fence> _fences(fenceCount);
		const vk::FenceCreateInfo fi {vk::FenceCreateFlagBits::eSignaled};
		
		for (uint32_t i = 0; i < fenceCount; i++)
		{
			_fences[i] = device->createFence(fi);
		}
		
		fences = make_ref(_fences);
	}
	
	void VulkanContext::CreateSemaphores(uint32_t semaphoresCount)
	{
		std::vector<vk::Semaphore> _semaphores(semaphoresCount);
		const vk::SemaphoreCreateInfo si;
		
		for (uint32_t i = 0; i < semaphoresCount; i++)
		{
			_semaphores[i] = device->createSemaphore(si);
		}
		
		semaphores = make_ref(_semaphores);
	}
	
	void VulkanContext::CreateDepth()
	{
		depth.format = make_ref(vk::Format::eUndefined);
		std::vector<vk::Format> candidates = {
				vk::Format::eD32SfloatS8Uint, vk::Format::eD32Sfloat, vk::Format::eD24UnormS8Uint
		};
		for (auto& format : candidates)
		{
			vk::FormatProperties props = gpu->getFormatProperties(format);
			if ((props.optimalTilingFeatures & vk::FormatFeatureFlagBits::eDepthStencilAttachment) ==
			    vk::FormatFeatureFlagBits::eDepthStencilAttachment)
			{
				depth.format = make_ref(format);
				break;
			}
		}
		if (*depth.format == vk::Format::eUndefined)
			throw std::runtime_error("Depth format is undefined");
		
		depth.createImage(
				static_cast<uint32_t>(WIDTH_f * GUI::renderTargetsScale),
				static_cast<uint32_t>(HEIGHT_f * GUI::renderTargetsScale), vk::ImageTiling::eOptimal,
				vk::ImageUsageFlagBits::eDepthStencilAttachment, vk::MemoryPropertyFlagBits::eDeviceLocal
		);
		depth.createImageView(vk::ImageAspectFlagBits::eDepth);
		
		depth.addressMode = make_ref(vk::SamplerAddressMode::eClampToEdge);
		depth.maxAnisotropy = 1.f;
		depth.borderColor = make_ref(vk::BorderColor::eFloatOpaqueWhite);
		depth.samplerCompareEnable = VK_TRUE;
		depth.createSampler();
		
		depth.transitionImageLayout(vk::ImageLayout::eUndefined, vk::ImageLayout::eDepthStencilAttachmentOptimal);
	}
	
	void VulkanContext::Init(Context* ctx)
	{
		CreateInstance(ctx->GetSystem<Renderer>()->GetWindow());
#ifdef _DEBUG
		CreateDebugMessenger();
#endif
		CreateSurface(ctx);
		GetGpu();
		GetSurfaceProperties(ctx);
		CreateDevice();
		CreateAllocator();
		GetQueues();
		CreateCommandPools();
		CreateSwapchain(ctx, SWAPCHAIN_IMAGES);
		CreateDescriptorPool(15000); // max number of all descriptor sets to allocate
		CreateCmdBuffers(SWAPCHAIN_IMAGES);
		CreateSemaphores(SWAPCHAIN_IMAGES * 3);
		CreateFences(SWAPCHAIN_IMAGES);
		CreateDepth();
	}
	
	void VulkanContext::Destroy()
	{
		device->waitIdle();
		
		for (auto& fence : *fences)
		{
			if (fence)
			{
				device->destroyFence(fence);
				fence = nullptr;
			}
		}
		for (auto& semaphore : *semaphores)
		{
			if (semaphore)
			{
				device->destroySemaphore(semaphore);
				semaphore = nullptr;
			}
		}
		
		depth.destroy();
		
		if (*descriptorPool)
		{
			device->destroyDescriptorPool(*descriptorPool);
		}
		if (*commandPool)
		{
			device->destroyCommandPool(*commandPool);
		}
		if (*commandPool2)
		{
			device->destroyCommandPool(*commandPool2);
		}
		
		swapchain.Destroy();
		
		if (*device)
		{
			device->destroy();
		}
		
		if (*surface.surface)
		{
			instance->destroySurfaceKHR(*surface.surface);
		}

#ifdef _DEBUG
		DestroyDebugMessenger();
#endif
		
		if (*instance)
		{
			instance->destroy();
		}
	}
	
	void VulkanContext::submit(
			const vk::ArrayProxy<const vk::CommandBuffer> commandBuffers,
			const vk::ArrayProxy<const vk::PipelineStageFlags> waitStages,
			const vk::ArrayProxy<const vk::Semaphore> waitSemaphores,
			const vk::ArrayProxy<const vk::Semaphore> signalSemaphores,
			const vk::Fence signalFence
	) const
	{
		vk::SubmitInfo si;
		si.waitSemaphoreCount = waitSemaphores.size();
		si.pWaitSemaphores = waitSemaphores.data();
		si.pWaitDstStageMask = waitStages.data();
		si.commandBufferCount = commandBuffers.size();
		si.pCommandBuffers = commandBuffers.data();
		si.signalSemaphoreCount = signalSemaphores.size();
		si.pSignalSemaphores = signalSemaphores.data();
		graphicsQueue->submit(si, signalFence);
	}
	
	void VulkanContext::waitFences(const vk::ArrayProxy<const vk::Fence> fences) const
	{
		if (device->waitForFences(fences, VK_TRUE, UINT64_MAX) != vk::Result::eSuccess)
			throw std::runtime_error("wait fences error!");
		device->resetFences(fences);
	}
	
	void VulkanContext::submitAndWaitFence(
			const vk::ArrayProxy<const vk::CommandBuffer> commandBuffers,
			const vk::ArrayProxy<const vk::PipelineStageFlags> waitStages,
			const vk::ArrayProxy<const vk::Semaphore> waitSemaphores,
			const vk::ArrayProxy<const vk::Semaphore> signalSemaphores
	) const
	{
		const vk::FenceCreateInfo fi;
		const vk::Fence fence = device->createFence(fi);
		
		submit(commandBuffers, waitStages, waitSemaphores, signalSemaphores, fence);
		
		if (device->waitForFences(fence, VK_TRUE, UINT64_MAX) != vk::Result::eSuccess)
			throw std::runtime_error("wait fences error!");
		device->destroyFence(fence);
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
	
	void VulkanContext::Remove() noexcept
	{
		if (Get())
			delete Get();
	}
}
