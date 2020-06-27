#include "vulkanPCH.h"
#include "Context.h"
#include "../Model/Mesh.h"
#include "../Core/Queue.h"
#include "../Core/Math.h"
#include "../VulkanContext/VulkanContext.h"
#include <filesystem>
#include <iostream>

namespace vm
{
	constexpr uint32_t SWAPCHAIN_IMAGES = 3;

	void Context::initVulkanContext() const
	{
		auto& vulkan = *VulkanContext::get();
		vulkan.instance = createInstance();
#ifdef _DEBUG
		vulkan.dispatchLoaderDynamic->init(vulkan.instance.Value(), vk::Device());
		vulkan.debugMessenger = createDebugMessenger();
#endif
		vulkan.surface = createSurface();
		vulkan.gpu = findGpu();
		vulkan.graphicsFamilyId = getGraphicsFamilyId();
		vulkan.computeFamilyId = getComputeFamilyId();
		vulkan.transferFamilyId = getTransferFamilyId();
		vulkan.gpuProperties = getGPUProperties();
		vulkan.gpuFeatures = getGPUFeatures();
		vulkan.surface.capabilities = getSurfaceCapabilities();
		vulkan.surface.formatKHR = getSurfaceFormat();
		vulkan.surface.presentModeKHR = getPresentationMode();
		vulkan.device = createDevice();
		vulkan.graphicsQueue = getGraphicsQueue();
		vulkan.computeQueue = getComputeQueue();
		vulkan.transferQueue = getTransferQueue();
		vulkan.commandPool = createCommandPool();
		vulkan.commandPool2 = createCommandPool();
		vulkan.swapchain = createSwapchain(SWAPCHAIN_IMAGES);
		vulkan.descriptorPool = createDescriptorPool(15000); // max number of all descriptor sets to allocate
		vulkan.dynamicCmdBuffers = createCmdBuffers(SWAPCHAIN_IMAGES);
		vulkan.shadowCmdBuffers = createCmdBuffers(SWAPCHAIN_IMAGES * 3);
		vulkan.semaphores = createSemaphores(SWAPCHAIN_IMAGES * 3);
		vulkan.fences = createFences(SWAPCHAIN_IMAGES);
		vulkan.depth = createDepthResources();
	}

	void Context::initRendering()
	{
		auto& vulkan = *VulkanContext::get();
		addRenderTarget("viewport", vulkan.surface.formatKHR->format, vk::ImageUsageFlagBits::eTransferSrc);
		addRenderTarget("depth", vk::Format::eR32Sfloat, vk::ImageUsageFlags());
		addRenderTarget("normal", vk::Format::eR32G32B32A32Sfloat, vk::ImageUsageFlags());
		addRenderTarget("albedo", vulkan.surface.formatKHR->format, vk::ImageUsageFlags());
		addRenderTarget("srm", vulkan.surface.formatKHR->format, vk::ImageUsageFlags()); // Specular Roughness Metallic
		addRenderTarget("ssao", vk::Format::eR16Unorm, vk::ImageUsageFlags());
		addRenderTarget("ssaoBlur", vk::Format::eR8Unorm, vk::ImageUsageFlags());
		addRenderTarget("ssr", vulkan.surface.formatKHR->format, vk::ImageUsageFlags());
		addRenderTarget("velocity", vk::Format::eR16G16Sfloat, vk::ImageUsageFlags());
		addRenderTarget("brightFilter", vulkan.surface.formatKHR->format, vk::ImageUsageFlags());
		addRenderTarget("gaussianBlurHorizontal", vulkan.surface.formatKHR->format, vk::ImageUsageFlags());
		addRenderTarget("gaussianBlurVertical", vulkan.surface.formatKHR->format, vk::ImageUsageFlags());
		addRenderTarget("emissive", vulkan.surface.formatKHR->format, vk::ImageUsageFlags());
		addRenderTarget("taa", vulkan.surface.formatKHR->format, vk::ImageUsageFlagBits::eTransferSrc);

		taa.Init();
		bloom.Init();
		fxaa.Init();
		motionBlur.Init();
		dof.Init();

		// render passes
		shadows.createRenderPass();
		ssao.createRenderPasses(renderTargets);
		ssr.createRenderPass(renderTargets);
		deferred.createRenderPasses(renderTargets);
		fxaa.createRenderPass(renderTargets);
		taa.createRenderPasses(renderTargets);
		bloom.createRenderPasses(renderTargets);
		dof.createRenderPass(renderTargets);
		motionBlur.createRenderPass(renderTargets);
		gui.createRenderPass();

		// frame buffers
		shadows.createFrameBuffers();
		ssao.createFrameBuffers(renderTargets);
		ssr.createFrameBuffers(renderTargets);
		deferred.createFrameBuffers(renderTargets);
		fxaa.createFrameBuffers(renderTargets);
		taa.createFrameBuffers(renderTargets);
		bloom.createFrameBuffers(renderTargets);
		dof.createFrameBuffers(renderTargets);
		motionBlur.createFrameBuffers(renderTargets);
		gui.createFrameBuffers();

		// pipelines
		shadows.createPipeline();
		ssao.createPipelines(renderTargets);
		ssr.createPipeline(renderTargets);
		deferred.createPipelines(renderTargets);
		fxaa.createPipeline(renderTargets);
		taa.createPipelines(renderTargets);
		bloom.createPipelines(renderTargets);
		dof.createPipeline(renderTargets);
		motionBlur.createPipeline(renderTargets);
		gui.createPipeline();

		ComputePool::get()->Init(5);

		metrics.resize(20);
	}

