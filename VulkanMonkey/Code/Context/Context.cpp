#include "Context.h"
#include "../Queue/Queue.h"
#include <filesystem>
#include "../Window/Window.h"

using namespace vm;

//std::vector<Model> Context::models = {};

void Context::initVulkanContext() const
{
	vulkan.instance = createInstance();
#ifdef _DEBUG
	vulkan.debugMessenger = createDebugMessenger();
#endif
	vulkan.surface = new Surface(createSurface());
	vulkan.gpu = findGpu();
	vulkan.graphicsFamilyId = getGraphicsFamilyId();
	vulkan.computeFamilyId = getComputeFamilyId();
	vulkan.transferFamilyId = getTransferFamilyId();
	vulkan.gpuProperties = getGPUProperties();
	vulkan.gpuFeatures = getGPUFeatures();
	vulkan.surface->capabilities = getSurfaceCapabilities();
	vulkan.surface->formatKHR = getSurfaceFormat();
	vulkan.surface->presentModeKHR = getPresentationMode();
	vulkan.device = createDevice();
	vulkan.graphicsQueue = getGraphicsQueue();
	vulkan.computeQueue = getComputeQueue();
	vulkan.transferQueue = getTransferQueue();
	vulkan.semaphores = createSemaphores(3);
	vulkan.fences = createFences(2);
	vulkan.commandPool = createCommandPool();
	vulkan.commandPool2 = createCommandPool();
	vulkan.swapchain = new Swapchain(createSwapchain());
	vulkan.descriptorPool = createDescriptorPool(15000); // max number of all descriptor sets to allocate
	vulkan.dynamicCmdBuffer = createCmdBuffers().at(0);
	vulkan.shadowCmdBuffers = createCmdBuffers(3);
	vulkan.depth = new Image(createDepthResources());
}

void Context::initRendering()
{
	addRenderTarget("viewport", vulkan.surface->formatKHR.format, vk::ImageUsageFlagBits::eTransferSrc);
	addRenderTarget("depth", vk::Format::eR32Sfloat);
	addRenderTarget("normal", vk::Format::eR32G32B32A32Sfloat);
	addRenderTarget("albedo", vulkan.surface->formatKHR.format);
	addRenderTarget("srm", vulkan.surface->formatKHR.format); // Specular Roughness Metallic
	addRenderTarget("ssao", vk::Format::eR16Unorm);
	addRenderTarget("ssaoBlur", vk::Format::eR8Unorm);
	addRenderTarget("ssr", vulkan.surface->formatKHR.format);
	addRenderTarget("velocity", vk::Format::eR16G16Sfloat);
	addRenderTarget("brightFilter", vulkan.surface->formatKHR.format);
	addRenderTarget("gaussianBlurHorizontal", vulkan.surface->formatKHR.format);
	addRenderTarget("gaussianBlurVertical", vulkan.surface->formatKHR.format);
	addRenderTarget("emissive", vulkan.surface->formatKHR.format);
	addRenderTarget("taa", vulkan.surface->formatKHR.format, vk::ImageUsageFlagBits::eTransferSrc);

	taa.Init();
	bloom.Init();
	fxaa.Init();
	motionBlur.Init();

	// render passes
#ifdef RENDER_SKYBOX
	skyBoxDay.createRenderPass();
	skyBoxNight.createRenderPass();
#endif
	shadows.createRenderPass();
	ssao.createRenderPasses(renderTargets);
	ssr.createRenderPass(renderTargets);
	deferred.createRenderPasses(renderTargets);
	fxaa.createRenderPass(renderTargets);
	taa.createRenderPasses(renderTargets);
	bloom.createRenderPasses(renderTargets);
	motionBlur.createRenderPass(renderTargets);
	gui.createRenderPass();

	// frame buffers
#ifdef RENDER_SKYBOX
	skyBoxDay.createFrameBuffers();
	skyBoxNight.createFrameBuffers();
#endif
	shadows.createFrameBuffers();
	ssao.createFrameBuffers(renderTargets);
	ssr.createFrameBuffers(renderTargets);
	deferred.createFrameBuffers(renderTargets);
	fxaa.createFrameBuffers(renderTargets);
	taa.createFrameBuffers(renderTargets);
	bloom.createFrameBuffers(renderTargets);
	motionBlur.createFrameBuffers(renderTargets);
	gui.createFrameBuffers();

	// pipelines
#ifdef RENDER_SKYBOX
	skyBoxDay.createPipeline();
	skyBoxNight.createPipeline();
#endif
	shadows.createPipeline(*Mesh::getDescriptorSetLayout(), *Model::getDescriptorSetLayout());
	ssao.createPipelines(renderTargets);
	ssr.createPipeline(renderTargets);
	deferred.createPipelines(renderTargets);
	fxaa.createPipeline(renderTargets);
	taa.createPipelines(renderTargets);
	bloom.createPipelines(renderTargets);
	motionBlur.createPipeline(renderTargets);
	gui.createPipeline();

	computePool.Init(5);

	metrics.resize(20);
	for (auto& metric : metrics)
		metric.initQueryPool();
}