	void Context::resizeViewport(uint32_t width, uint32_t height)
	{
		auto& vulkan = *VulkanContext::get();
		vulkan.graphicsQueue->waitIdle();

		//- Free resources ----------------------
		// render targets
		for (auto& RT : renderTargets)
			RT.second.destroy();
		renderTargets.clear();

		// GUI
		gui.renderPass.Destroy();
		for (auto& framebuffer : *gui.framebuffers)
			framebuffer.Destroy();
		gui.pipeline.destroy();

		// deferred
		deferred.renderPass.Destroy();
		deferred.compositionRenderPass.Destroy();
		for (auto& framebuffer : deferred.framebuffers)
			framebuffer.Destroy();
		for (auto& framebuffer : deferred.compositionFramebuffers)
			framebuffer.Destroy();
		deferred.pipeline.destroy();
		deferred.pipelineComposition.destroy();

		// SSR
		for (auto& framebuffer : ssr.framebuffers)
			framebuffer.Destroy();
		ssr.renderPass.Destroy();
		ssr.pipeline.destroy();

		// FXAA
		for (auto& framebuffer : fxaa.framebuffers)
			framebuffer.Destroy();
		fxaa.renderPass.Destroy();
		fxaa.pipeline.destroy();
		fxaa.frameImage.destroy();

		// TAA
		taa.previous.destroy();
		taa.frameImage.destroy();
		for (auto& framebuffer : taa.framebuffers)
			framebuffer.Destroy();
		for (auto& framebuffer : taa.framebuffersSharpen)
			framebuffer.Destroy();
		taa.renderPass.Destroy();
		taa.renderPassSharpen.Destroy();
		taa.pipeline.destroy();
		taa.pipelineSharpen.destroy();

		// Bloom
		for (auto& frameBuffer : bloom.framebuffers)
			frameBuffer.Destroy();
		bloom.renderPassBrightFilter.Destroy();
		bloom.renderPassGaussianBlur.Destroy();
		bloom.renderPassCombine.Destroy();
		bloom.pipelineBrightFilter.destroy();
		bloom.pipelineGaussianBlurHorizontal.destroy();
		bloom.pipelineGaussianBlurVertical.destroy();
		bloom.pipelineCombine.destroy();
		bloom.frameImage.destroy();

		// Depth of Field
		for (auto& framebuffer : dof.framebuffers)
			framebuffer.Destroy();
		dof.renderPass.Destroy();
		dof.pipeline.destroy();
		dof.frameImage.destroy();

		// Motion blur
		for (auto& framebuffer : motionBlur.framebuffers)
			framebuffer.Destroy();
		motionBlur.renderPass.Destroy();
		motionBlur.pipeline.destroy();
		motionBlur.frameImage.destroy();

		// SSAO
		ssao.renderPass.Destroy();
		ssao.blurRenderPass.Destroy();
		for (auto& framebuffer : ssao.framebuffers)
			framebuffer.Destroy();
		for (auto& framebuffer : ssao.blurFramebuffers)
			framebuffer.Destroy();
		ssao.pipeline.destroy();
		ssao.pipelineBlur.destroy();

		vulkan.depth.destroy();
		vulkan.swapchain.destroy();
		//- Free resources end ------------------

		//- Recreate resources ------------------
		WIDTH = width;
		HEIGHT = height;
		vulkan.swapchain = createSwapchain(SWAPCHAIN_IMAGES);
		vulkan.depth = createDepthResources();

		addRenderTarget("viewport", vulkan.surface.formatKHR->format, vk::ImageUsageFlagBits::eTransferSrc);
		addRenderTarget("depth", vk::Format::eR32Sfloat, vk::ImageUsageFlags());
		addRenderTarget("normal", vk::Format::eR32G32B32A32Sfloat, vk::ImageUsageFlags());
		addRenderTarget("albedo", vulkan.surface.formatKHR->format, vk::ImageUsageFlags());
		addRenderTarget("srm", vulkan.surface.formatKHR->format, vk::ImageUsageFlags()); // Specular Roughness Metallic
		addRenderTarget("ssao", vk::Format::eR16Unorm, vk::ImageUsageFlags());
		addRenderTarget("ssaoBlur", vk::Format::eR8Unorm, vk::ImageUsageFlags());
		addRenderTarget("ssr", vulkan.surface.formatKHR->format, vk::ImageUsageFlags());
		addRenderTarget("velocity", vk::Format::eR16G16Sfloat, vk::ImageUsageFlags());
		addRenderTarget("brightFilter", vulkan.surface.formatKHR->format, vk::ImageUsageFlags());
		addRenderTarget("gaussianBlurHorizontal", vulkan.surface.formatKHR->format, vk::ImageUsageFlags());
		addRenderTarget("gaussianBlurVertical", vulkan.surface.formatKHR->format, vk::ImageUsageFlags());
		addRenderTarget("emissive", vulkan.surface.formatKHR->format, vk::ImageUsageFlags());
		addRenderTarget("taa", vulkan.surface.formatKHR->format, vk::ImageUsageFlagBits::eTransferSrc);

		deferred.createRenderPasses(renderTargets);
		deferred.createFrameBuffers(renderTargets);
		deferred.createPipelines(renderTargets);
		deferred.updateDescriptorSets(renderTargets, lightUniforms);

		ssr.createRenderPass(renderTargets);
		ssr.createFrameBuffers(renderTargets);
		ssr.createPipeline(renderTargets);
		ssr.updateDescriptorSets(renderTargets);

		fxaa.Init();
		fxaa.createRenderPass(renderTargets);
		fxaa.createFrameBuffers(renderTargets);
		fxaa.createPipeline(renderTargets);
		fxaa.updateDescriptorSets(renderTargets);

		taa.Init();
		taa.createRenderPasses(renderTargets);
		taa.createFrameBuffers(renderTargets);
		taa.createPipelines(renderTargets);
		taa.updateDescriptorSets(renderTargets);

		bloom.Init();
		bloom.createRenderPasses(renderTargets);
		bloom.createFrameBuffers(renderTargets);
		bloom.createPipelines(renderTargets);
		bloom.updateDescriptorSets(renderTargets);

		dof.Init();
		dof.createRenderPass(renderTargets);
		dof.createFrameBuffers(renderTargets);
		dof.createPipeline(renderTargets);
		dof.updateDescriptorSets(renderTargets);

		motionBlur.Init();
		motionBlur.createRenderPass(renderTargets);
		motionBlur.createFrameBuffers(renderTargets);
		motionBlur.createPipeline(renderTargets);
		motionBlur.updateDescriptorSets(renderTargets);

		ssao.createRenderPasses(renderTargets);
		ssao.createFrameBuffers(renderTargets);
		ssao.createPipelines(renderTargets);
		ssao.updateDescriptorSets(renderTargets);

		gui.createRenderPass();
		gui.createFrameBuffers();
		gui.createPipeline();
		gui.updateDescriptorSets();

		//compute.pipeline = createComputePipeline();
		//compute.updateDescriptorSets();
		//- Recreate resources end --------------
	}