void Context::resizeViewport(uint32_t width, uint32_t height)
{
	vulkan.device.waitIdle();

	//- Free resources ----------------------
	// render targets
	for (auto& RT : renderTargets)
		RT.second.destroy();
	renderTargets.clear();

	// GUI
	if (gui.renderPass) {
		vulkan.device.destroyRenderPass(gui.renderPass);
	}
	for (auto &frameBuffer : gui.frameBuffers) {
		if (frameBuffer) {
			vulkan.device.destroyFramebuffer(frameBuffer);
		}
	}
	gui.pipeline.destroy();

	// deferred
	if (deferred.renderPass) {
		vulkan.device.destroyRenderPass(deferred.renderPass);
	}
	if (deferred.compositionRenderPass) {
		vulkan.device.destroyRenderPass(deferred.compositionRenderPass);
	}
	for (auto &frameBuffer : deferred.frameBuffers) {
		if (frameBuffer) {
			vulkan.device.destroyFramebuffer(frameBuffer);
		}
	}
	for (auto &frameBuffer : deferred.compositionFrameBuffers) {
		if (frameBuffer) {
			vulkan.device.destroyFramebuffer(frameBuffer);
		}
	}
	deferred.pipeline.destroy();
	deferred.pipelineComposition.destroy();

	// SSR
	for (auto &frameBuffer : ssr.frameBuffers) {
		if (frameBuffer) {
			vulkan.device.destroyFramebuffer(frameBuffer);
		}
	}
	if (ssr.renderPass) {
		vulkan.device.destroyRenderPass(ssr.renderPass);
	}
	ssr.pipeline.destroy();

	// FXAA
	for (auto &frameBuffer : fxaa.frameBuffers) {
		if (frameBuffer) {
			vulkan.device.destroyFramebuffer(frameBuffer);
		}
	}
	if (fxaa.renderPass) {
		vulkan.device.destroyRenderPass(fxaa.renderPass);
	}
	fxaa.pipeline.destroy();
	fxaa.frameImage.destroy();

	// TAA
	taa.previous.destroy();
	taa.frameImage.destroy();
	for (auto& frameBuffer : taa.frameBuffers) {
		if (frameBuffer) {
			vulkan.device.destroyFramebuffer(frameBuffer);
		}
	}
	for (auto& frameBuffer : taa.frameBuffersSharpen) {
		if (frameBuffer) {
			vulkan.device.destroyFramebuffer(frameBuffer);
		}
	}
	if (taa.renderPass) {
		vulkan.device.destroyRenderPass(taa.renderPass);
	}
	if (taa.renderPassSharpen) {
		vulkan.device.destroyRenderPass(taa.renderPassSharpen);
	}
	taa.pipeline.destroy();
	taa.pipelineSharpen.destroy();

	// Bloom
	for (auto &frameBuffer : bloom.frameBuffers) {
		if (frameBuffer) {
			vulkan.device.destroyFramebuffer(frameBuffer);
		}
	}
	if (bloom.renderPassBrightFilter) {
		vulkan.device.destroyRenderPass(bloom.renderPassBrightFilter);
	}
	if (bloom.renderPassGaussianBlur) {
		vulkan.device.destroyRenderPass(bloom.renderPassGaussianBlur);
	}
	if (bloom.renderPassCombine) {
		vulkan.device.destroyRenderPass(bloom.renderPassCombine);
	}
	bloom.pipelineBrightFilter.destroy();
	bloom.pipelineGaussianBlurHorizontal.destroy();
	bloom.pipelineGaussianBlurVertical.destroy();
	bloom.pipelineCombine.destroy();
	bloom.frameImage.destroy();

	// Motion blur
	for (auto &frameBuffer : motionBlur.frameBuffers) {
		if (frameBuffer) {
			vulkan.device.destroyFramebuffer(frameBuffer);
		}
	}
	if (motionBlur.renderPass) {
		vulkan.device.destroyRenderPass(motionBlur.renderPass);
	}
	motionBlur.pipeline.destroy();
	motionBlur.frameImage.destroy();

	// SSAO
	if (ssao.renderPass) {
		vulkan.device.destroyRenderPass(ssao.renderPass);
	}
	if (ssao.blurRenderPass) {
		vulkan.device.destroyRenderPass(ssao.blurRenderPass);
	}
	for (auto &frameBuffer : ssao.frameBuffers) {
		if (frameBuffer) {
			vulkan.device.destroyFramebuffer(frameBuffer);
		}
	}
	for (auto &frameBuffer : ssao.blurFrameBuffers) {
		if (frameBuffer) {
			vulkan.device.destroyFramebuffer(frameBuffer);
		}
	}
	ssao.pipeline.destroy();
	ssao.pipelineBlur.destroy();

	// skyboxes
	if (skyBoxDay.renderPass) {
		vulkan.device.destroyRenderPass(skyBoxDay.renderPass);
	}
	for (auto &frameBuffer : skyBoxDay.frameBuffers) {
		if (frameBuffer) {
			vulkan.device.destroyFramebuffer(frameBuffer);
		}
	}
	skyBoxDay.pipeline.destroy();

	if (skyBoxNight.renderPass) {
		vulkan.device.destroyRenderPass(skyBoxNight.renderPass);
	}
	for (auto &frameBuffer : skyBoxNight.frameBuffers) {
		if (frameBuffer) {
			vulkan.device.destroyFramebuffer(frameBuffer);
		}
	}
	skyBoxNight.pipeline.destroy();

	vulkan.depth->destroy();
	vulkan.swapchain->destroy();
	//- Free resources end ------------------

	//- Recreate resources ------------------
	WIDTH = width;
	HEIGHT = height;
	*vulkan.swapchain = createSwapchain();
	*vulkan.depth = createDepthResources();

	addRenderTarget("viewport", vulkan.surface->formatKHR.format, vk::ImageUsageFlagBits::eTransferSrc);
	addRenderTarget("depth", vk::Format::eR32Sfloat);
	addRenderTarget("normal", vk::Format::eR32G32B32A32Sfloat);
	addRenderTarget("albedo", vulkan.surface->formatKHR.format);
	addRenderTarget("srm", vulkan.surface->formatKHR.format); // Specular Roughness Metallic
	addRenderTarget("ssao", vk::Format::eR16Unorm);
	addRenderTarget("ssaoBlur", vk::Format::eR8Unorm);
	addRenderTarget("ssr", vulkan.surface->formatKHR.format);
	addRenderTarget("velocity", vk::Format::eR16G16Sfloat);
	addRenderTarget("brightFilter", vulkan.surface->formatKHR.format);
	addRenderTarget("gaussianBlurHorizontal", vulkan.surface->formatKHR.format);
	addRenderTarget("gaussianBlurVertical", vulkan.surface->formatKHR.format);
	addRenderTarget("emissive", vulkan.surface->formatKHR.format);
	addRenderTarget("taa", vulkan.surface->formatKHR.format, vk::ImageUsageFlagBits::eTransferSrc);

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

#ifdef RENDER_SKYBOX
	skyBoxDay.createRenderPass();
	skyBoxDay.createFrameBuffers();
	skyBoxDay.createPipeline();
	skyBoxNight.createRenderPass();
	skyBoxNight.createFrameBuffers();
	skyBoxNight.createPipeline();
#endif

	//compute.pipeline = createComputePipeline();
	//compute.updateDescriptorSets();
	//- Recreate resources end --------------
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
	gui.createDescriptorSet(GUI::getDescriptorSetLayout(vulkan.device));
	// DESCRIPTOR SETS FOR SKYBOX
	skyBoxDay.createUniformBuffer(2 * sizeof(mat4));
	skyBoxDay.createDescriptorSet(SkyBox::getDescriptorSetLayout());
	skyBoxNight.createUniformBuffer(2 * sizeof(mat4));
	skyBoxNight.createDescriptorSet(SkyBox::getDescriptorSetLayout());
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
	// DESCRIPTOR SET FOR BLOOM PIPELINEs
	bloom.createUniforms(renderTargets);
	// DESCRIPTOR SET FOR MOTIONBLUR PIPELINE
	motionBlur.createMotionBlurUniforms(renderTargets);
	// DESCRIPTOR SET FOR COMPUTE PIPELINE
	//compute.createComputeUniforms(sizeof(SBOIn));
}

vk::Instance Context::createInstance() const
{
	unsigned extCount;
	if (!SDL_Vulkan_GetInstanceExtensions(vulkan.window, &extCount, nullptr))
		throw std::runtime_error(SDL_GetError());

	std::vector<const char*> instanceExtensions(extCount);
	if (!SDL_Vulkan_GetInstanceExtensions(vulkan.window, &extCount, instanceExtensions.data()))
		throw std::runtime_error(SDL_GetError());

	std::vector<const char*> instanceLayers{};
#ifdef _DEBUG
	auto extensions = vk::enumerateInstanceExtensionProperties();
	for (auto& extension : extensions) {
		if (std::string(extension.extensionName) == "VK_EXT_debug_utils")
			instanceExtensions.push_back("VK_EXT_debug_utils");
	}

	auto layers = vk::enumerateInstanceLayerProperties();
	for (auto layer : layers) {
		if (std::string(layer.layerName) == "VK_LAYER_KHRONOS_validation")
			instanceLayers.push_back("VK_LAYER_KHRONOS_validation");
		if (std::string(layer.layerName) == "VK_LAYER_LUNARG_assistant_layer")
			instanceLayers.push_back("VK_LAYER_LUNARG_assistant_layer");
	}
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

	return vk::createInstance(instInfo);
}