	void Context::recreatePipelines()
	{
		VulkanContext::get()->graphicsQueue->waitIdle();

		shadows.pipeline.destroy();
		ssao.pipeline.destroy();
		ssao.pipelineBlur.destroy();
		ssr.pipeline.destroy();
		deferred.pipeline.destroy();
		deferred.pipelineComposition.destroy();
		fxaa.pipeline.destroy();
		taa.pipeline.destroy();
		taa.pipelineSharpen.destroy();
		//bloom.pipelineBrightFilter.destroy();
		//bloom.pipelineCombine.destroy();
		//bloom.pipelineGaussianBlurHorizontal.destroy();
		//bloom.pipelineGaussianBlurVertical.destroy();
		dof.pipeline.destroy();
		motionBlur.pipeline.destroy();
		gui.pipeline.destroy();

		shadows.createPipeline();
		ssao.createPipelines(renderTargets);
		ssr.createPipeline(renderTargets);
		deferred.createPipelines(renderTargets);
		fxaa.createPipeline(renderTargets);
		taa.createPipelines(renderTargets);
		bloom.createPipelines(renderTargets);
		dof.createPipeline(renderTargets);
		motionBlur.createPipeline(renderTargets);
		gui.createPipeline();
	}

	// Callbacks for scripts -------------------
	static void LoadModel(MonoString* folderPath, MonoString* modelName, uint32_t instances)
	{
		const std::string curPath = std::filesystem::current_path().string() + "\\";
		const std::string path(mono_string_to_utf8(folderPath));
		const std::string name(mono_string_to_utf8(modelName));
		for (; instances > 0; instances--)
			Queue::loadModel.emplace_back(curPath + path, name);
	}

	static bool KeyDown(uint32_t key)
	{
		return ImGui::GetIO().KeysDown[key];
	}

	static bool MouseButtonDown(uint32_t button)
	{
		return ImGui::GetIO().MouseDown[button];
	}

	static ImVec2 GetMousePos()
	{
		return ImGui::GetIO().MousePos;
	}