VKAPI_ATTR VkBool32 VKAPI_CALL Context::messageCallback(
	VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
	VkDebugUtilsMessageTypeFlagsEXT messageType,
	const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
	void* pUserData)
{
	if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
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

	return vulkan.instance.createDebugUtilsMessengerEXT(dumci, nullptr, vk::DispatchLoaderDynamic(vulkan.instance));
}

void Context::destroyDebugMessenger() const
{
	vulkan.instance.destroyDebugUtilsMessengerEXT(vulkan.debugMessenger, nullptr, vk::DispatchLoaderDynamic(vulkan.instance));
}

Surface Context::createSurface() const
{
	VkSurfaceKHR _vkSurface;
	if (!SDL_Vulkan_CreateSurface(vulkan.window, VkInstance(vulkan.instance), &_vkSurface))
		throw std::runtime_error(SDL_GetError());

	Surface _surface;
	int width, height;
	SDL_GL_GetDrawableSize(vulkan.window, &width, &height);
	_surface.actualExtent = vk::Extent2D{ static_cast<uint32_t>(width), static_cast<uint32_t>(height) };
	_surface.surface = vk::SurfaceKHR(_vkSurface);

	return _surface;
}

int Context::getGraphicsFamilyId() const
{
#ifdef UNIFIED_GRAPHICS_AND_TRANSFER_QUEUE
	const vk::QueueFlags flags = vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eTransfer;
#else
	const vk::QueueFlags flags = vk::QueueFlagBits::eTransfer;
#endif
	auto& properties = vulkan.queueFamilyProperties;
	for (uint32_t i = 0; i < properties.size(); i++) {
		//find graphics queue family index
		if (properties[i].queueFlags & flags && vulkan.gpu.getSurfaceSupportKHR(i, vulkan.surface->surface))
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
	auto& properties = vulkan.queueFamilyProperties;
	// prefer different families for different queue types, thus the reverse check
	for (int i = static_cast<int>(properties.size()) - 1; i >= 0; --i) {
		//find compute queue family index
		if (properties[i].queueFlags & flags)
			return i;
	}
	return -1;
}

vk::PhysicalDevice Context::findGpu() const
{
	//if (vulkan.graphicsFamilyId < 0 || vulkan.presentFamilyId < 0 || vulkan.computeFamilyId < 0)
	//	return nullptr;
	std::vector<vk::PhysicalDevice> gpuList = vulkan.instance.enumeratePhysicalDevices();

	for (const auto& gpu : gpuList) {
		vulkan.queueFamilyProperties = gpu.getQueueFamilyProperties();
		vk::QueueFlags flags;

		for (const auto& qfp : vulkan.queueFamilyProperties) {
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
	auto caps = vulkan.gpu.getSurfaceCapabilitiesKHR(vulkan.surface->surface);
	// Ensure eTransferSrc bit for blit operations
	if (!(caps.supportedUsageFlags & vk::ImageUsageFlagBits::eTransferSrc))
		throw std::runtime_error("Surface doesnt support vk::ImageUsageFlagBits::eTransferSrc");
	return caps;
}

vk::SurfaceFormatKHR Context::getSurfaceFormat() const
{
	std::vector<vk::SurfaceFormatKHR> formats = vulkan.gpu.getSurfaceFormatsKHR(vulkan.surface->surface);
	auto format = formats[0];
	for (const auto& i : formats) {
		if (i.format == vk::Format::eB8G8R8A8Unorm && i.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear)
			format = i;
	}
	
	// Check for blit operation
	auto const fProps = vulkan.gpu.getFormatProperties(format.format);
	if (!(fProps.optimalTilingFeatures & vk::FormatFeatureFlagBits::eBlitSrc))
		throw std::runtime_error("No blit source operation supported");
	if (!(fProps.optimalTilingFeatures & vk::FormatFeatureFlagBits::eBlitDst))
		throw std::runtime_error("No blit destination operation supported");

	return format;
}

vk::PresentModeKHR Context::getPresentationMode() const
{
	std::vector<vk::PresentModeKHR> presentModes = vulkan.gpu.getSurfacePresentModesKHR(vulkan.surface->surface);

	for (const auto& i : presentModes)
		if (i == vk::PresentModeKHR::eImmediate || i == vk::PresentModeKHR::eMailbox)
			return i;

	return vk::PresentModeKHR::eFifo;
}

vk::PhysicalDeviceProperties Context::getGPUProperties() const
{
	return vulkan.gpu.getProperties();
}

vk::PhysicalDeviceFeatures Context::getGPUFeatures() const
{
	return vulkan.gpu.getFeatures();
}

vk::Device Context::createDevice() const
{
	std::vector<vk::ExtensionProperties> extensionProperties = vulkan.gpu.enumerateDeviceExtensionProperties();

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
	deviceCreateInfo.pEnabledFeatures = &vulkan.gpuFeatures;

	return vulkan.gpu.createDevice(deviceCreateInfo);
}

vk::Queue Context::getGraphicsQueue() const
{
	return vulkan.device.getQueue(vulkan.graphicsFamilyId, 0);
}

vk::Queue vm::Context::getTransferQueue() const
{
	return vulkan.device.getQueue(vulkan.transferFamilyId, 0);
}
vk::Queue Context::getComputeQueue() const
{
	return vulkan.device.getQueue(vulkan.computeFamilyId, 0);
}

Swapchain Context::createSwapchain() const
{
	const VkExtent2D extent = vulkan.surface->actualExtent;
	vulkan.surface->capabilities = getSurfaceCapabilities();

	uint32_t swapchainImageCount = vulkan.surface->capabilities.minImageCount + 1;
	if (vulkan.surface->capabilities.maxImageCount > 0 &&
		swapchainImageCount > vulkan.surface->capabilities.maxImageCount) {
		swapchainImageCount = vulkan.surface->capabilities.maxImageCount;
	}

	vk::SwapchainCreateInfoKHR swapchainCreateInfo;
	swapchainCreateInfo.surface = vulkan.surface->surface;
	swapchainCreateInfo.minImageCount = swapchainImageCount;
	swapchainCreateInfo.imageFormat = vulkan.surface->formatKHR.format;
	swapchainCreateInfo.imageColorSpace = vulkan.surface->formatKHR.colorSpace;
	swapchainCreateInfo.imageExtent = extent;
	swapchainCreateInfo.imageArrayLayers = 1;
	swapchainCreateInfo.imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst;
	swapchainCreateInfo.preTransform = vulkan.surface->capabilities.currentTransform;
	swapchainCreateInfo.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
	swapchainCreateInfo.presentMode = vulkan.surface->presentModeKHR;
	swapchainCreateInfo.clipped = VK_TRUE;
	swapchainCreateInfo.oldSwapchain = vulkan.swapchain && vulkan.swapchain->swapchain ? vulkan.swapchain->swapchain : nullptr;

	// new swapchain with old create info
	Swapchain swapchain;
	swapchain.swapchain = vulkan.device.createSwapchainKHR(swapchainCreateInfo);

	// destroy old swapchain
	if (vulkan.swapchain && vulkan.swapchain->swapchain) {
		vulkan.device.destroySwapchainKHR(vulkan.swapchain->swapchain);
		vulkan.swapchain->swapchain = nullptr;
	}

	// get the swapchain image handlers
	std::vector<vk::Image> images = vulkan.device.getSwapchainImagesKHR(swapchain.swapchain);

	swapchain.images.resize(images.size());
	for (unsigned i = 0; i < images.size(); i++) {
		swapchain.images[i].image = images[i]; // hold the image handlers
		swapchain.images[i].transitionImageLayout(vk::ImageLayout::eUndefined, vk::ImageLayout::ePresentSrcKHR);
		swapchain.images[i].blentAttachment.blendEnable = VK_TRUE;
		swapchain.images[i].blentAttachment.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
		swapchain.images[i].blentAttachment.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
		swapchain.images[i].blentAttachment.colorBlendOp = vk::BlendOp::eAdd;
		swapchain.images[i].blentAttachment.srcAlphaBlendFactor = vk::BlendFactor::eOne;
		swapchain.images[i].blentAttachment.dstAlphaBlendFactor = vk::BlendFactor::eZero;
		swapchain.images[i].blentAttachment.alphaBlendOp = vk::BlendOp::eAdd;
		swapchain.images[i].blentAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
	}

	// create image views for each swapchain image
	for (auto &image : swapchain.images) {
		vk::ImageViewCreateInfo imageViewCreateInfo;
		imageViewCreateInfo.image = image.image;
		imageViewCreateInfo.viewType = vk::ImageViewType::e2D;
		imageViewCreateInfo.format = vulkan.surface->formatKHR.format;
		imageViewCreateInfo.subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 };
		image.view = vulkan.device.createImageView(imageViewCreateInfo);
	}

	return swapchain;
}

vk::CommandPool Context::createCommandPool() const
{
	vk::CommandPoolCreateInfo cpci;
	cpci.queueFamilyIndex = vulkan.graphicsFamilyId;
	cpci.flags = vk::CommandPoolCreateFlagBits::eTransient | vk::CommandPoolCreateFlagBits::eResetCommandBuffer;

	return vulkan.device.createCommandPool(cpci);
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
	renderTargets[name].blentAttachment.blendEnable = name == "albedo" ? VK_TRUE : VK_FALSE;
	renderTargets[name].blentAttachment.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
	renderTargets[name].blentAttachment.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
	renderTargets[name].blentAttachment.colorBlendOp = vk::BlendOp::eAdd;
	renderTargets[name].blentAttachment.srcAlphaBlendFactor = vk::BlendFactor::eOne;
	renderTargets[name].blentAttachment.dstAlphaBlendFactor = vk::BlendFactor::eZero;
	renderTargets[name].blentAttachment.alphaBlendOp = vk::BlendOp::eAdd;
	renderTargets[name].blentAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
}

Image Context::createDepthResources() const
{
	Image _image = Image();
	_image.format = vk::Format::eUndefined;
	std::vector<vk::Format> candidates = { vk::Format::eD32SfloatS8Uint, vk::Format::eD32Sfloat, vk::Format::eD24UnormS8Uint };
	for (auto &format : candidates) {
		vk::FormatProperties props = vulkan.gpu.getFormatProperties(format);
		if ((props.optimalTilingFeatures & vk::FormatFeatureFlagBits::eDepthStencilAttachment) == vk::FormatFeatureFlagBits::eDepthStencilAttachment) {
			_image.format = format;
			break;
		}
	}
	if (_image.format == vk::Format::eUndefined)
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
	cbai.commandPool = vulkan.commandPool;
	cbai.level = vk::CommandBufferLevel::ePrimary;
	cbai.commandBufferCount = bufferCount;

	return vulkan.device.allocateCommandBuffers(cbai);
}

vk::DescriptorPool Context::createDescriptorPool(uint32_t maxDescriptorSets) const
{
	std::vector<vk::DescriptorPoolSize> descPoolsize(4);
	descPoolsize[0].type = vk::DescriptorType::eUniformBuffer;
	descPoolsize[0].descriptorCount =  maxDescriptorSets;
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

	return vulkan.device.createDescriptorPool(createInfo);
}

std::vector<vk::Fence> Context::createFences(const uint32_t fenceCount) const
{
	std::vector<vk::Fence> _fences(fenceCount);
	const vk::FenceCreateInfo fi;

	for (uint32_t i = 0; i < fenceCount; i++) {
		_fences[i] = vulkan.device.createFence(fi);
	}

	return _fences;
}

std::vector<vk::Semaphore> Context::createSemaphores(const uint32_t semaphoresCount) const
{
	std::vector<vk::Semaphore> _semaphores(semaphoresCount);
	const vk::SemaphoreCreateInfo si;

	for (uint32_t i = 0; i < semaphoresCount; i++) {
		_semaphores[i] = vulkan.device.createSemaphore(si);
	}

	return _semaphores;
}

void Context::destroyVkContext()
{
	for (auto& fence : vulkan.fences) {
		if (fence) {
			vulkan.device.destroyFence(fence);
			fence = nullptr;
		}
	}
	for (auto &semaphore : vulkan.semaphores) {
		if (semaphore) {
			vulkan.device.destroySemaphore(semaphore);
			semaphore = nullptr;
		}
	}
	for (auto& rt : renderTargets)
		rt.second.destroy();

	vulkan.depth->destroy();
	delete vulkan.depth;

	if (vulkan.descriptorPool) {
		vulkan.device.destroyDescriptorPool(vulkan.descriptorPool);
	}
	if (vulkan.commandPool) {
		vulkan.device.destroyCommandPool(vulkan.commandPool);
	}
	if (vulkan.commandPool2) {
		vulkan.device.destroyCommandPool(vulkan.commandPool2);
	}

	vulkan.swapchain->destroy();
	delete vulkan.swapchain;

	if (vulkan.device) {
		vulkan.device.destroy();
	}

	if (vulkan.surface->surface) {
		vulkan.instance.destroySurfaceKHR(vulkan.surface->surface);
	}delete vulkan.surface;

#ifdef _DEBUG
	destroyDebugMessenger();
#endif

	if (vulkan.instance) {
		vulkan.instance.destroy();
	}
}