	static void SetMousePos(float x, float y)
	{
		SDL_WarpMouseInWindow(GUI::g_Window, static_cast<int>(x), static_cast<int>(y));
	}

	static float GetMouseWheel()
	{
		return ImGui::GetIO().MouseWheel;
	}

	static void SetTimeScale(float timeScale)
	{
		GUI::timeScale = timeScale;
	}
	// ----------------------------------------

	void Context::loadResources()
	{
		// SKYBOXES LOAD
		std::array<std::string, 6> skyTextures = {
			"objects/sky/right.png",
			"objects/sky/left.png",
			"objects/sky/top.png",
			"objects/sky/bottom.png",
			"objects/sky/back.png",
			"objects/sky/front.png" };
		skyBoxDay.loadSkyBox(skyTextures, 1024);
		skyTextures = {
			"objects/lmcity/lmcity_rt.png",
			"objects/lmcity/lmcity_lf.png",
			"objects/lmcity/lmcity_up.png",
			"objects/lmcity/lmcity_dn.png",
			"objects/lmcity/lmcity_bk.png",
			"objects/lmcity/lmcity_ft.png" };
		skyBoxNight.loadSkyBox(skyTextures, 512);

		// GUI LOAD
		gui.loadGUI();

		// SCRIPTS
#ifdef USE_SCRIPTS
		Script::Init();
		Script::addCallback("Global::LoadModel", reinterpret_cast<const void*>(LoadModel));
		Script::addCallback("Global::KeyDown", reinterpret_cast<const void*>(KeyDown));
		Script::addCallback("Global::SetTimeScale", reinterpret_cast<const void*>(SetTimeScale));
		Script::addCallback("Global::MouseButtonDown", reinterpret_cast<const void*>(MouseButtonDown));
		Script::addCallback("Global::GetMousePos", reinterpret_cast<const void*>(GetMousePos));
		Script::addCallback("Global::SetMousePos", reinterpret_cast<const void*>(SetMousePos));
		Script::addCallback("Global::GetMouseWheel", reinterpret_cast<const void*>(GetMouseWheel));
		scripts.push_back(new Script("Load"));
#endif
	}

	void Context::createUniforms()
	{
		// DESCRIPTOR SETS FOR GUI
		gui.createDescriptorSet(GUI::getDescriptorSetLayout(VulkanContext::get()->device.Value()));
		// DESCRIPTOR SETS FOR SKYBOX
		skyBoxDay.createDescriptorSet();
		skyBoxNight.createDescriptorSet();
		// DESCRIPTOR SETS FOR SHADOWS
		shadows.createUniformBuffers();
		shadows.createDescriptorSets();
		// DESCRIPTOR SETS FOR LIGHTS
		lightUniforms.createLightUniforms();
		// DESCRIPTOR SETS FOR SSAO
		ssao.createUniforms(renderTargets);
		// DESCRIPTOR SETS FOR COMPOSITION PIPELINE
		deferred.createDeferredUniforms(renderTargets, lightUniforms);
		// DESCRIPTOR SET FOR REFLECTION PIPELINE
		ssr.createSSRUniforms(renderTargets);
		// DESCRIPTOR SET FOR FXAA PIPELINE
		fxaa.createUniforms(renderTargets);
		// DESCRIPTOR SET FOR TAA PIPELINE
		taa.createUniforms(renderTargets);
		// DESCRIPTOR SET FOR BLOOM PIPELINES
		bloom.createUniforms(renderTargets);
		// DESCRIPTOR SET FOR DEPTH OF FIELD PIPELINE
		dof.createUniforms(renderTargets);
		// DESCRIPTOR SET FOR MOTIONBLUR PIPELINE
		motionBlur.createMotionBlurUniforms(renderTargets);
		// DESCRIPTOR SET FOR COMPUTE PIPELINE
		//compute.createComputeUniforms(sizeof(SBOIn));
	}

	vk::Instance Context::createInstance() const
	{
		std::vector<const char*> instanceExtensions;
		std::vector<const char*> instanceLayers;
		vk::ValidationFeaturesEXT validationFeatures;
		std::vector<vk::ValidationFeatureEnableEXT> enabledFeatures;

		// === Extentions ==============================
		unsigned extCount;
		if (!SDL_Vulkan_GetInstanceExtensions(VulkanContext::get()->window, &extCount, nullptr))
			throw std::runtime_error(SDL_GetError());
		instanceExtensions.resize(extCount);
		if (!SDL_Vulkan_GetInstanceExtensions(VulkanContext::get()->window, &extCount, instanceExtensions.data()))
			throw std::runtime_error(SDL_GetError());
		// =============================================

#ifdef _DEBUG
	// === Debug Extensions ========================
		auto extensions = vk::enumerateInstanceExtensionProperties();
		for (auto& extension : extensions) {
			if (std::string(extension.extensionName) == "VK_EXT_debug_utils")
				instanceExtensions.push_back("VK_EXT_debug_utils");
		}
		// =============================================

		// === Debug Layers ============================
		// To use these debug layers, here is assumed VulkanSDK is installed and Bin is in enviromental path
		auto layers = vk::enumerateInstanceLayerProperties();
		for (auto layer : layers) {
			if (std::string(layer.layerName) == "VK_LAYER_KHRONOS_validation")
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

		return vk::createInstance(instInfo);
	}

	VKAPI_ATTR uint32_t VKAPI_CALL Context::messageCallback(
		VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
		uint32_t messageType,
		const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
		void* pUserData)
	{
		if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
			std::cerr
			<< to_string(vk::DebugUtilsMessageTypeFlagBitsEXT(messageType)) << " "
			<< to_string(vk::DebugUtilsMessageSeverityFlagBitsEXT(messageSeverity)) << " from \""
			<< pCallbackData->pMessageIdName << "\": "
			<< pCallbackData->pMessage << std::endl;

		return VK_FALSE;
	}

	vk::DebugUtilsMessengerEXT Context::createDebugMessenger() const
	{
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
		dumci.pfnUserCallback = Context::messageCallback;

		return VulkanContext::get()->instance->createDebugUtilsMessengerEXT(dumci, nullptr, VulkanContext::get()->dispatchLoaderDynamic.Value());
	}

	void Context::destroyDebugMessenger() const
	{
		vk::DispatchLoaderDynamic dld;
		dld.init(VulkanContext::get()->instance.Value(), vk::Device());

		VulkanContext::get()->instance->destroyDebugUtilsMessengerEXT(VulkanContext::get()->debugMessenger.Value(), nullptr, dld);
	}

	Surface Context::createSurface() const
	{
		VkSurfaceKHR _vkSurface;
		if (!SDL_Vulkan_CreateSurface(VulkanContext::get()->window, VkInstance(VulkanContext::get()->instance.Value()), &_vkSurface))
			throw std::runtime_error(SDL_GetError());

		Surface _surface;
		int width, height;
		SDL_GL_GetDrawableSize(VulkanContext::get()->window, &width, &height);
		_surface.actualExtent = vk::Extent2D{ static_cast<uint32_t>(width), static_cast<uint32_t>(height) };
		_surface.surface = vk::SurfaceKHR(_vkSurface);

		return _surface;
	}

	int Context::getGraphicsFamilyId() const
	{
#ifdef UNIFIED_GRAPHICS_AND_TRANSFER_QUEUE
		const vk::QueueFlags flags = vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eTransfer;
#else
		const vk::QueueFlags flags = vk::QueueFlagBits::eGraphics;
#endif
		auto& properties = VulkanContext::get()->queueFamilyProperties;
		for (uint32_t i = 0; i < properties->size(); i++) {
			//find graphics queue family index
			if (properties[i].queueFlags & flags && VulkanContext::get()->gpu->getSurfaceSupportKHR(i, VulkanContext::get()->surface.surface.Value()))
				return i;
		}
		return -1;
	}

	int vm::Context::getTransferFamilyId() const
	{
#ifdef UNIFIED_GRAPHICS_AND_TRANSFER_QUEUE
		return getGraphicsFamilyId();
#else
		vk::QueueFlags flags = vk::QueueFlagBits::eTransfer;
		auto& properties = vulkan.queueFamilyProperties;
		// prefer different families for different queue types, thus the reverse check
		for (int i = static_cast<int>(properties.size()) - 1; i >= 0; --i) {
			//find transfer queue family index
			if (properties[i].queueFlags & flags)
				return i;
		}
		return -1;
#endif
	}

	int Context::getComputeFamilyId() const
	{
		const vk::QueueFlags flags = vk::QueueFlagBits::eCompute;
		auto& properties = VulkanContext::get()->queueFamilyProperties;
		// prefer different families for different queue types, thus the reverse check
		for (int i = static_cast<int>(properties->size()) - 1; i >= 0; --i) {
			//find compute queue family index
			if (properties[i].queueFlags & flags)
				return i;
		}
		return -1;
	}

	vk::PhysicalDevice Context::findGpu() const
	{
		std::vector<vk::PhysicalDevice> gpuList = VulkanContext::get()->instance->enumeratePhysicalDevices();

		for (const auto& gpu : gpuList) {
			VulkanContext::get()->queueFamilyProperties = gpu.getQueueFamilyProperties();
			vk::QueueFlags flags;

			for (const auto& qfp : VulkanContext::get()->queueFamilyProperties.Value()) {
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
				return gpu;
		}
		return nullptr;
	}

	vk::SurfaceCapabilitiesKHR Context::getSurfaceCapabilities() const
	{
		auto caps = VulkanContext::get()->gpu->getSurfaceCapabilitiesKHR(VulkanContext::get()->surface.surface.Value());
		// Ensure eTransferSrc bit for blit operations
		if (!(caps.supportedUsageFlags & vk::ImageUsageFlagBits::eTransferSrc))
			throw std::runtime_error("Surface doesnt support vk::ImageUsageFlagBits::eTransferSrc");
		return caps;
	}

	vk::SurfaceFormatKHR Context::getSurfaceFormat() const
	{
		std::vector<vk::SurfaceFormatKHR> formats = VulkanContext::get()->gpu->getSurfaceFormatsKHR(VulkanContext::get()->surface.surface.Value());
		auto format = formats[0];
		for (const auto& f : formats) {
			if (f.format == vk::Format::eB8G8R8A8Unorm && f.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear)
				format = f;
		}

		// Check for blit operation
		auto const fProps = VulkanContext::get()->gpu->getFormatProperties(format.format);
		if (!(fProps.optimalTilingFeatures & vk::FormatFeatureFlagBits::eBlitSrc))
			throw std::runtime_error("No blit source operation supported");
		if (!(fProps.optimalTilingFeatures & vk::FormatFeatureFlagBits::eBlitDst))
			throw std::runtime_error("No blit destination operation supported");

		return format;
	}

	vk::PresentModeKHR Context::getPresentationMode() const
	{
		std::vector<vk::PresentModeKHR> presentModes = VulkanContext::get()->gpu->getSurfacePresentModesKHR(VulkanContext::get()->surface.surface.Value());

		for (const auto& i : presentModes)
			if (i == vk::PresentModeKHR::eMailbox)
				return i;

		for (const auto& i : presentModes)
			if (i == vk::PresentModeKHR::eImmediate)
				return i;

		return vk::PresentModeKHR::eFifo;
	}

	vk::PhysicalDeviceProperties Context::getGPUProperties() const
	{
		return VulkanContext::get()->gpu->getProperties();
	}

	vk::PhysicalDeviceFeatures Context::getGPUFeatures() const
	{
		return VulkanContext::get()->gpu->getFeatures();
	}

	vk::Device Context::createDevice() const
	{
		auto& vulkan = *VulkanContext::get();
		auto extensionProperties = vulkan.gpu->enumerateDeviceExtensionProperties();

		std::vector<const char*> deviceExtensions{};
		for (auto& i : extensionProperties) {
			if (std::string(i.extensionName) == VK_KHR_SWAPCHAIN_EXTENSION_NAME)
				deviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
		}
		float priorities[]{ 1.0f }; // range : [0.0, 1.0]

		std::vector<vk::DeviceQueueCreateInfo> queueCreateInfos{};

		// graphics queue
		queueCreateInfos.emplace_back();
		queueCreateInfos.back().queueFamilyIndex = vulkan.graphicsFamilyId;
		queueCreateInfos.back().queueCount = 1;
		queueCreateInfos.back().pQueuePriorities = priorities;

		// compute queue
		if (vulkan.computeFamilyId != vulkan.graphicsFamilyId) {
			queueCreateInfos.emplace_back();
			queueCreateInfos.back().queueFamilyIndex = vulkan.computeFamilyId;
			queueCreateInfos.back().queueCount = 1;
			queueCreateInfos.back().pQueuePriorities = priorities;
		}

		// transer queue
		if (vulkan.transferFamilyId != vulkan.graphicsFamilyId && vulkan.transferFamilyId != vulkan.computeFamilyId) {
			queueCreateInfos.emplace_back();
			queueCreateInfos.back().queueFamilyIndex = vulkan.transferFamilyId;
			queueCreateInfos.back().queueCount = 1;
			queueCreateInfos.back().pQueuePriorities = priorities;
		}

		vk::DeviceCreateInfo deviceCreateInfo;
		deviceCreateInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
		deviceCreateInfo.pQueueCreateInfos = queueCreateInfos.data();
		deviceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
		deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions.data();
		deviceCreateInfo.pEnabledFeatures = &*vulkan.gpuFeatures;

		return vulkan.gpu->createDevice(deviceCreateInfo);
	}

	vk::Queue Context::getGraphicsQueue() const
	{
		return VulkanContext::get()->device->getQueue(VulkanContext::get()->graphicsFamilyId, 0);
	}

	vk::Queue vm::Context::getTransferQueue() const
	{
		return VulkanContext::get()->device->getQueue(VulkanContext::get()->transferFamilyId, 0);
	}

	vk::Queue Context::getComputeQueue() const
	{
		return VulkanContext::get()->device->getQueue(VulkanContext::get()->computeFamilyId, 0);
	}

	Swapchain Context::createSwapchain(uint32_t requestImageCount) const
	{
		auto& vulkan = *VulkanContext::get();
		const VkExtent2D extent = vulkan.surface.actualExtent.Value();
		vulkan.surface.capabilities = getSurfaceCapabilities();

		vk::SwapchainCreateInfoKHR swapchainCreateInfo;
		swapchainCreateInfo.surface = vulkan.surface.surface.Value();
		swapchainCreateInfo.minImageCount = clamp(requestImageCount, vulkan.surface.capabilities->minImageCount, vulkan.surface.capabilities->maxImageCount);
		swapchainCreateInfo.imageFormat = vulkan.surface.formatKHR->format;
		swapchainCreateInfo.imageColorSpace = vulkan.surface.formatKHR->colorSpace;
		swapchainCreateInfo.imageExtent = extent;
		swapchainCreateInfo.imageArrayLayers = 1;
		swapchainCreateInfo.imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst;
		swapchainCreateInfo.preTransform = vulkan.surface.capabilities->currentTransform;
		swapchainCreateInfo.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
		swapchainCreateInfo.presentMode = vulkan.surface.presentModeKHR.Value();
		swapchainCreateInfo.clipped = VK_TRUE;
		swapchainCreateInfo.oldSwapchain = 
			*vulkan.swapchain.swapchain ?
			*vulkan.swapchain.swapchain :
			nullptr;

		// new swapchain with old create info
		Swapchain swapchain;
		swapchain.swapchain = vulkan.device->createSwapchainKHR(swapchainCreateInfo);

		// destroy old swapchain
		if (vulkan.swapchain.swapchain.Value()) {
			vulkan.device->destroySwapchainKHR(vulkan.swapchain.swapchain.Value());
			*vulkan.swapchain.swapchain = nullptr;
		}

		// get the swapchain image handlers
		std::vector<vk::Image> images = vulkan.device->getSwapchainImagesKHR(swapchain.swapchain.Value());

		swapchain.images.resize(images.size());
		for (unsigned i = 0; i < images.size(); i++) {
			swapchain.images[i].image = images[i]; // hold the image handlers
			swapchain.images[i].transitionImageLayout(vk::ImageLayout::eUndefined, vk::ImageLayout::ePresentSrcKHR);
			swapchain.images[i].blentAttachment->blendEnable = VK_TRUE;
			swapchain.images[i].blentAttachment->srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
			swapchain.images[i].blentAttachment->dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
			swapchain.images[i].blentAttachment->colorBlendOp = vk::BlendOp::eAdd;
			swapchain.images[i].blentAttachment->srcAlphaBlendFactor = vk::BlendFactor::eOne;
			swapchain.images[i].blentAttachment->dstAlphaBlendFactor = vk::BlendFactor::eZero;
			swapchain.images[i].blentAttachment->alphaBlendOp = vk::BlendOp::eAdd;
			swapchain.images[i].blentAttachment->colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
		}

		// create image views for each swapchain image
		for (auto& image : swapchain.images) {
			vk::ImageViewCreateInfo imageViewCreateInfo;
			imageViewCreateInfo.image = image.image.Value();
			imageViewCreateInfo.viewType = vk::ImageViewType::e2D;
			imageViewCreateInfo.format = VulkanContext::get()->surface.formatKHR->format;
			imageViewCreateInfo.subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 };
			image.view = VulkanContext::get()->device->createImageView(imageViewCreateInfo);
		}

		return swapchain;
	}

	vk::CommandPool Context::createCommandPool() const
	{
		vk::CommandPoolCreateInfo cpci;
		cpci.queueFamilyIndex = VulkanContext::get()->graphicsFamilyId;
		cpci.flags = vk::CommandPoolCreateFlagBits::eTransient | vk::CommandPoolCreateFlagBits::eResetCommandBuffer;

		return VulkanContext::get()->device->createCommandPool(cpci);
	}

	void Context::addRenderTarget(const std::string& name, vk::Format format, const vk::ImageUsageFlags& additionalFlags)
	{
		if (renderTargets.find(name) != renderTargets.end()) {
			std::cout << "Render Target already exists\n";
			return;
		}
		renderTargets[name] = Image();
		renderTargets[name].format = format;
		renderTargets[name].initialLayout = vk::ImageLayout::eUndefined;
		renderTargets[name].createImage(
			static_cast<uint32_t>(WIDTH_f * GUI::renderTargetsScale),
			static_cast<uint32_t>(HEIGHT_f * GUI::renderTargetsScale),
			vk::ImageTiling::eOptimal,
			vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled | additionalFlags,
			vk::MemoryPropertyFlagBits::eDeviceLocal
		);
		renderTargets[name].transitionImageLayout(vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal);
		renderTargets[name].createImageView(vk::ImageAspectFlagBits::eColor);
		renderTargets[name].createSampler();

		//std::string str = to_string(format); str.find("A8") != std::string::npos
		renderTargets[name].blentAttachment->blendEnable = name == "albedo" ? VK_TRUE : VK_FALSE;
		renderTargets[name].blentAttachment->srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
		renderTargets[name].blentAttachment->dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
		renderTargets[name].blentAttachment->colorBlendOp = vk::BlendOp::eAdd;
		renderTargets[name].blentAttachment->srcAlphaBlendFactor = vk::BlendFactor::eOne;
		renderTargets[name].blentAttachment->dstAlphaBlendFactor = vk::BlendFactor::eZero;
		renderTargets[name].blentAttachment->alphaBlendOp = vk::BlendOp::eAdd;
		renderTargets[name].blentAttachment->colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
	}

	Image Context::createDepthResources() const
	{
		Image _image = Image();
		_image.format = vk::Format::eUndefined;
		std::vector<vk::Format> candidates = { vk::Format::eD32SfloatS8Uint, vk::Format::eD32Sfloat, vk::Format::eD24UnormS8Uint };
		for (auto& format : candidates) {
			vk::FormatProperties props = VulkanContext::get()->gpu->getFormatProperties(format);
			if ((props.optimalTilingFeatures & vk::FormatFeatureFlagBits::eDepthStencilAttachment) == vk::FormatFeatureFlagBits::eDepthStencilAttachment) {
				_image.format = format;
				break;
			}
		}
		if (_image.format.Value() == vk::Format::eUndefined)
			throw std::runtime_error("Depth format is undefined");

		_image.createImage(static_cast<uint32_t>(WIDTH_f * GUI::renderTargetsScale), static_cast<uint32_t>(HEIGHT_f * GUI::renderTargetsScale), vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eDepthStencilAttachment, vk::MemoryPropertyFlagBits::eDeviceLocal);
		_image.createImageView(vk::ImageAspectFlagBits::eDepth);

		_image.addressMode = vk::SamplerAddressMode::eClampToEdge;
		_image.maxAnisotropy = 1.f;
		_image.borderColor = vk::BorderColor::eFloatOpaqueWhite;
		_image.samplerCompareEnable = VK_TRUE;
		_image.createSampler();

		_image.transitionImageLayout(vk::ImageLayout::eUndefined, vk::ImageLayout::eDepthStencilAttachmentOptimal);

		return _image;
	}

	std::vector<vk::CommandBuffer> Context::createCmdBuffers(const uint32_t bufferCount) const
	{
		vk::CommandBufferAllocateInfo cbai;
		cbai.commandPool = VulkanContext::get()->commandPool.Value();
		cbai.level = vk::CommandBufferLevel::ePrimary;
		cbai.commandBufferCount = bufferCount;

		return VulkanContext::get()->device->allocateCommandBuffers(cbai);
	}

	vk::DescriptorPool Context::createDescriptorPool(uint32_t maxDescriptorSets) const
	{
		// TODO: Make these dynamic
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

		return VulkanContext::get()->device->createDescriptorPool(createInfo);
	}

	std::vector<vk::Fence> Context::createFences(const uint32_t fenceCount) const
	{
		std::vector<vk::Fence> _fences(fenceCount);
		const vk::FenceCreateInfo fi{ vk::FenceCreateFlagBits::eSignaled };

		for (uint32_t i = 0; i < fenceCount; i++) {
			_fences[i] = VulkanContext::get()->device->createFence(fi);
		}

		return _fences;
	}

	std::vector<vk::Semaphore> Context::createSemaphores(const uint32_t semaphoresCount) const
	{
		std::vector<vk::Semaphore> _semaphores(semaphoresCount);
		const vk::SemaphoreCreateInfo si;

		for (uint32_t i = 0; i < semaphoresCount; i++) {
			_semaphores[i] = VulkanContext::get()->device->createSemaphore(si);
		}

		return _semaphores;
	}

	void Context::destroyVkContext()
	{
		auto& vulkan = *VulkanContext::get();
		vulkan.device->waitIdle();

		for (auto& fence : *vulkan.fences) {
			if (fence) {
				vulkan.device->destroyFence(fence);
				fence = nullptr;
			}
		}
		for (auto& semaphore : *vulkan.semaphores) {
			if (semaphore) {
				vulkan.device->destroySemaphore(semaphore);
				semaphore = nullptr;
			}
		}
		for (auto& rt : renderTargets)
			rt.second.destroy();

		vulkan.depth.destroy();

		if (vulkan.descriptorPool.Value()) {
			vulkan.device->destroyDescriptorPool(vulkan.descriptorPool.Value());
		}
		if (vulkan.commandPool.Value()) {
			vulkan.device->destroyCommandPool(vulkan.commandPool.Value());
		}
		if (vulkan.commandPool2.Value()) {
			vulkan.device->destroyCommandPool(vulkan.commandPool2.Value());
		}

		vulkan.swapchain.destroy();

		if (vulkan.device.Value()) {
			vulkan.device->destroy();
		}

		if (vulkan.surface.surface.Value()) {
			vulkan.instance->destroySurfaceKHR(vulkan.surface.surface.Value());
		}

#ifdef _DEBUG
		destroyDebugMessenger();
#endif

		if (vulkan.instance.Value()) {
			vulkan.instance->destroy();
		}
	}
}
