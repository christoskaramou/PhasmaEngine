#include "Context.h"
#include "../Queue/Queue.h"
#include <iostream>
#include <fstream>
#include <filesystem>
#include "../Window/Window.h"

using namespace vm;

//std::vector<Model> Context::models = {};

void Context::initVulkanContext()
{
	vulkan.instance = createInstance();
	vulkan.surface = new Surface(createSurface());
	vulkan.graphicsFamilyId = getGraphicsFamilyId();
	vulkan.presentFamilyId = getPresentFamilyId();
	vulkan.computeFamilyId = getComputeFamilyId();
	vulkan.gpu = findGpu();
	vulkan.gpuProperties = getGPUProperties();
	vulkan.gpuFeatures = getGPUFeatures();
	vulkan.surface->capabilities = getSurfaceCapabilities();
	vulkan.surface->formatKHR = getSurfaceFormat();
	vulkan.surface->presentModeKHR = getPresentationMode();
	vulkan.device = createDevice();
	vulkan.graphicsQueue = getGraphicsQueue();
	vulkan.presentQueue = getPresentQueue();
	vulkan.computeQueue = getComputeQueue();
	vulkan.semaphores = createSemaphores(3);
	vulkan.fences = createFences(2);
	vulkan.swapchain = new Swapchain(createSwapchain());
	vulkan.commandPool = createCommandPool();
	vulkan.commandPool2 = createCommandPool();
	vulkan.descriptorPool = createDescriptorPool(15000); // max number of all descriptor sets to allocate
	vulkan.dynamicCmdBuffer = createCmdBuffers().at(0);
	vulkan.shadowCmdBuffer = createCmdBuffers(3);
	vulkan.depth = new Image(createDepthResources());
}

void Context::initRendering()
{
	// similar init with resize

	addRenderTarget("depth", vk::Format::eR32Sfloat);
	addRenderTarget("normal", vk::Format::eR32G32B32A32Sfloat);
	addRenderTarget("albedo", vk::Format::eR8G8B8A8Unorm);
	addRenderTarget("srm", vk::Format::eR8G8B8A8Unorm); // Specular Roughness Metallic
	addRenderTarget("ssao", vk::Format::eR16Unorm);
	addRenderTarget("ssaoBlur", vk::Format::eR8Unorm);
	addRenderTarget("ssr", vk::Format::eR8G8B8A8Unorm);
	addRenderTarget("composition", vk::Format::eR8G8B8A8Unorm);
	addRenderTarget("composition2", vk::Format::eR8G8B8A8Unorm);
	addRenderTarget("velocity", vk::Format::eR16G16B16A16Sfloat);
	addRenderTarget("brightFilter", vk::Format::eR8G8B8A8Unorm);
	addRenderTarget("gaussianBlurHorizontal", vk::Format::eR8G8B8A8Unorm);
	addRenderTarget("gaussianBlurVertical", vk::Format::eR8G8B8A8Unorm);
	addRenderTarget("emissive", vk::Format::eR8G8B8A8Unorm);

	// render passes
	deferred.renderPass = createDeferredRenderPass();
	deferred.compositionRenderPass = createCompositionRenderPass();
	ssao.renderPass = createSSAORenderPass();
	ssao.blurRenderPass = createSSAOBlurRenderPass();
	ssr.renderPass = createSSRRenderPass();
	fxaa.renderPass = createFXAARenderPass();
	motionBlur.renderPass = createMotionBlurRenderPass();
	gui.renderPass = createGUIRenderPass();
	shadows.renderPass = createShadowsRenderPass();
	bloom.renderPassBrightFilter = createBrightFilterRenderPass();
	bloom.renderPassGaussianBlur = createGaussianBlurRenderPass();
	bloom.renderPassCombine = createCombineRenderPass();

	// frame buffers
	deferred.frameBuffers = createDeferredFrameBuffers();
	deferred.compositionFrameBuffers = createCompositionFrameBuffers();
	ssao.frameBuffers = createSSAOFrameBuffers();
	ssao.blurFrameBuffers = createSSAOBlurFrameBuffers();
	ssr.frameBuffers = createSSRFrameBuffers();
	fxaa.frameBuffers = createFXAAFrameBuffers();
	motionBlur.frameBuffers = createMotionBlurFrameBuffers();
	gui.frameBuffers = createGUIFrameBuffers();
	shadows.frameBuffers = createShadowsFrameBuffers();
	bloom.frameBuffers = createBloomFrameBuffers();

	// pipelines
	gui.pipeline = createPipeline(getPipelineSpecificationsGUI());
	deferred.pipeline = createPipeline(getPipelineSpecificationsDeferred());
	deferred.pipelineComposition = createCompositionPipeline();
	ssao.pipeline = createSSAOPipeline();
	ssao.pipelineBlur = createSSAOBlurPipeline();
	ssr.pipeline = createSSRPipeline();
	fxaa.pipeline = createFXAAPipeline();
	bloom.pipelineBrightFilter = createBrightFilterPipeline();
	bloom.pipelineGaussianBlurHorizontal = createGaussianBlurHorizontalPipeline();
	bloom.pipelineGaussianBlurVertical = createGaussianBlurVerticalPipeline();
	bloom.pipelineCombine = createCombinePipeline();
	motionBlur.pipeline = createMotionBlurPipeline();
	shadows.pipeline = createPipeline(getPipelineSpecificationsShadows());

	skyBoxDay.renderPass = createSkyboxRenderPass();
	skyBoxDay.frameBuffers = createSkyboxFrameBuffers(skyBoxDay);
	skyBoxDay.pipeline = createPipeline(getPipelineSpecificationsSkyBox(skyBoxDay));
	skyBoxNight.renderPass = createSkyboxRenderPass();
	skyBoxNight.frameBuffers = createSkyboxFrameBuffers(skyBoxNight);
	skyBoxNight.pipeline = createPipeline(getPipelineSpecificationsSkyBox(skyBoxNight));

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
	vulkan.surface->actualExtent = { width, height };
	*vulkan.swapchain = createSwapchain();
	*vulkan.depth = createDepthResources();

	addRenderTarget("depth", vk::Format::eR32Sfloat);
	addRenderTarget("normal", vk::Format::eR32G32B32A32Sfloat);
	addRenderTarget("albedo", vk::Format::eR8G8B8A8Unorm);
	addRenderTarget("srm", vk::Format::eR8G8B8A8Unorm); // Specular Roughness Metallic
	addRenderTarget("ssao", vk::Format::eR16Unorm);
	addRenderTarget("ssaoBlur", vk::Format::eR8Unorm);
	addRenderTarget("ssr", vk::Format::eR8G8B8A8Unorm);
	addRenderTarget("composition", vk::Format::eR8G8B8A8Unorm);
	addRenderTarget("composition2", vk::Format::eR8G8B8A8Unorm);
	addRenderTarget("velocity", vk::Format::eR16G16B16A16Sfloat);
	addRenderTarget("brightFilter", vk::Format::eR8G8B8A8Unorm);
	addRenderTarget("gaussianBlurHorizontal", vk::Format::eR8G8B8A8Unorm);
	addRenderTarget("gaussianBlurVertical", vk::Format::eR8G8B8A8Unorm);
	addRenderTarget("emissive", vk::Format::eR8G8B8A8Unorm);

	deferred.renderPass = createDeferredRenderPass();
	deferred.compositionRenderPass = createCompositionRenderPass();
	deferred.frameBuffers = createDeferredFrameBuffers();
	deferred.compositionFrameBuffers = createCompositionFrameBuffers();
	deferred.pipeline = createPipeline(getPipelineSpecificationsDeferred());
	deferred.pipelineComposition = createCompositionPipeline();
	deferred.updateDescriptorSets(renderTargets, lightUniforms);

	ssr.renderPass = createSSRRenderPass();
	ssr.frameBuffers = createSSRFrameBuffers();
	ssr.pipeline = createSSRPipeline();
	ssr.updateDescriptorSets(renderTargets);

	fxaa.renderPass = createFXAARenderPass();
	fxaa.frameBuffers = createFXAAFrameBuffers();
	fxaa.pipeline = createFXAAPipeline();
	fxaa.updateDescriptorSets(renderTargets);

	bloom.renderPassBrightFilter = createBrightFilterRenderPass();
	bloom.renderPassGaussianBlur = createGaussianBlurRenderPass();
	bloom.renderPassCombine = createCombineRenderPass();
	bloom.frameBuffers = createBloomFrameBuffers();
	bloom.pipelineBrightFilter = createBrightFilterPipeline();
	bloom.pipelineGaussianBlurHorizontal = createGaussianBlurHorizontalPipeline();
	bloom.pipelineGaussianBlurVertical = createGaussianBlurVerticalPipeline();
	bloom.pipelineCombine = createCombinePipeline();
	bloom.updateDescriptorSets(renderTargets);

	motionBlur.renderPass = createMotionBlurRenderPass();
	motionBlur.frameBuffers = createMotionBlurFrameBuffers();
	motionBlur.pipeline = createMotionBlurPipeline();
	motionBlur.updateDescriptorSets(renderTargets);

	ssao.renderPass = createSSAORenderPass();
	ssao.blurRenderPass = createSSAOBlurRenderPass();
	ssao.frameBuffers = createSSAOFrameBuffers();
	ssao.blurFrameBuffers = createSSAOBlurFrameBuffers();
	ssao.pipeline = createSSAOPipeline();
	ssao.pipelineBlur = createSSAOBlurPipeline();
	ssao.updateDescriptorSets(renderTargets);

	gui.renderPass = createGUIRenderPass();
	gui.frameBuffers = createGUIFrameBuffers();
	gui.pipeline = createPipeline(getPipelineSpecificationsGUI());

	skyBoxDay.renderPass = createSkyboxRenderPass();
	skyBoxDay.frameBuffers = createSkyboxFrameBuffers(skyBoxDay);
	skyBoxDay.pipeline = createPipeline(getPipelineSpecificationsSkyBox(skyBoxDay));
	skyBoxNight.renderPass = createSkyboxRenderPass();
	skyBoxNight.frameBuffers = createSkyboxFrameBuffers(skyBoxNight);
	skyBoxNight.pipeline = createPipeline(getPipelineSpecificationsSkyBox(skyBoxNight));

	//compute.pipeline = createComputePipeline();
	//compute.updateDescriptorSets();
	//- Recreate resources end --------------
}

// Callbacks for scripts -------------------
static void LoadModel(MonoString* folderPath, MonoString* modelName, uint32_t instances)
{
	std::string curPath = std::filesystem::current_path().string() + "\\";
	std::string path(mono_string_to_utf8(folderPath));
	std::string name(mono_string_to_utf8(modelName));
	for (; instances > 0; instances--)
		Queue::loadModel.push_back({ curPath + path, name });
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

	// MODELS LOAD
#ifdef USE_SCRIPTS
	Script::Init();
	Script::addCallback("Global::LoadModel", LoadModel);
	Script::addCallback("Global::KeyDown", KeyDown);
	Script::addCallback("Global::SetTimeScale", SetTimeScale);
	Script::addCallback("Global::MouseButtonDown", MouseButtonDown);
	Script::addCallback("Global::GetMousePos", GetMousePos);
	Script::addCallback("Global::SetMousePos", SetMousePos);
	Script::addCallback("Global::GetMouseWheel", GetMouseWheel);
	scripts.push_back(new Script("Load"));
#endif
}

void Context::createUniforms()
{
	// DESCRIPTOR SETS FOR GUI
	gui.createDescriptorSet(GUI::getDescriptorSetLayout(vulkan.device));
	// DESCRIPTOR SETS FOR SKYBOX
	skyBoxDay.createUniformBuffer(2 * sizeof(mat4));
	skyBoxDay.createDescriptorSet(SkyBox::getDescriptorSetLayout(vulkan.device));
	skyBoxNight.createUniformBuffer(2 * sizeof(mat4));
	skyBoxNight.createDescriptorSet(SkyBox::getDescriptorSetLayout(vulkan.device));
	// DESCRIPTOR SETS FOR SHADOWS
	shadows.createUniformBuffers();
	shadows.createDescriptorSets();
	// DESCRIPTOR SETS FOR LIGHTS
	getDescriptorSetLayoutLights();
	lightUniforms.createLightUniforms();
	// DESCRIPTOR SETS FOR SSAO
	ssao.createUniforms(renderTargets);
	// DESCRIPTOR SETS FOR COMPOSITION PIPELINE
	deferred.createDeferredUniforms(renderTargets, lightUniforms);
	// DESCRIPTOR SET FOR REFLECTION PIPELINE
	ssr.createSSRUniforms(renderTargets);
	// DESCRIPTOR SET FOR FXAA PIPELINE
	fxaa.createFXAAUniforms(renderTargets);
	// DESCRIPTOR SET FOR BLOOM PIPELINEs
	bloom.createUniforms(renderTargets);
	// DESCRIPTOR SET FOR MOTIONBLUR PIPELINE
	motionBlur.createMotionBlurUniforms(renderTargets);
	// DESCRIPTOR SET FOR COMPUTE PIPELINE
	//compute.createComputeUniforms(sizeof(SBOIn));
}

vk::Instance Context::createInstance()
{
	unsigned extCount;
	if (!SDL_Vulkan_GetInstanceExtensions(vulkan.window, &extCount, nullptr))
		throw std::runtime_error(SDL_GetError());

	std::vector<const char*> instanceExtensions(extCount);
	if (!SDL_Vulkan_GetInstanceExtensions(vulkan.window, &extCount, instanceExtensions.data()))
		throw std::runtime_error(SDL_GetError());

	std::vector<const char*> instanceLayers{};
#ifdef _DEBUG
	instanceLayers.push_back("VK_LAYER_KHRONOS_validation");
	instanceLayers.push_back("VK_LAYER_LUNARG_assistant_layer");
#endif

	vk::ApplicationInfo appInfo;
	appInfo.pApplicationName = "VulkanMonkey3D";
	appInfo.pEngineName = "VulkanMonkey3D";
	appInfo.apiVersion = VK_MAKE_VERSION(1, 1, 0);

	vk::InstanceCreateInfo instInfo;
	instInfo.pApplicationInfo = &appInfo;
	instInfo.enabledLayerCount = static_cast<uint32_t>(instanceLayers.size());
	instInfo.ppEnabledLayerNames = instanceLayers.data();
	instInfo.enabledExtensionCount = (uint32_t)(instanceExtensions.size());
	instInfo.ppEnabledExtensionNames = instanceExtensions.data();

	return vk::createInstance(instInfo);
}

Surface Context::createSurface()
{
	VkSurfaceKHR _vkSurface;
	if (!SDL_Vulkan_CreateSurface(vulkan.window, VkInstance(vulkan.instance), &_vkSurface))
		throw std::runtime_error(SDL_GetError());

	Surface _surface;
	int width, height;
	SDL_GL_GetDrawableSize(vulkan.window, &width, &height);
	_surface.actualExtent = { static_cast<uint32_t>(width), static_cast<uint32_t>(height) };
	_surface.surface = vk::SurfaceKHR(_vkSurface);

	return _surface;
}

int Context::getGraphicsFamilyId()
{
	std::vector<vk::PhysicalDevice> gpuList = vulkan.instance.enumeratePhysicalDevices();

	for (const auto& gpu : gpuList) {

		std::vector<vk::QueueFamilyProperties> properties = gpu.getQueueFamilyProperties();

		for (int i = 0; i < properties.size(); i++) {
			//find graphics queue family index
			if (properties[i].queueFlags & vk::QueueFlagBits::eGraphics)
				return i;
		}
	}
	return -1;
}

int Context::getPresentFamilyId()
{
	std::vector<vk::PhysicalDevice> gpuList = vulkan.instance.enumeratePhysicalDevices();

	for (const auto& gpu : gpuList) {

		std::vector<vk::QueueFamilyProperties> properties = gpu.getQueueFamilyProperties();

		for (uint32_t i = 0; i < properties.size(); ++i) {
			// find present queue family index
			if (properties[i].queueCount > 0 && gpu.getSurfaceSupportKHR(i, vulkan.surface->surface))
				return i;
		}
	}
	return -1;
}

int Context::getComputeFamilyId()
{
	std::vector<vk::PhysicalDevice> gpuList = vulkan.instance.enumeratePhysicalDevices();

	for (const auto& gpu : gpuList) {
		std::vector<vk::QueueFamilyProperties> properties = gpu.getQueueFamilyProperties();

		for (uint32_t i = 0; i < properties.size(); ++i) {
			//find compute queue family index
			if (properties[i].queueFlags & vk::QueueFlagBits::eCompute)
				return i;
		}
	}
	return -1;
}

vk::PhysicalDevice Context::findGpu()
{
	if (vulkan.graphicsFamilyId < 0 || vulkan.presentFamilyId < 0 || vulkan.computeFamilyId < 0)
		return nullptr;
	std::vector<vk::PhysicalDevice> gpuList = vulkan.instance.enumeratePhysicalDevices();

	for (const auto& gpu : gpuList) {
		std::vector<vk::QueueFamilyProperties> properties = gpu.getQueueFamilyProperties();

		if (properties[vulkan.graphicsFamilyId].queueFlags & vk::QueueFlagBits::eGraphics &&
			gpu.getSurfaceSupportKHR(vulkan.presentFamilyId, vulkan.surface->surface) &&
			properties[vulkan.computeFamilyId].queueFlags & vk::QueueFlagBits::eCompute)
			return gpu;
	}
	return nullptr;
}

vk::SurfaceCapabilitiesKHR Context::getSurfaceCapabilities()
{
	return vulkan.gpu.getSurfaceCapabilitiesKHR(vulkan.surface->surface);
}

vk::SurfaceFormatKHR Context::getSurfaceFormat()
{
	std::vector<vk::SurfaceFormatKHR> formats = vulkan.gpu.getSurfaceFormatsKHR(vulkan.surface->surface);

	for (const auto& i : formats) {
		if (i.format == vk::Format::eB8G8R8A8Unorm && i.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear)
			return i;
	}

	return formats[0];
}

vk::PresentModeKHR Context::getPresentationMode()
{
	std::vector<vk::PresentModeKHR> presentModes = vulkan.gpu.getSurfacePresentModesKHR(vulkan.surface->surface);

	for (const auto& i : presentModes)
		if (i == vk::PresentModeKHR::eImmediate || i == vk::PresentModeKHR::eMailbox)
			return i;

	return vk::PresentModeKHR::eFifo;
}

vk::PhysicalDeviceProperties Context::getGPUProperties()
{
	return vulkan.gpu.getProperties();
}

vk::PhysicalDeviceFeatures Context::getGPUFeatures()
{
	return vulkan.gpu.getFeatures();
}

vk::Device Context::createDevice()
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
	queueCreateInfos.push_back({});
	queueCreateInfos.back().queueFamilyIndex = vulkan.graphicsFamilyId;
	queueCreateInfos.back().queueCount = 1;
	queueCreateInfos.back().pQueuePriorities = priorities;

	// compute queue
	if (vulkan.computeFamilyId != vulkan.graphicsFamilyId) {
		queueCreateInfos.push_back({});
		queueCreateInfos.back().queueFamilyIndex = vulkan.computeFamilyId;
		queueCreateInfos.back().queueCount = 1;
		queueCreateInfos.back().pQueuePriorities = priorities;
	}

	vk::DeviceCreateInfo deviceCreateInfo;
	deviceCreateInfo.queueCreateInfoCount = (uint32_t)queueCreateInfos.size();
	deviceCreateInfo.pQueueCreateInfos = queueCreateInfos.data();
	deviceCreateInfo.enabledExtensionCount = (uint32_t)deviceExtensions.size();
	deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions.data();
	deviceCreateInfo.pEnabledFeatures = &vulkan.gpuFeatures;

	return vulkan.gpu.createDevice(deviceCreateInfo);
}

vk::Queue Context::getGraphicsQueue()
{
	return vulkan.device.getQueue(vulkan.graphicsFamilyId, 0);
}

vk::Queue Context::getPresentQueue()
{
	return vulkan.device.getQueue(vulkan.presentFamilyId, 0);
}

vk::Queue Context::getComputeQueue()
{
	return vulkan.device.getQueue(vulkan.computeFamilyId, 0);
}

Swapchain Context::createSwapchain()
{
	VkExtent2D extent = vulkan.surface->actualExtent;
	vulkan.surface->capabilities = getSurfaceCapabilities();
	Swapchain _swapchain;

	uint32_t swapchainImageCount = vulkan.surface->capabilities.minImageCount + 1;
	if (vulkan.surface->capabilities.maxImageCount > 0 &&
		swapchainImageCount > vulkan.surface->capabilities.maxImageCount) {
		swapchainImageCount = vulkan.surface->capabilities.maxImageCount;
	}

	vk::SwapchainKHR oldSwapchain = _swapchain.swapchain;
	vk::SwapchainCreateInfoKHR swapchainCreateInfo;
	swapchainCreateInfo.surface = vulkan.surface->surface;
	swapchainCreateInfo.minImageCount = swapchainImageCount;
	swapchainCreateInfo.imageFormat = vulkan.surface->formatKHR.format;
	swapchainCreateInfo.imageColorSpace = vulkan.surface->formatKHR.colorSpace;
	swapchainCreateInfo.imageExtent = extent;
	swapchainCreateInfo.imageArrayLayers = 1;
	swapchainCreateInfo.imageUsage = vk::ImageUsageFlagBits::eColorAttachment;
	swapchainCreateInfo.preTransform = vulkan.surface->capabilities.currentTransform;
	swapchainCreateInfo.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
	swapchainCreateInfo.presentMode = vulkan.surface->presentModeKHR;
	swapchainCreateInfo.clipped = VK_TRUE;
	swapchainCreateInfo.oldSwapchain = oldSwapchain;

	// new swapchain with old create info
	vk::SwapchainKHR newSwapchain = vulkan.device.createSwapchainKHR(swapchainCreateInfo);

	if (_swapchain.swapchain)
		vulkan.device.destroySwapchainKHR(_swapchain.swapchain);
	_swapchain.swapchain = newSwapchain;

	// get the swapchain image handlers
	std::vector<vk::Image> images = vulkan.device.getSwapchainImagesKHR(_swapchain.swapchain);

	_swapchain.images.resize(images.size());
	for (unsigned i = 0; i < images.size(); i++)
		_swapchain.images[i].image = images[i]; // hold the image handlers

	// create image views for each swapchain image
	for (auto &image : _swapchain.images) {
		vk::ImageViewCreateInfo imageViewCreateInfo;
		imageViewCreateInfo.image = image.image;
		imageViewCreateInfo.viewType = vk::ImageViewType::e2D;
		imageViewCreateInfo.format = vulkan.surface->formatKHR.format;
		imageViewCreateInfo.subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 };
		image.view = vulkan.device.createImageView(imageViewCreateInfo);
	}

	return _swapchain;
}

vk::CommandPool Context::createCommandPool()
{
	vk::CommandPoolCreateInfo cpci;
	cpci.queueFamilyIndex = vulkan.graphicsFamilyId;
	cpci.flags = vk::CommandPoolCreateFlagBits::eTransient | vk::CommandPoolCreateFlagBits::eResetCommandBuffer;

	return vulkan.device.createCommandPool(cpci);
}

void Context::addRenderTarget(const std::string& name, vk::Format format)
{
	if (renderTargets.find(name) != renderTargets.end()) {
		std::cout << "Render Target already exists\n";
		return;
	}
	renderTargets[name] = Image();
	renderTargets[name].format = format;
	renderTargets[name].initialLayout = vk::ImageLayout::eUndefined;
	renderTargets[name].createImage(WIDTH, HEIGHT, vk::ImageTiling::eOptimal,
		vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled, vk::MemoryPropertyFlagBits::eDeviceLocal);
	renderTargets[name].transitionImageLayout(vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal);
	renderTargets[name].createImageView(vk::ImageAspectFlagBits::eColor);
	renderTargets[name].createSampler();
}

vk::RenderPass Context::createDeferredRenderPass()
{
	std::array<vk::AttachmentDescription, 7> attachments{};
	// Deferred targets
	// Depth store
	attachments[0].format = renderTargets["depth"].format;
	attachments[0].samples = vk::SampleCountFlagBits::e1;
	attachments[0].loadOp = vk::AttachmentLoadOp::eClear;
	attachments[0].storeOp = vk::AttachmentStoreOp::eStore;
	attachments[0].stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
	attachments[0].stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
	attachments[0].initialLayout = vk::ImageLayout::eUndefined;
	attachments[0].finalLayout = vk::ImageLayout::eColorAttachmentOptimal;
	// Normals
	attachments[1].format = renderTargets["normal"].format;
	attachments[1].samples = vk::SampleCountFlagBits::e1;
	attachments[1].loadOp = vk::AttachmentLoadOp::eClear;
	attachments[1].storeOp = vk::AttachmentStoreOp::eStore;
	attachments[1].stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
	attachments[1].stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
	attachments[1].initialLayout = vk::ImageLayout::eUndefined;
	attachments[1].finalLayout = vk::ImageLayout::eColorAttachmentOptimal;
	// Albedo
	attachments[2].format = renderTargets["albedo"].format;
	attachments[2].samples = vk::SampleCountFlagBits::e1;
	attachments[2].loadOp = vk::AttachmentLoadOp::eClear;
	attachments[2].storeOp = vk::AttachmentStoreOp::eStore;
	attachments[2].stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
	attachments[2].stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
	attachments[2].initialLayout = vk::ImageLayout::eUndefined;
	attachments[2].finalLayout = vk::ImageLayout::eColorAttachmentOptimal;
	// Specular Roughness Metallic
	attachments[3].format = renderTargets["srm"].format;
	attachments[3].samples = vk::SampleCountFlagBits::e1;
	attachments[3].loadOp = vk::AttachmentLoadOp::eClear;
	attachments[3].storeOp = vk::AttachmentStoreOp::eStore;
	attachments[3].stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
	attachments[3].stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
	attachments[3].initialLayout = vk::ImageLayout::eUndefined;
	attachments[3].finalLayout = vk::ImageLayout::eColorAttachmentOptimal;
	// Velocity
	attachments[4].format = renderTargets["velocity"].format;
	attachments[4].samples = vk::SampleCountFlagBits::e1;
	attachments[4].loadOp = vk::AttachmentLoadOp::eClear;
	attachments[4].storeOp = vk::AttachmentStoreOp::eStore;
	attachments[4].stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
	attachments[4].stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
	attachments[4].initialLayout = vk::ImageLayout::eUndefined;
	attachments[4].finalLayout = vk::ImageLayout::eColorAttachmentOptimal;
	// Emissive
	attachments[5].format = renderTargets["emissive"].format;
	attachments[5].samples = vk::SampleCountFlagBits::e1;
	attachments[5].loadOp = vk::AttachmentLoadOp::eClear;
	attachments[5].storeOp = vk::AttachmentStoreOp::eStore;
	attachments[5].stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
	attachments[5].stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
	attachments[5].initialLayout = vk::ImageLayout::eUndefined;
	attachments[5].finalLayout = vk::ImageLayout::eColorAttachmentOptimal;

	// Depth
	attachments[6].format = vulkan.depth->format;
	attachments[6].samples = vk::SampleCountFlagBits::e1;
	attachments[6].loadOp = vk::AttachmentLoadOp::eClear;
	attachments[6].storeOp = vk::AttachmentStoreOp::eDontCare;
	attachments[6].stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
	attachments[6].stencilStoreOp = vk::AttachmentStoreOp::eStore;
	attachments[6].initialLayout = vk::ImageLayout::eUndefined;
	attachments[6].finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

	std::array<vk::SubpassDescription, 1> subpassDescriptions{};

	std::vector<vk::AttachmentReference> colorReferences{
		{ 0, vk::ImageLayout::eColorAttachmentOptimal },
		{ 1, vk::ImageLayout::eColorAttachmentOptimal },
		{ 2, vk::ImageLayout::eColorAttachmentOptimal },
		{ 3, vk::ImageLayout::eColorAttachmentOptimal },
		{ 4, vk::ImageLayout::eColorAttachmentOptimal },
		{ 5, vk::ImageLayout::eColorAttachmentOptimal }
	};
	vk::AttachmentReference depthReference = { 6, vk::ImageLayout::eDepthStencilAttachmentOptimal };

	subpassDescriptions[0].pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
	subpassDescriptions[0].colorAttachmentCount = static_cast<uint32_t>(colorReferences.size());
	subpassDescriptions[0].pColorAttachments = colorReferences.data();
	subpassDescriptions[0].pDepthStencilAttachment = &depthReference;


	// Subpass dependencies for layout transitions
	std::vector<vk::SubpassDependency> dependencies(2);
	dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
	dependencies[0].dstSubpass = 0;
	dependencies[0].srcStageMask = vk::PipelineStageFlagBits::eBottomOfPipe;
	dependencies[0].dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
	dependencies[0].srcAccessMask = vk::AccessFlagBits::eMemoryRead;
	dependencies[0].dstAccessMask = vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;
	dependencies[0].dependencyFlags = vk::DependencyFlagBits::eByRegion;
	dependencies[1].srcSubpass = 0;
	dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
	dependencies[1].srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
	dependencies[1].dstStageMask = vk::PipelineStageFlagBits::eBottomOfPipe;
	dependencies[1].srcAccessMask = vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;
	dependencies[1].dstAccessMask = vk::AccessFlagBits::eMemoryRead;
	dependencies[1].dependencyFlags = vk::DependencyFlagBits::eByRegion;

	vk::RenderPassCreateInfo renderPassInfo = {};
	renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
	renderPassInfo.pAttachments = attachments.data();
	renderPassInfo.subpassCount = static_cast<uint32_t>(subpassDescriptions.size());
	renderPassInfo.pSubpasses = subpassDescriptions.data();
	renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
	renderPassInfo.pDependencies = dependencies.data();

	return vulkan.device.createRenderPass(renderPassInfo);
}

vk::RenderPass Context::createCompositionRenderPass()
{
	std::array<vk::AttachmentDescription, 2> attachments{};
	// Color target
	attachments[0].format = vulkan.surface->formatKHR.format;
	attachments[0].samples = vk::SampleCountFlagBits::e1;
	attachments[0].loadOp = vk::AttachmentLoadOp::eLoad;
	attachments[0].storeOp = vk::AttachmentStoreOp::eStore;
	attachments[0].stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
	attachments[0].stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
	attachments[0].initialLayout = vk::ImageLayout::eUndefined;
	attachments[0].finalLayout = vk::ImageLayout::ePresentSrcKHR;

	// Composition out Image
	attachments[1].format = renderTargets["composition"].format;
	attachments[1].samples = vk::SampleCountFlagBits::e1;
	attachments[1].loadOp = vk::AttachmentLoadOp::eClear;
	attachments[1].storeOp = vk::AttachmentStoreOp::eStore;
	attachments[1].stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
	attachments[1].stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
	attachments[1].initialLayout = vk::ImageLayout::eUndefined;
	attachments[1].finalLayout = vk::ImageLayout::eColorAttachmentOptimal;

	std::array<vk::SubpassDescription, 1> subpassDescriptions{};

	std::vector<vk::AttachmentReference> colorReferences{ 
		{ 0, vk::ImageLayout::eColorAttachmentOptimal },
		{ 1, vk::ImageLayout::eColorAttachmentOptimal }
	};

	subpassDescriptions[0].pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
	subpassDescriptions[0].colorAttachmentCount = static_cast<uint32_t>(colorReferences.size());
	subpassDescriptions[0].pColorAttachments = colorReferences.data();
	subpassDescriptions[0].pDepthStencilAttachment = nullptr;


	// Subpass dependencies for layout transitions
	std::vector<vk::SubpassDependency> dependencies(2);
	dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
	dependencies[0].dstSubpass = 0;
	dependencies[0].srcStageMask = vk::PipelineStageFlagBits::eBottomOfPipe;
	dependencies[0].dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
	dependencies[0].srcAccessMask = vk::AccessFlagBits::eMemoryRead;
	dependencies[0].dstAccessMask = vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;
	dependencies[0].dependencyFlags = vk::DependencyFlagBits::eByRegion;
	dependencies[1].srcSubpass = 0;
	dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
	dependencies[1].srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
	dependencies[1].dstStageMask = vk::PipelineStageFlagBits::eBottomOfPipe;
	dependencies[1].srcAccessMask = vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;
	dependencies[1].dstAccessMask = vk::AccessFlagBits::eMemoryRead;
	dependencies[1].dependencyFlags = vk::DependencyFlagBits::eByRegion;

	vk::RenderPassCreateInfo renderPassInfo = {};
	renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
	renderPassInfo.pAttachments = attachments.data();
	renderPassInfo.subpassCount = static_cast<uint32_t>(subpassDescriptions.size());
	renderPassInfo.pSubpasses = subpassDescriptions.data();
	renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
	renderPassInfo.pDependencies = dependencies.data();

	return vulkan.device.createRenderPass(renderPassInfo);
}

vk::RenderPass Context::createSSAORenderPass()
{
	std::array<vk::AttachmentDescription, 1> attachments{};
	// Color attachment
	attachments[0].format = renderTargets["ssao"].format;
	attachments[0].samples = vk::SampleCountFlagBits::e1;
	attachments[0].loadOp = vk::AttachmentLoadOp::eClear;
	attachments[0].storeOp = vk::AttachmentStoreOp::eStore;
	attachments[0].stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
	attachments[0].stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
	attachments[0].initialLayout = vk::ImageLayout::eUndefined;
	attachments[0].finalLayout = vk::ImageLayout::eColorAttachmentOptimal;

	vk::AttachmentReference colorReference = { 0, vk::ImageLayout::eColorAttachmentOptimal };

	vk::SubpassDescription subpassDescription;
	subpassDescription.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
	subpassDescription.colorAttachmentCount = 1;
	subpassDescription.pColorAttachments = &colorReference;
	subpassDescription.pDepthStencilAttachment = nullptr;

	// Subpass dependencies for layout transitions
	std::vector<vk::SubpassDependency> dependencies(2);
	dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
	dependencies[0].dstSubpass = 0;
	dependencies[0].srcStageMask = vk::PipelineStageFlagBits::eBottomOfPipe;
	dependencies[0].dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
	dependencies[0].srcAccessMask = vk::AccessFlagBits::eMemoryRead;
	dependencies[0].dstAccessMask = vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;
	dependencies[0].dependencyFlags = vk::DependencyFlagBits::eByRegion;
	dependencies[1].srcSubpass = 0;
	dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
	dependencies[1].srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
	dependencies[1].dstStageMask = vk::PipelineStageFlagBits::eBottomOfPipe;
	dependencies[1].srcAccessMask = vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;
	dependencies[1].dstAccessMask = vk::AccessFlagBits::eMemoryRead;
	dependencies[1].dependencyFlags = vk::DependencyFlagBits::eByRegion;

	vk::RenderPassCreateInfo renderPassInfo = {};
	renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
	renderPassInfo.pAttachments = attachments.data();
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpassDescription;
	renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
	renderPassInfo.pDependencies = dependencies.data();

	return vulkan.device.createRenderPass(renderPassInfo);
}

vk::RenderPass Context::createSSAOBlurRenderPass()
{
	std::array<vk::AttachmentDescription, 1> attachments{};
	// Color attachment
	attachments[0].format = renderTargets["ssaoBlur"].format;
	attachments[0].samples = vk::SampleCountFlagBits::e1;
	attachments[0].loadOp = vk::AttachmentLoadOp::eClear;
	attachments[0].storeOp = vk::AttachmentStoreOp::eStore;
	attachments[0].stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
	attachments[0].stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
	attachments[0].initialLayout = vk::ImageLayout::eUndefined;
	attachments[0].finalLayout = vk::ImageLayout::eColorAttachmentOptimal;


	vk::AttachmentReference colorReference = { 0, vk::ImageLayout::eColorAttachmentOptimal };

	vk::SubpassDescription subpassDescription;
	subpassDescription.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
	subpassDescription.colorAttachmentCount = 1;
	subpassDescription.pColorAttachments = &colorReference;
	subpassDescription.pDepthStencilAttachment = nullptr;

	// Subpass dependencies for layout transitions
	std::vector<vk::SubpassDependency> dependencies(2);
	dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
	dependencies[0].dstSubpass = 0;
	dependencies[0].srcStageMask = vk::PipelineStageFlagBits::eBottomOfPipe;
	dependencies[0].dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
	dependencies[0].srcAccessMask = vk::AccessFlagBits::eMemoryRead;
	dependencies[0].dstAccessMask = vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;
	dependencies[0].dependencyFlags = vk::DependencyFlagBits::eByRegion;
	dependencies[1].srcSubpass = 0;
	dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
	dependencies[1].srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
	dependencies[1].dstStageMask = vk::PipelineStageFlagBits::eBottomOfPipe;
	dependencies[1].srcAccessMask = vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;
	dependencies[1].dstAccessMask = vk::AccessFlagBits::eMemoryRead;
	dependencies[1].dependencyFlags = vk::DependencyFlagBits::eByRegion;

	vk::RenderPassCreateInfo renderPassInfo = {};
	renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
	renderPassInfo.pAttachments = attachments.data();
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpassDescription;
	renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
	renderPassInfo.pDependencies = dependencies.data();

	return vulkan.device.createRenderPass(renderPassInfo);
}

vk::RenderPass Context::createMotionBlurRenderPass()
{
	std::array<vk::AttachmentDescription, 1> attachments{};
	// Color attachment
	attachments[0].format = vulkan.surface->formatKHR.format;
	attachments[0].samples = vk::SampleCountFlagBits::e1;
	attachments[0].loadOp = vk::AttachmentLoadOp::eLoad;
	attachments[0].storeOp = vk::AttachmentStoreOp::eStore;
	attachments[0].stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
	attachments[0].stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
	attachments[0].initialLayout = vk::ImageLayout::eUndefined;
	attachments[0].finalLayout = vk::ImageLayout::ePresentSrcKHR;

	vk::AttachmentReference colorReference = { 0, vk::ImageLayout::eColorAttachmentOptimal };

	vk::SubpassDescription subpassDescription;
	subpassDescription.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
	subpassDescription.colorAttachmentCount = 1;
	subpassDescription.pColorAttachments = &colorReference;
	subpassDescription.pDepthStencilAttachment = nullptr;

	// Subpass dependencies for layout transitions
	std::vector<vk::SubpassDependency> dependencies(2);
	dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
	dependencies[0].dstSubpass = 0;
	dependencies[0].srcStageMask = vk::PipelineStageFlagBits::eBottomOfPipe;
	dependencies[0].dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
	dependencies[0].srcAccessMask = vk::AccessFlagBits::eMemoryRead;
	dependencies[0].dstAccessMask = vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;
	dependencies[0].dependencyFlags = vk::DependencyFlagBits::eByRegion;
	dependencies[1].srcSubpass = 0;
	dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
	dependencies[1].srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
	dependencies[1].dstStageMask = vk::PipelineStageFlagBits::eBottomOfPipe;
	dependencies[1].srcAccessMask = vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;
	dependencies[1].dstAccessMask = vk::AccessFlagBits::eMemoryRead;
	dependencies[1].dependencyFlags = vk::DependencyFlagBits::eByRegion;

	vk::RenderPassCreateInfo renderPassInfo = {};
	renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
	renderPassInfo.pAttachments = attachments.data();
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpassDescription;
	renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
	renderPassInfo.pDependencies = dependencies.data();

	return vulkan.device.createRenderPass(renderPassInfo);
}

vk::RenderPass Context::createSSRRenderPass()
{
	std::array<vk::AttachmentDescription, 1> attachments{};
	// Color attachment
	attachments[0].format = renderTargets["ssr"].format;
	attachments[0].samples = vk::SampleCountFlagBits::e1;
	attachments[0].loadOp = vk::AttachmentLoadOp::eClear;
	attachments[0].storeOp = vk::AttachmentStoreOp::eStore;
	attachments[0].stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
	attachments[0].stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
	attachments[0].initialLayout = vk::ImageLayout::eUndefined;
	attachments[0].finalLayout = vk::ImageLayout::eColorAttachmentOptimal;


	vk::AttachmentReference colorReference = { 0, vk::ImageLayout::eColorAttachmentOptimal };

	vk::SubpassDescription subpassDescription;
	subpassDescription.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
	subpassDescription.colorAttachmentCount = 1;
	subpassDescription.pColorAttachments = &colorReference;
	subpassDescription.pDepthStencilAttachment = nullptr;

	// Subpass dependencies for layout transitions
	std::vector<vk::SubpassDependency> dependencies(2);
	dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
	dependencies[0].dstSubpass = 0;
	dependencies[0].srcStageMask = vk::PipelineStageFlagBits::eBottomOfPipe;
	dependencies[0].dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
	dependencies[0].srcAccessMask = vk::AccessFlagBits::eMemoryRead;
	dependencies[0].dstAccessMask = vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;
	dependencies[0].dependencyFlags = vk::DependencyFlagBits::eByRegion;
	dependencies[1].srcSubpass = 0;
	dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
	dependencies[1].srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
	dependencies[1].dstStageMask = vk::PipelineStageFlagBits::eBottomOfPipe;
	dependencies[1].srcAccessMask = vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;
	dependencies[1].dstAccessMask = vk::AccessFlagBits::eMemoryRead;
	dependencies[1].dependencyFlags = vk::DependencyFlagBits::eByRegion;

	vk::RenderPassCreateInfo renderPassInfo = {};
	renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
	renderPassInfo.pAttachments = attachments.data();
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpassDescription;
	renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
	renderPassInfo.pDependencies = dependencies.data();

	return vulkan.device.createRenderPass(renderPassInfo);
}

vk::RenderPass vm::Context::createFXAARenderPass()
{
	std::array<vk::AttachmentDescription, 2> attachments{};
	// Swqpchain attachment
	attachments[0].format = vulkan.surface->formatKHR.format;
	attachments[0].samples = vk::SampleCountFlagBits::e1;
	attachments[0].loadOp = vk::AttachmentLoadOp::eLoad;
	attachments[0].storeOp = vk::AttachmentStoreOp::eStore;
	attachments[0].stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
	attachments[0].stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
	attachments[0].initialLayout = vk::ImageLayout::eUndefined;
	attachments[0].finalLayout = vk::ImageLayout::ePresentSrcKHR;
	// FXAA output
	attachments[1].format = renderTargets["composition2"].format;
	attachments[1].samples = vk::SampleCountFlagBits::e1;
	attachments[1].loadOp = vk::AttachmentLoadOp::eClear;
	attachments[1].storeOp = vk::AttachmentStoreOp::eStore;
	attachments[1].stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
	attachments[1].stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
	attachments[1].initialLayout = vk::ImageLayout::eUndefined;
	attachments[1].finalLayout = vk::ImageLayout::eColorAttachmentOptimal;

	std::vector<vk::AttachmentReference> colorReferences{
		{ 0, vk::ImageLayout::eColorAttachmentOptimal },
		{ 1, vk::ImageLayout::eColorAttachmentOptimal }
	};

	vk::SubpassDescription subpassDescription;
	subpassDescription.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
	subpassDescription.colorAttachmentCount = static_cast<uint32_t>(colorReferences.size());
	subpassDescription.pColorAttachments = colorReferences.data();
	subpassDescription.pDepthStencilAttachment = nullptr;

	// Subpass dependencies for layout transitions
	std::vector<vk::SubpassDependency> dependencies(2);
	dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
	dependencies[0].dstSubpass = 0;
	dependencies[0].srcStageMask = vk::PipelineStageFlagBits::eBottomOfPipe;
	dependencies[0].dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
	dependencies[0].srcAccessMask = vk::AccessFlagBits::eMemoryRead;
	dependencies[0].dstAccessMask = vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;
	dependencies[0].dependencyFlags = vk::DependencyFlagBits::eByRegion;
	dependencies[1].srcSubpass = 0;
	dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
	dependencies[1].srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
	dependencies[1].dstStageMask = vk::PipelineStageFlagBits::eBottomOfPipe;
	dependencies[1].srcAccessMask = vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;
	dependencies[1].dstAccessMask = vk::AccessFlagBits::eMemoryRead;
	dependencies[1].dependencyFlags = vk::DependencyFlagBits::eByRegion;

	vk::RenderPassCreateInfo renderPassInfo = {};
	renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
	renderPassInfo.pAttachments = attachments.data();
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpassDescription;
	renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
	renderPassInfo.pDependencies = dependencies.data();

	return vulkan.device.createRenderPass(renderPassInfo);
}

vk::RenderPass vm::Context::createBrightFilterRenderPass()
{
	std::array<vk::AttachmentDescription, 1> attachments{};
	// Color attachment
	attachments[0].format = renderTargets["brightFilter"].format;
	attachments[0].samples = vk::SampleCountFlagBits::e1;
	attachments[0].loadOp = vk::AttachmentLoadOp::eClear;
	attachments[0].storeOp = vk::AttachmentStoreOp::eStore;
	attachments[0].stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
	attachments[0].stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
	attachments[0].initialLayout = vk::ImageLayout::eUndefined;
	attachments[0].finalLayout = vk::ImageLayout::eColorAttachmentOptimal;


	vk::AttachmentReference colorReference = { 0, vk::ImageLayout::eColorAttachmentOptimal };

	vk::SubpassDescription subpassDescription;
	subpassDescription.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
	subpassDescription.colorAttachmentCount = 1;
	subpassDescription.pColorAttachments = &colorReference;
	subpassDescription.pDepthStencilAttachment = nullptr;

	// Subpass dependencies for layout transitions
	std::vector<vk::SubpassDependency> dependencies(2);
	dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
	dependencies[0].dstSubpass = 0;
	dependencies[0].srcStageMask = vk::PipelineStageFlagBits::eBottomOfPipe;
	dependencies[0].dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
	dependencies[0].srcAccessMask = vk::AccessFlagBits::eMemoryRead;
	dependencies[0].dstAccessMask = vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;
	dependencies[0].dependencyFlags = vk::DependencyFlagBits::eByRegion;
	dependencies[1].srcSubpass = 0;
	dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
	dependencies[1].srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
	dependencies[1].dstStageMask = vk::PipelineStageFlagBits::eBottomOfPipe;
	dependencies[1].srcAccessMask = vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;
	dependencies[1].dstAccessMask = vk::AccessFlagBits::eMemoryRead;
	dependencies[1].dependencyFlags = vk::DependencyFlagBits::eByRegion;

	vk::RenderPassCreateInfo renderPassInfo = {};
	renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
	renderPassInfo.pAttachments = attachments.data();
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpassDescription;
	renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
	renderPassInfo.pDependencies = dependencies.data();

	return vulkan.device.createRenderPass(renderPassInfo);
}

vk::RenderPass vm::Context::createGaussianBlurRenderPass()
{
	std::array<vk::AttachmentDescription, 1> attachments{};
	// Color attachment
	attachments[0].format = renderTargets["gaussianBlurHorizontal"].format;
	attachments[0].samples = vk::SampleCountFlagBits::e1;
	attachments[0].loadOp = vk::AttachmentLoadOp::eClear;
	attachments[0].storeOp = vk::AttachmentStoreOp::eStore;
	attachments[0].stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
	attachments[0].stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
	attachments[0].initialLayout = vk::ImageLayout::eUndefined;
	attachments[0].finalLayout = vk::ImageLayout::eColorAttachmentOptimal;


	vk::AttachmentReference colorReference = { 0, vk::ImageLayout::eColorAttachmentOptimal };

	vk::SubpassDescription subpassDescription;
	subpassDescription.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
	subpassDescription.colorAttachmentCount = 1;
	subpassDescription.pColorAttachments = &colorReference;
	subpassDescription.pDepthStencilAttachment = nullptr;

	// Subpass dependencies for layout transitions
	std::vector<vk::SubpassDependency> dependencies(2);
	dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
	dependencies[0].dstSubpass = 0;
	dependencies[0].srcStageMask = vk::PipelineStageFlagBits::eBottomOfPipe;
	dependencies[0].dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
	dependencies[0].srcAccessMask = vk::AccessFlagBits::eMemoryRead;
	dependencies[0].dstAccessMask = vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;
	dependencies[0].dependencyFlags = vk::DependencyFlagBits::eByRegion;
	dependencies[1].srcSubpass = 0;
	dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
	dependencies[1].srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
	dependencies[1].dstStageMask = vk::PipelineStageFlagBits::eBottomOfPipe;
	dependencies[1].srcAccessMask = vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;
	dependencies[1].dstAccessMask = vk::AccessFlagBits::eMemoryRead;
	dependencies[1].dependencyFlags = vk::DependencyFlagBits::eByRegion;

	vk::RenderPassCreateInfo renderPassInfo = {};
	renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
	renderPassInfo.pAttachments = attachments.data();
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpassDescription;
	renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
	renderPassInfo.pDependencies = dependencies.data();

	return vulkan.device.createRenderPass(renderPassInfo);
}

vk::RenderPass vm::Context::createCombineRenderPass()
{
	std::array<vk::AttachmentDescription, 2> attachments{};
	// Swapchain attachment
	attachments[0].format = vulkan.surface->formatKHR.format;
	attachments[0].samples = vk::SampleCountFlagBits::e1;
	attachments[0].loadOp = vk::AttachmentLoadOp::eLoad;
	attachments[0].storeOp = vk::AttachmentStoreOp::eStore;
	attachments[0].stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
	attachments[0].stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
	attachments[0].initialLayout = vk::ImageLayout::eUndefined;
	attachments[0].finalLayout = vk::ImageLayout::ePresentSrcKHR;
	// Color attachment
	attachments[1].format = renderTargets["composition"].format;
	attachments[1].samples = vk::SampleCountFlagBits::e1;
	attachments[1].loadOp = vk::AttachmentLoadOp::eClear;
	attachments[1].storeOp = vk::AttachmentStoreOp::eStore;
	attachments[1].stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
	attachments[1].stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
	attachments[1].initialLayout = vk::ImageLayout::eUndefined;
	attachments[1].finalLayout = vk::ImageLayout::eColorAttachmentOptimal;


	std::vector<vk::AttachmentReference> colorReferences{
		{ 0, vk::ImageLayout::eColorAttachmentOptimal },
		{ 1, vk::ImageLayout::eColorAttachmentOptimal }
	};

	vk::SubpassDescription subpassDescription;
	subpassDescription.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
	subpassDescription.colorAttachmentCount = static_cast<uint32_t>(colorReferences.size());
	subpassDescription.pColorAttachments = colorReferences.data();
	subpassDescription.pDepthStencilAttachment = nullptr;

	// Subpass dependencies for layout transitions
	std::vector<vk::SubpassDependency> dependencies(2);
	dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
	dependencies[0].dstSubpass = 0;
	dependencies[0].srcStageMask = vk::PipelineStageFlagBits::eBottomOfPipe;
	dependencies[0].dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
	dependencies[0].srcAccessMask = vk::AccessFlagBits::eMemoryRead;
	dependencies[0].dstAccessMask = vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;
	dependencies[0].dependencyFlags = vk::DependencyFlagBits::eByRegion;
	dependencies[1].srcSubpass = 0;
	dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
	dependencies[1].srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
	dependencies[1].dstStageMask = vk::PipelineStageFlagBits::eBottomOfPipe;
	dependencies[1].srcAccessMask = vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;
	dependencies[1].dstAccessMask = vk::AccessFlagBits::eMemoryRead;
	dependencies[1].dependencyFlags = vk::DependencyFlagBits::eByRegion;

	vk::RenderPassCreateInfo renderPassInfo = {};
	renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
	renderPassInfo.pAttachments = attachments.data();
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpassDescription;
	renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
	renderPassInfo.pDependencies = dependencies.data();

	return vulkan.device.createRenderPass(renderPassInfo);
}

vk::RenderPass Context::createGUIRenderPass()
{
	std::array<vk::AttachmentDescription, 1> attachments{};
	// Color attachment
	attachments[0].format = vulkan.surface->formatKHR.format;
	attachments[0].samples = vk::SampleCountFlagBits::e1;
	attachments[0].loadOp = vk::AttachmentLoadOp::eLoad;
	attachments[0].storeOp = vk::AttachmentStoreOp::eStore;
	attachments[0].stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
	attachments[0].stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
	attachments[0].initialLayout = vk::ImageLayout::eUndefined;
	attachments[0].finalLayout = vk::ImageLayout::ePresentSrcKHR;

	std::array<vk::SubpassDescription, 1> subpassDescriptions{};

	vk::AttachmentReference colorReference = { 0, vk::ImageLayout::eColorAttachmentOptimal };

	subpassDescriptions[0].pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
	subpassDescriptions[0].colorAttachmentCount = 1;
	subpassDescriptions[0].pColorAttachments = &colorReference;

	// Subpass dependencies for layout transitions
	std::vector<vk::SubpassDependency> dependencies(2);
	dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
	dependencies[0].dstSubpass = 0;
	dependencies[0].srcStageMask = vk::PipelineStageFlagBits::eBottomOfPipe;
	dependencies[0].dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
	dependencies[0].srcAccessMask = vk::AccessFlagBits::eMemoryRead;
	dependencies[0].dstAccessMask = vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;
	dependencies[0].dependencyFlags = vk::DependencyFlagBits::eByRegion;
	dependencies[1].srcSubpass = 0;
	dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
	dependencies[1].srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
	dependencies[1].dstStageMask = vk::PipelineStageFlagBits::eBottomOfPipe;
	dependencies[1].srcAccessMask = vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;
	dependencies[1].dstAccessMask = vk::AccessFlagBits::eMemoryRead;
	dependencies[1].dependencyFlags = vk::DependencyFlagBits::eByRegion;

	vk::RenderPassCreateInfo renderPassInfo = {};
	renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
	renderPassInfo.pAttachments = attachments.data();
	renderPassInfo.subpassCount = static_cast<uint32_t>(subpassDescriptions.size());
	renderPassInfo.pSubpasses = subpassDescriptions.data();
	renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
	renderPassInfo.pDependencies = dependencies.data();

	return vulkan.device.createRenderPass(renderPassInfo);
}


vk::RenderPass Context::createShadowsRenderPass()
{
	vk::AttachmentDescription attachment;
	attachment.format = vulkan.depth->format;
	attachment.samples = vk::SampleCountFlagBits::e1;
	attachment.loadOp = vk::AttachmentLoadOp::eClear;
	attachment.storeOp = vk::AttachmentStoreOp::eStore;
	attachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
	attachment.stencilStoreOp = vk::AttachmentStoreOp::eStore;
	attachment.initialLayout = vk::ImageLayout::eUndefined;
	attachment.finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

	vk::AttachmentReference depthAttachmentRef;
	depthAttachmentRef.attachment = 0;
	depthAttachmentRef.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

	vk::SubpassDescription subpassDesc;
	subpassDesc.pDepthStencilAttachment = &depthAttachmentRef;

	// Subpass dependencies for layout transitions
	std::vector<vk::SubpassDependency> dependencies(2);
	dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
	dependencies[0].dstSubpass = 0;
	dependencies[0].srcStageMask = vk::PipelineStageFlagBits::eBottomOfPipe;
	dependencies[0].dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
	dependencies[0].srcAccessMask = vk::AccessFlagBits::eMemoryRead;
	dependencies[0].dstAccessMask = vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;
	dependencies[0].dependencyFlags = vk::DependencyFlagBits::eByRegion;
	dependencies[1].srcSubpass = 0;
	dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
	dependencies[1].srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
	dependencies[1].dstStageMask = vk::PipelineStageFlagBits::eBottomOfPipe;
	dependencies[1].srcAccessMask = vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;
	dependencies[1].dstAccessMask = vk::AccessFlagBits::eMemoryRead;
	dependencies[1].dependencyFlags = vk::DependencyFlagBits::eByRegion;

	vk::RenderPassCreateInfo rpci;
	rpci.attachmentCount = 1;
	rpci.pAttachments = &attachment;
	rpci.subpassCount = 1;
	rpci.pSubpasses = &subpassDesc;
	rpci.dependencyCount = (uint32_t)dependencies.size();
	rpci.pDependencies = dependencies.data();

	return vulkan.device.createRenderPass(rpci);
}

vk::RenderPass vm::Context::createSkyboxRenderPass()
{
	std::array<vk::AttachmentDescription, 1> attachments{};
	// Color attachment
	attachments[0].format = vulkan.surface->formatKHR.format;
	attachments[0].samples = vk::SampleCountFlagBits::e1;
	attachments[0].loadOp = vk::AttachmentLoadOp::eClear;
	attachments[0].storeOp = vk::AttachmentStoreOp::eStore;
	attachments[0].stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
	attachments[0].stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
	attachments[0].initialLayout = vk::ImageLayout::eUndefined;
	attachments[0].finalLayout = vk::ImageLayout::eColorAttachmentOptimal;


	vk::AttachmentReference colorReference = { 0, vk::ImageLayout::eColorAttachmentOptimal };

	vk::SubpassDescription subpassDescription;
	subpassDescription.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
	subpassDescription.colorAttachmentCount = 1;
	subpassDescription.pColorAttachments = &colorReference;
	subpassDescription.pDepthStencilAttachment = nullptr;

	// Subpass dependencies for layout transitions
	std::vector<vk::SubpassDependency> dependencies(2);
	dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
	dependencies[0].dstSubpass = 0;
	dependencies[0].srcStageMask = vk::PipelineStageFlagBits::eBottomOfPipe;
	dependencies[0].dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
	dependencies[0].srcAccessMask = vk::AccessFlagBits::eMemoryRead;
	dependencies[0].dstAccessMask = vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;
	dependencies[0].dependencyFlags = vk::DependencyFlagBits::eByRegion;
	dependencies[1].srcSubpass = 0;
	dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
	dependencies[1].srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
	dependencies[1].dstStageMask = vk::PipelineStageFlagBits::eBottomOfPipe;
	dependencies[1].srcAccessMask = vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;
	dependencies[1].dstAccessMask = vk::AccessFlagBits::eMemoryRead;
	dependencies[1].dependencyFlags = vk::DependencyFlagBits::eByRegion;

	vk::RenderPassCreateInfo renderPassInfo = {};
	renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
	renderPassInfo.pAttachments = attachments.data();
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpassDescription;
	renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
	renderPassInfo.pDependencies = dependencies.data();

	return vulkan.device.createRenderPass(renderPassInfo);
}

Image Context::createDepthResources()
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

	_image.createImage(WIDTH, HEIGHT, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eDepthStencilAttachment, vk::MemoryPropertyFlagBits::eDeviceLocal);
	_image.createImageView(vk::ImageAspectFlagBits::eDepth);

	_image.addressMode = vk::SamplerAddressMode::eClampToEdge;
	_image.maxAnisotropy = 1.f;
	_image.borderColor = vk::BorderColor::eFloatOpaqueWhite;
	_image.samplerCompareEnable = VK_TRUE;
	_image.createSampler();

	_image.transitionImageLayout(vk::ImageLayout::eUndefined, vk::ImageLayout::eDepthStencilAttachmentOptimal);

	return _image;
}

std::vector<vk::Framebuffer> Context::createDeferredFrameBuffers()
{
	std::vector<vk::Framebuffer> _frameBuffers(vulkan.swapchain->images.size());

	for (size_t i = 0; i < _frameBuffers.size(); ++i) {
		std::vector<vk::ImageView> attachments = {
			renderTargets["depth"].view,
			renderTargets["normal"].view,
			renderTargets["albedo"].view,
			renderTargets["srm"].view,
			renderTargets["velocity"].view,
			renderTargets["emissive"].view,
			vulkan.depth->view
		};
		vk::FramebufferCreateInfo fbci;
		fbci.renderPass = deferred.renderPass;
		fbci.attachmentCount = static_cast<uint32_t>(attachments.size());
		fbci.pAttachments = attachments.data();
		fbci.width = WIDTH;
		fbci.height = HEIGHT;
		fbci.layers = 1;
		_frameBuffers[i] = vulkan.device.createFramebuffer(fbci);
	}

	return _frameBuffers;
}

std::vector<vk::Framebuffer> Context::createCompositionFrameBuffers()
{
	std::vector<vk::Framebuffer> _frameBuffers(vulkan.swapchain->images.size());

	for (size_t i = 0; i < _frameBuffers.size(); ++i) {
		std::vector<vk::ImageView> attachments = {
			vulkan.swapchain->images[i].view,
			renderTargets["composition"].view
		};
		vk::FramebufferCreateInfo fbci;
		fbci.renderPass = deferred.compositionRenderPass;
		fbci.attachmentCount = static_cast<uint32_t>(attachments.size());
		fbci.pAttachments = attachments.data();
		fbci.width = WIDTH;
		fbci.height = HEIGHT;
		fbci.layers = 1;
		_frameBuffers[i] = vulkan.device.createFramebuffer(fbci);
	}

	return _frameBuffers;
}

std::vector<vk::Framebuffer> Context::createSSRFrameBuffers()
{
	std::vector<vk::Framebuffer> _frameBuffers(vulkan.swapchain->images.size());

	for (size_t i = 0; i < _frameBuffers.size(); ++i) {
		std::vector<vk::ImageView> attachments = {
			renderTargets["ssr"].view
		};
		vk::FramebufferCreateInfo fbci;
		fbci.renderPass = ssr.renderPass;
		fbci.attachmentCount = static_cast<uint32_t>(attachments.size());
		fbci.pAttachments = attachments.data();
		fbci.width = WIDTH;
		fbci.height = HEIGHT;
		fbci.layers = 1;
		_frameBuffers[i] = vulkan.device.createFramebuffer(fbci);
	}

	return _frameBuffers;
}

std::vector<vk::Framebuffer> Context::createSSAOFrameBuffers()
{
	std::vector<vk::Framebuffer> _frameBuffers(vulkan.swapchain->images.size());

	for (size_t i = 0; i < _frameBuffers.size(); ++i) {
		std::vector<vk::ImageView> attachments = {
			renderTargets["ssao"].view
		};
		vk::FramebufferCreateInfo fbci;
		fbci.renderPass = ssao.renderPass;
		fbci.attachmentCount = static_cast<uint32_t>(attachments.size());
		fbci.pAttachments = attachments.data();
		fbci.width = WIDTH;
		fbci.height = HEIGHT;
		fbci.layers = 1;
		_frameBuffers[i] = vulkan.device.createFramebuffer(fbci);
	}

	return _frameBuffers;
}

std::vector<vk::Framebuffer> Context::createSSAOBlurFrameBuffers()
{
	std::vector<vk::Framebuffer> _frameBuffers(vulkan.swapchain->images.size());

	for (size_t i = 0; i < _frameBuffers.size(); ++i) {
		std::vector<vk::ImageView> attachments = {
			renderTargets["ssaoBlur"].view
		};
		vk::FramebufferCreateInfo fbci;
		fbci.renderPass = ssao.blurRenderPass;
		fbci.attachmentCount = static_cast<uint32_t>(attachments.size());
		fbci.pAttachments = attachments.data();
		fbci.width = WIDTH;
		fbci.height = HEIGHT;
		fbci.layers = 1;
		_frameBuffers[i] = vulkan.device.createFramebuffer(fbci);
	}

	return _frameBuffers;
}

std::vector<vk::Framebuffer> vm::Context::createFXAAFrameBuffers()
{
	std::vector<vk::Framebuffer> _frameBuffers(vulkan.swapchain->images.size());

	for (size_t i = 0; i < _frameBuffers.size(); ++i) {
		std::vector<vk::ImageView> attachments = {
			vulkan.swapchain->images[i].view,
			renderTargets["composition2"].view
		};
		vk::FramebufferCreateInfo fbci;
		fbci.renderPass = fxaa.renderPass;
		fbci.attachmentCount = static_cast<uint32_t>(attachments.size());
		fbci.pAttachments = attachments.data();
		fbci.width = WIDTH;
		fbci.height = HEIGHT;
		fbci.layers = 1;
		_frameBuffers[i] = vulkan.device.createFramebuffer(fbci);
	}

	return _frameBuffers;
}

std::vector<vk::Framebuffer> vm::Context::createBloomFrameBuffers()
{
	std::vector<vk::Framebuffer> _frameBuffers(vulkan.swapchain->images.size() * 4);

	for (size_t i = 0; i < vulkan.swapchain->images.size(); ++i) {
		std::vector<vk::ImageView> attachments = {
			renderTargets["brightFilter"].view
		};
		vk::FramebufferCreateInfo fbci;
		fbci.renderPass = bloom.renderPassBrightFilter;
		fbci.attachmentCount = static_cast<uint32_t>(attachments.size());
		fbci.pAttachments = attachments.data();
		fbci.width = WIDTH;
		fbci.height = HEIGHT;
		fbci.layers = 1;
		_frameBuffers[i] = vulkan.device.createFramebuffer(fbci);
	}

	for (size_t i = vulkan.swapchain->images.size(); i < vulkan.swapchain->images.size() * 2; ++i) {
		std::vector<vk::ImageView> attachments = {
			renderTargets["gaussianBlurHorizontal"].view
		};
		vk::FramebufferCreateInfo fbci;
		fbci.renderPass = bloom.renderPassGaussianBlur;
		fbci.attachmentCount = static_cast<uint32_t>(attachments.size());
		fbci.pAttachments = attachments.data();
		fbci.width = WIDTH;
		fbci.height = HEIGHT;
		fbci.layers = 1;
		_frameBuffers[i] = vulkan.device.createFramebuffer(fbci);
	}

	for (size_t i = vulkan.swapchain->images.size() * 2; i < vulkan.swapchain->images.size() * 3; ++i) {
		std::vector<vk::ImageView> attachments = {
			renderTargets["gaussianBlurVertical"].view
		};
		vk::FramebufferCreateInfo fbci;
		fbci.renderPass = bloom.renderPassGaussianBlur;
		fbci.attachmentCount = static_cast<uint32_t>(attachments.size());
		fbci.pAttachments = attachments.data();
		fbci.width = WIDTH;
		fbci.height = HEIGHT;
		fbci.layers = 1;
		_frameBuffers[i] = vulkan.device.createFramebuffer(fbci);
	}

	for (size_t i = vulkan.swapchain->images.size() * 3; i < vulkan.swapchain->images.size() * 4; ++i) {
		std::vector<vk::ImageView> attachments = {
			vulkan.swapchain->images[i - vulkan.swapchain->images.size() * 3].view,
			GUI::show_FXAA ? renderTargets["composition"].view : renderTargets["composition2"].view
		};
		vk::FramebufferCreateInfo fbci;
		fbci.renderPass = bloom.renderPassCombine;
		fbci.attachmentCount = static_cast<uint32_t>(attachments.size());
		fbci.pAttachments = attachments.data();
		fbci.width = WIDTH;
		fbci.height = HEIGHT;
		fbci.layers = 1;
		_frameBuffers[i] = vulkan.device.createFramebuffer(fbci);
	}

	return _frameBuffers;
}

std::vector<vk::Framebuffer> Context::createMotionBlurFrameBuffers()
{
	std::vector<vk::Framebuffer> _frameBuffers(vulkan.swapchain->images.size());

	for (size_t i = 0; i < _frameBuffers.size(); ++i) {
		std::vector<vk::ImageView> attachments = {
			vulkan.swapchain->images[i].view
		};
		vk::FramebufferCreateInfo fbci;
		fbci.renderPass = motionBlur.renderPass;
		fbci.attachmentCount = static_cast<uint32_t>(attachments.size());
		fbci.pAttachments = attachments.data();
		fbci.width = WIDTH;
		fbci.height = HEIGHT;
		fbci.layers = 1;
		_frameBuffers[i] = vulkan.device.createFramebuffer(fbci);
	}

	return _frameBuffers;
}

std::vector<vk::Framebuffer> Context::createGUIFrameBuffers()
{
	std::vector<vk::Framebuffer> _frameBuffers(vulkan.swapchain->images.size());

	for (size_t i = 0; i < _frameBuffers.size(); ++i) {
		std::vector<vk::ImageView> attachments = {
			vulkan.swapchain->images[i].view
		};
		vk::FramebufferCreateInfo fbci;
		fbci.renderPass = gui.renderPass;
		fbci.attachmentCount = static_cast<uint32_t>(attachments.size());
		fbci.pAttachments = attachments.data();
		fbci.width = WIDTH;
		fbci.height = HEIGHT;
		fbci.layers = 1;
		_frameBuffers[i] = vulkan.device.createFramebuffer(fbci);
	}

	return _frameBuffers;
}

std::vector<vk::Framebuffer> Context::createShadowsFrameBuffers()
{
	shadows.textures.resize(3);
	for (uint32_t i = 0; i < shadows.textures.size(); i++)
	{
		shadows.textures[i].format				= vulkan.depth->format;
		shadows.textures[i].initialLayout		= vk::ImageLayout::eUndefined;
		shadows.textures[i].addressMode			= vk::SamplerAddressMode::eClampToEdge;
		shadows.textures[i].maxAnisotropy		= 1.f;
		shadows.textures[i].borderColor			= vk::BorderColor::eFloatOpaqueWhite;
		shadows.textures[i].samplerCompareEnable = VK_TRUE;
		shadows.textures[i].compareOp			= vk::CompareOp::eGreaterOrEqual;
		shadows.textures[i].samplerMipmapMode	= vk::SamplerMipmapMode::eLinear;

		shadows.textures[i].createImage(Shadows::imageSize, Shadows::imageSize, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled, vk::MemoryPropertyFlagBits::eDeviceLocal);
		shadows.textures[i].transitionImageLayout(vk::ImageLayout::eUndefined, vk::ImageLayout::eDepthStencilAttachmentOptimal);
		shadows.textures[i].createImageView(vk::ImageAspectFlagBits::eDepth);
		shadows.textures[i].createSampler();
	}

	std::vector<vk::Framebuffer> _frameBuffers(vulkan.swapchain->images.size() * shadows.textures.size());
	for (uint32_t i = 0; i < _frameBuffers.size(); ++i) {
		std::vector<vk::ImageView> attachments = {
			shadows.textures[i / vulkan.swapchain->images.size()].view // e.g. framebuffer[0,1,2,3,4,5] -> texture[0,0,1,1,2,2].view
		};

		vk::FramebufferCreateInfo fbci;
		fbci.renderPass			= shadows.renderPass;
		fbci.attachmentCount	= static_cast<uint32_t>(attachments.size());
		fbci.pAttachments		= attachments.data();
		fbci.width				= Shadows::imageSize;
		fbci.height				= Shadows::imageSize;
		fbci.layers				= 1;

		_frameBuffers[i] = vulkan.device.createFramebuffer(fbci);
	}

	return _frameBuffers;
}

std::vector<vk::Framebuffer> vm::Context::createSkyboxFrameBuffers(SkyBox& skybox)
{
	std::vector<vk::Framebuffer> _frameBuffers(vulkan.swapchain->images.size());

	for (size_t i = 0; i < _frameBuffers.size(); ++i) {
		std::vector<vk::ImageView> attachments = {
			vulkan.swapchain->images[i].view //renderTargets["albedo"].view
		};
		vk::FramebufferCreateInfo fbci;
		fbci.renderPass = skybox.renderPass;
		fbci.attachmentCount = static_cast<uint32_t>(attachments.size());
		fbci.pAttachments = attachments.data();
		fbci.width = WIDTH;
		fbci.height = HEIGHT;
		fbci.layers = 1;
		_frameBuffers[i] = vulkan.device.createFramebuffer(fbci);
	}

	return _frameBuffers;
}

std::vector<vk::CommandBuffer> Context::createCmdBuffers(const uint32_t bufferCount)
{
	vk::CommandBufferAllocateInfo cbai;
	cbai.commandPool = vulkan.commandPool;
	cbai.level = vk::CommandBufferLevel::ePrimary;
	cbai.commandBufferCount = bufferCount;

	return vulkan.device.allocateCommandBuffers(cbai);
}

std::vector<char> readFile(const std::string& filename)
{
	std::ifstream file(filename, std::ios::ate | std::ios::binary);
	if (!file.is_open()) {
		throw std::runtime_error("failed to open file!");
	}
	size_t fileSize = (size_t)file.tellg();
	std::vector<char> buffer(fileSize);
	file.seekg(0);
	file.read(buffer.data(), fileSize);
	file.close();

	return buffer;
}

Pipeline Context::createPipeline(const PipelineInfo& specificInfo)
{
	Pipeline _pipeline;

	// Shader stages
	vk::ShaderModule vertModule;
	vk::ShaderModule fragModule;

	std::vector<char> vertCode{};
	std::vector<char> fragCode{};

	vk::ShaderModuleCreateInfo vsmci;
	vk::ShaderModuleCreateInfo fsmci;

	if (specificInfo.shaders.size() > 0) {
		vertCode = readFile(specificInfo.shaders[0]);
		if (specificInfo.shaders.size() > 1)
			fragCode = readFile(specificInfo.shaders[1]);

		vsmci.codeSize = vertCode.size();
		vsmci.pCode = reinterpret_cast<const uint32_t*>(vertCode.data());

		if (specificInfo.shaders.size() > 1) {
			fsmci.codeSize = fragCode.size();
			fsmci.pCode = reinterpret_cast<const uint32_t*>(fragCode.data());
		}
		vertModule = vulkan.device.createShaderModule(vsmci);
		if (specificInfo.shaders.size() > 1) {
			fragModule = vulkan.device.createShaderModule(fsmci);
		}
	}
	else
		throw std::runtime_error("could not find shader info for PipelineShaderStageCreateInfo");

	_pipeline.pipeinfo.stageCount = (uint32_t)specificInfo.shaders.size();

	std::vector<vk::PipelineShaderStageCreateInfo> stages((uint32_t)specificInfo.shaders.size());
	stages[0].flags = vk::PipelineShaderStageCreateFlags();
	stages[0].stage = vk::ShaderStageFlagBits::eVertex;
	stages[0].module = vertModule;
	stages[0].pName = "main";
	stages[0].pSpecializationInfo = nullptr;
	if (specificInfo.shaders.size() > 1) {
		stages[1].flags = vk::PipelineShaderStageCreateFlags();
		stages[1].stage = vk::ShaderStageFlagBits::eFragment;
		stages[1].module = fragModule;
		stages[1].pName = "main";
		stages[1].pSpecializationInfo = &specificInfo.specializationInfo;
	}
	_pipeline.pipeinfo.pStages = stages.data();

	// Vertex Input state
	vk::PipelineVertexInputStateCreateInfo pvisci;
	pvisci.vertexBindingDescriptionCount = (uint32_t)specificInfo.vertexInputBindingDescriptions.size();
	pvisci.pVertexBindingDescriptions = specificInfo.vertexInputBindingDescriptions.data();
	pvisci.vertexAttributeDescriptionCount = (uint32_t)specificInfo.vertexInputAttributeDescriptions.size();
	pvisci.pVertexAttributeDescriptions = specificInfo.vertexInputAttributeDescriptions.data();
	_pipeline.pipeinfo.pVertexInputState = &pvisci;

	// Input Assembly stage
	vk::PipelineInputAssemblyStateCreateInfo piasci;
	piasci.topology = vk::PrimitiveTopology::eTriangleList;
	piasci.primitiveRestartEnable = VK_FALSE;
	_pipeline.pipeinfo.pInputAssemblyState = &piasci;

	// Viewports and Scissors
	vk::Viewport vp;
	vp.x = 0.0f;
	vp.y = 0.0f;
	vp.width = (float)specificInfo.viewportSize.width;
	vp.height = (float)specificInfo.viewportSize.height;
	vp.minDepth = specificInfo.depth.x;
	vp.maxDepth = specificInfo.depth.y;

	vk::Rect2D r2d;
	r2d.extent = specificInfo.viewportSize;

	vk::PipelineViewportStateCreateInfo pvsci;
	pvsci.viewportCount = 1;
	pvsci.pViewports = &vp;
	pvsci.scissorCount = 1;
	pvsci.pScissors = &r2d;
	_pipeline.pipeinfo.pViewportState = &pvsci;

	// Rasterization state
	vk::PipelineRasterizationStateCreateInfo prsci;
	prsci.depthClampEnable = VK_FALSE;
	prsci.rasterizerDiscardEnable = VK_FALSE;
	prsci.polygonMode = vk::PolygonMode::eFill;
	prsci.cullMode = specificInfo.cull;
	prsci.frontFace = specificInfo.face;
	prsci.depthBiasEnable = specificInfo.depthBiasEnable;
	prsci.depthBiasConstantFactor = 0.0f;
	prsci.depthBiasClamp = 0.0f;
	prsci.depthBiasSlopeFactor = 0.0f;
	prsci.lineWidth = 1.0f;
	_pipeline.pipeinfo.pRasterizationState = &prsci;

	// Multisample state
	vk::PipelineMultisampleStateCreateInfo pmsci;
	pmsci.rasterizationSamples = specificInfo.sampleCount;
	pmsci.sampleShadingEnable = VK_FALSE;
	pmsci.minSampleShading = 1.0f;
	pmsci.pSampleMask = nullptr;
	pmsci.alphaToCoverageEnable = VK_FALSE;
	pmsci.alphaToOneEnable = VK_FALSE;
	_pipeline.pipeinfo.pMultisampleState = &pmsci;

	// Depth stencil state
	vk::PipelineDepthStencilStateCreateInfo pdssci;
	pdssci.depthTestEnable = VK_TRUE;
	pdssci.depthWriteEnable = VK_TRUE;
	pdssci.depthCompareOp = specificInfo.compareOp;
	pdssci.depthBoundsTestEnable = VK_FALSE;
	pdssci.stencilTestEnable = VK_FALSE;
	pdssci.front.compareOp = vk::CompareOp::eAlways;
	pdssci.back.compareOp = vk::CompareOp::eAlways;
	pdssci.minDepthBounds = 0.0f;
	pdssci.maxDepthBounds = 0.0f;
	_pipeline.pipeinfo.pDepthStencilState = &pdssci;

	// Color Blending state
	if (specificInfo.useBlendState) {
		vk::PipelineColorBlendStateCreateInfo pcbsci;
		pcbsci.logicOpEnable = VK_FALSE;
		pcbsci.logicOp = vk::LogicOp::eCopy;
		pcbsci.attachmentCount = (uint32_t)specificInfo.blendAttachmentStates.size();
		pcbsci.pAttachments = specificInfo.blendAttachmentStates.data();
		float blendConstants[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		memcpy(pcbsci.blendConstants, blendConstants, 4 * sizeof(float));
		_pipeline.pipeinfo.pColorBlendState = &pcbsci;
	}
	else
		_pipeline.pipeinfo.pColorBlendState = nullptr;

	// Dynamic state
	_pipeline.pipeinfo.pDynamicState = specificInfo.dynamicStates.size() > 0 ? &specificInfo.dynamicStateInfo : nullptr;

	// Push Constant Range
	auto pushConstantRange = specificInfo.pushConstantRange;
	uint32_t numOfConsts = pushConstantRange == vk::PushConstantRange() ? 0 : 1;

	// Pipeline Layout
	_pipeline.pipeinfo.layout = vulkan.device.createPipelineLayout(
		vk::PipelineLayoutCreateInfo{ vk::PipelineLayoutCreateFlags(), (uint32_t)specificInfo.descriptorSetLayouts.size(), specificInfo.descriptorSetLayouts.data(), numOfConsts, &pushConstantRange });

	// Render Pass
	_pipeline.pipeinfo.renderPass = specificInfo.renderPass;

	// Subpass
	_pipeline.pipeinfo.subpass = specificInfo.subpassTarget;

	// Base Pipeline Handle
	_pipeline.pipeinfo.basePipelineHandle = nullptr;

	// Base Pipeline Index
	_pipeline.pipeinfo.basePipelineIndex = -1;

	_pipeline.pipeline = vulkan.device.createGraphicsPipelines(nullptr, _pipeline.pipeinfo).at(0);

	// destroy Shader Modules
	vulkan.device.destroyShaderModule(vertModule);
	vulkan.device.destroyShaderModule(fragModule);

	return _pipeline;
}

Pipeline Context::createCompositionPipeline()
{
	Pipeline _pipeline;

	// Shader stages
	std::vector<char> vertCode = readFile("shaders/Deferred/cvert.spv");
	vk::ShaderModuleCreateInfo vsmci;
	vsmci.codeSize = vertCode.size();
	vsmci.pCode = reinterpret_cast<const uint32_t*>(vertCode.data());
	vk::ShaderModule vertModule = vulkan.device.createShaderModule(vsmci);

	std::vector<char> fragCode = readFile("shaders/Deferred/cfrag.spv");
	vk::ShaderModuleCreateInfo fsmci;
	fsmci.codeSize = fragCode.size();
	fsmci.pCode = reinterpret_cast<const uint32_t*>(fragCode.data());
	vk::ShaderModule fragModule = vulkan.device.createShaderModule(fsmci);

	vk::PipelineShaderStageCreateInfo pssci1;
	pssci1.stage = vk::ShaderStageFlagBits::eVertex;
	pssci1.module = vertModule;
	pssci1.pName = "main";

	auto max_lights = MAX_LIGHTS;
	vk::SpecializationMapEntry sme;
	sme.size = sizeof(max_lights);
	vk::SpecializationInfo si;
	si.mapEntryCount = 1;
	si.pMapEntries = &sme;
	si.dataSize = sizeof(max_lights);
	si.pData = &max_lights;

	vk::PipelineShaderStageCreateInfo pssci2;
	pssci2.stage = vk::ShaderStageFlagBits::eFragment;
	pssci2.module = fragModule;
	pssci2.pName = "main";
	pssci2.pSpecializationInfo = &si;
	std::vector<vk::PipelineShaderStageCreateInfo> stages{ pssci1, pssci2 };
	_pipeline.pipeinfo.stageCount = static_cast<uint32_t>(stages.size());
	_pipeline.pipeinfo.pStages = stages.data();

	// Vertex Input state
	vk::PipelineVertexInputStateCreateInfo pvisci;
	_pipeline.pipeinfo.pVertexInputState = &pvisci;

	// Input Assembly stage
	vk::PipelineInputAssemblyStateCreateInfo piasci;
	piasci.topology = vk::PrimitiveTopology::eTriangleList;
	piasci.primitiveRestartEnable = VK_FALSE;
	_pipeline.pipeinfo.pInputAssemblyState = &piasci;

	// Viewports and Scissors
	vk::Viewport vp;
	vp.x = 0.0f;
	vp.y = 0.0f;
	vp.width = WIDTH_f;
	vp.height = HEIGHT_f;
	vp.minDepth = 0.0f;
	vp.maxDepth = 1.0f;

	vk::Rect2D r2d;
	r2d.extent = vulkan.surface->actualExtent;

	vk::PipelineViewportStateCreateInfo pvsci;
	pvsci.viewportCount = 1;
	pvsci.pViewports = &vp;
	pvsci.scissorCount = 1;
	pvsci.pScissors = &r2d;
	_pipeline.pipeinfo.pViewportState = &pvsci;

	// Rasterization state
	vk::PipelineRasterizationStateCreateInfo prsci;
	prsci.depthClampEnable = VK_FALSE;
	prsci.rasterizerDiscardEnable = VK_FALSE;
	prsci.polygonMode = vk::PolygonMode::eFill;
	prsci.cullMode = vk::CullModeFlagBits::eBack;
	prsci.frontFace = vk::FrontFace::eClockwise;
	prsci.depthBiasEnable = VK_FALSE;
	prsci.depthBiasConstantFactor = 0.0f;
	prsci.depthBiasClamp = 0.0f;
	prsci.depthBiasSlopeFactor = 0.0f;
	prsci.lineWidth = 1.0f;
	_pipeline.pipeinfo.pRasterizationState = &prsci;

	// Multisample state
	vk::PipelineMultisampleStateCreateInfo pmsci;
	pmsci.rasterizationSamples = vk::SampleCountFlagBits::e1;
	pmsci.sampleShadingEnable = VK_FALSE;
	pmsci.minSampleShading = 1.0f;
	pmsci.pSampleMask = nullptr;
	pmsci.alphaToCoverageEnable = VK_FALSE;
	pmsci.alphaToOneEnable = VK_FALSE;
	_pipeline.pipeinfo.pMultisampleState = &pmsci;

	// Depth stencil state
	vk::PipelineDepthStencilStateCreateInfo pdssci;
	pdssci.depthTestEnable = VK_TRUE;
	pdssci.depthWriteEnable = VK_TRUE;
	pdssci.depthCompareOp = vk::CompareOp::eGreater;
	pdssci.depthBoundsTestEnable = VK_FALSE;
	pdssci.stencilTestEnable = VK_FALSE;
	pdssci.front.compareOp = vk::CompareOp::eAlways;
	pdssci.back.compareOp = vk::CompareOp::eAlways;
	pdssci.minDepthBounds = 0.0f;
	pdssci.maxDepthBounds = 0.0f;
	_pipeline.pipeinfo.pDepthStencilState = &pdssci;

	// Color Blending state
	vk::PipelineColorBlendAttachmentState cba;
	cba.blendEnable = VK_TRUE;
	cba.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
	cba.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
	cba.colorBlendOp = vk::BlendOp::eAdd;
	cba.srcAlphaBlendFactor = vk::BlendFactor::eOne;
	cba.dstAlphaBlendFactor = vk::BlendFactor::eZero;
	cba.alphaBlendOp = vk::BlendOp::eAdd;
	cba.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;

	std::vector<vk::PipelineColorBlendAttachmentState> colorBlendAttachments = { cba, cba };

	vk::PipelineColorBlendStateCreateInfo pcbsci;
	pcbsci.logicOpEnable = VK_FALSE;
	pcbsci.logicOp = vk::LogicOp::eCopy;
	pcbsci.attachmentCount = (uint32_t)colorBlendAttachments.size();
	pcbsci.pAttachments = colorBlendAttachments.data();
	float blendConstants[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	memcpy(pcbsci.blendConstants, blendConstants, 4 * sizeof(float));
	_pipeline.pipeinfo.pColorBlendState = &pcbsci;

	// Dynamic state
	_pipeline.pipeinfo.pDynamicState = nullptr;

	// Pipeline Layout
	if (!deferred.DSLayoutComposition)
	{
		auto layoutBinding = [](uint32_t binding, vk::DescriptorType descriptorType) {
			return vk::DescriptorSetLayoutBinding{ binding, descriptorType, 1, vk::ShaderStageFlagBits::eFragment, nullptr };
		};
		std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings {
			layoutBinding(0, vk::DescriptorType::eCombinedImageSampler),
			layoutBinding(1, vk::DescriptorType::eCombinedImageSampler),
			layoutBinding(2, vk::DescriptorType::eCombinedImageSampler),
			layoutBinding(3, vk::DescriptorType::eCombinedImageSampler),
			layoutBinding(4, vk::DescriptorType::eUniformBuffer),
			layoutBinding(5, vk::DescriptorType::eCombinedImageSampler),
			layoutBinding(6, vk::DescriptorType::eCombinedImageSampler),
			layoutBinding(7, vk::DescriptorType::eCombinedImageSampler),
		};
		vk::DescriptorSetLayoutCreateInfo descriptorLayout;
		descriptorLayout.bindingCount = (uint32_t)setLayoutBindings.size();
		descriptorLayout.pBindings = setLayoutBindings.data();
		deferred.DSLayoutComposition  = vulkan.device.createDescriptorSetLayout(descriptorLayout);
	}

	std::vector<vk::DescriptorSetLayout> descriptorSetLayouts = {
		deferred.DSLayoutComposition,
		Shadows::getDescriptorSetLayout(vulkan.device),
		Shadows::getDescriptorSetLayout(vulkan.device),
		Shadows::getDescriptorSetLayout(vulkan.device),
		SkyBox::getDescriptorSetLayout(vulkan.device) };

	vk::PushConstantRange pConstants;
	pConstants.stageFlags = vk::ShaderStageFlagBits::eFragment;
	pConstants.offset = 0;
	pConstants.size = 7 * sizeof(vec4);
	
	vk::PipelineLayoutCreateInfo plci;
	plci.setLayoutCount = (uint32_t)descriptorSetLayouts.size();
	plci.pSetLayouts = descriptorSetLayouts.data();
	plci.pushConstantRangeCount = 1;
	plci.pPushConstantRanges = &pConstants;
	_pipeline.pipeinfo.layout = vulkan.device.createPipelineLayout(plci);

	// Render Pass
	_pipeline.pipeinfo.renderPass = deferred.compositionRenderPass;

	// Subpass (Index of subpass this pipeline will be used in)
	_pipeline.pipeinfo.subpass = 0;

	// Base Pipeline Handle
	_pipeline.pipeinfo.basePipelineHandle = nullptr;

	// Base Pipeline Index
	_pipeline.pipeinfo.basePipelineIndex = -1;

	_pipeline.pipeline = vulkan.device.createGraphicsPipelines(nullptr, _pipeline.pipeinfo).at(0);

	// destroy Shader Modules
	vulkan.device.destroyShaderModule(vertModule);
	vulkan.device.destroyShaderModule(fragModule);

	return _pipeline;
}

Pipeline Context::createSSRPipeline()
{
	Pipeline _pipeline;

	// Shader stages
	std::vector<char> vertCode = readFile("shaders/Common/vert.spv");
	vk::ShaderModuleCreateInfo vsmci;
	vsmci.codeSize = vertCode.size();
	vsmci.pCode = reinterpret_cast<const uint32_t*>(vertCode.data());
	vk::ShaderModule vertModule = vulkan.device.createShaderModule(vsmci);

	std::vector<char> fragCode = readFile("shaders/SSR/frag.spv");
	vk::ShaderModuleCreateInfo fsmci;
	fsmci.codeSize = fragCode.size();
	fsmci.pCode = reinterpret_cast<const uint32_t*>(fragCode.data());
	vk::ShaderModule fragModule = vulkan.device.createShaderModule(fsmci);

	vk::PipelineShaderStageCreateInfo pssci1;
	pssci1.stage = vk::ShaderStageFlagBits::eVertex;
	pssci1.module = vertModule;
	pssci1.pName = "main";

	vk::PipelineShaderStageCreateInfo pssci2;
	pssci2.stage = vk::ShaderStageFlagBits::eFragment;
	pssci2.module = fragModule;
	pssci2.pName = "main";

	std::vector<vk::PipelineShaderStageCreateInfo> stages{ pssci1, pssci2 };
	_pipeline.pipeinfo.stageCount = static_cast<uint32_t>(stages.size());
	_pipeline.pipeinfo.pStages = stages.data();

	// Vertex Input state
	vk::PipelineVertexInputStateCreateInfo pvisci;
	_pipeline.pipeinfo.pVertexInputState = &pvisci;

	// Input Assembly stage
	vk::PipelineInputAssemblyStateCreateInfo piasci;
	piasci.topology = vk::PrimitiveTopology::eTriangleList;
	piasci.primitiveRestartEnable = VK_FALSE;
	_pipeline.pipeinfo.pInputAssemblyState = &piasci;

	// Viewports and Scissors
	vk::Viewport vp;
	vp.x = 0.0f;
	vp.y = 0.0f;
	vp.width = WIDTH_f;
	vp.height = HEIGHT_f;
	vp.minDepth = 0.0f;
	vp.maxDepth = 1.0f;

	vk::Rect2D r2d;
	r2d.extent = vulkan.surface->actualExtent;

	vk::PipelineViewportStateCreateInfo pvsci;
	pvsci.viewportCount = 1;
	pvsci.pViewports = &vp;
	pvsci.scissorCount = 1;
	pvsci.pScissors = &r2d;
	_pipeline.pipeinfo.pViewportState = &pvsci;

	// Rasterization state
	vk::PipelineRasterizationStateCreateInfo prsci;
	prsci.depthClampEnable = VK_FALSE;
	prsci.rasterizerDiscardEnable = VK_FALSE;
	prsci.polygonMode = vk::PolygonMode::eFill;
	prsci.cullMode = vk::CullModeFlagBits::eBack;
	prsci.frontFace = vk::FrontFace::eClockwise;
	prsci.depthBiasEnable = VK_FALSE;
	prsci.depthBiasConstantFactor = 0.0f;
	prsci.depthBiasClamp = 0.0f;
	prsci.depthBiasSlopeFactor = 0.0f;
	prsci.lineWidth = 1.0f;
	_pipeline.pipeinfo.pRasterizationState = &prsci;

	// Multisample state
	vk::PipelineMultisampleStateCreateInfo pmsci;
	pmsci.rasterizationSamples = vk::SampleCountFlagBits::e1;
	pmsci.sampleShadingEnable = VK_FALSE;
	pmsci.minSampleShading = 1.0f;
	pmsci.pSampleMask = nullptr;
	pmsci.alphaToCoverageEnable = VK_FALSE;
	pmsci.alphaToOneEnable = VK_FALSE;
	_pipeline.pipeinfo.pMultisampleState = &pmsci;

	// Depth stencil state
	vk::PipelineDepthStencilStateCreateInfo pdssci;
	pdssci.depthTestEnable = VK_TRUE;
	pdssci.depthWriteEnable = VK_TRUE;
	pdssci.depthCompareOp = vk::CompareOp::eGreater;
	pdssci.depthBoundsTestEnable = VK_FALSE;
	pdssci.stencilTestEnable = VK_FALSE;
	pdssci.front.compareOp = vk::CompareOp::eAlways;
	pdssci.back.compareOp = vk::CompareOp::eAlways;
	pdssci.minDepthBounds = 0.0f;
	pdssci.maxDepthBounds = 0.0f;
	_pipeline.pipeinfo.pDepthStencilState = &pdssci;

	// Color Blending state
	vk::PipelineColorBlendAttachmentState cba;
	cba.blendEnable = VK_TRUE;
	cba.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
	cba.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
	cba.colorBlendOp = vk::BlendOp::eAdd;
	cba.srcAlphaBlendFactor = vk::BlendFactor::eOne;
	cba.dstAlphaBlendFactor = vk::BlendFactor::eZero;
	cba.alphaBlendOp = vk::BlendOp::eAdd;
	cba.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;

	std::vector<vk::PipelineColorBlendAttachmentState> colorBlendAttachments = { cba };

	vk::PipelineColorBlendStateCreateInfo pcbsci;
	pcbsci.logicOpEnable = VK_FALSE;
	pcbsci.logicOp = vk::LogicOp::eCopy;
	pcbsci.attachmentCount = (uint32_t)colorBlendAttachments.size();
	pcbsci.pAttachments = colorBlendAttachments.data();
	float blendConstants[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	memcpy(pcbsci.blendConstants, blendConstants, 4 * sizeof(float));
	_pipeline.pipeinfo.pColorBlendState = &pcbsci;

	// Dynamic state
	_pipeline.pipeinfo.pDynamicState = nullptr;

	// Pipeline Layout
	if (!ssr.DSLayoutReflection)
	{
		auto layoutBinding = [](uint32_t binding, vk::DescriptorType descriptorType) {
			return vk::DescriptorSetLayoutBinding{ binding, descriptorType, 1, vk::ShaderStageFlagBits::eFragment, nullptr };
		};
		std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings{
			layoutBinding(0, vk::DescriptorType::eCombinedImageSampler),
			layoutBinding(1, vk::DescriptorType::eCombinedImageSampler),
			layoutBinding(2, vk::DescriptorType::eCombinedImageSampler),
			layoutBinding(3, vk::DescriptorType::eCombinedImageSampler),
			layoutBinding(4, vk::DescriptorType::eUniformBuffer),
		};
		vk::DescriptorSetLayoutCreateInfo descriptorLayout;
		descriptorLayout.bindingCount = (uint32_t)setLayoutBindings.size();
		descriptorLayout.pBindings = setLayoutBindings.data();
		ssr.DSLayoutReflection = vulkan.device.createDescriptorSetLayout(descriptorLayout);
	}

	std::vector<vk::DescriptorSetLayout> descriptorSetLayouts = { ssr.DSLayoutReflection };

	vk::PushConstantRange pConstants;
	pConstants.stageFlags = vk::ShaderStageFlagBits::eFragment;
	pConstants.offset = 0;
	pConstants.size = 4 * sizeof(vec4);
	
	vk::PipelineLayoutCreateInfo plci;
	plci.setLayoutCount = (uint32_t)descriptorSetLayouts.size();
	plci.pSetLayouts = descriptorSetLayouts.data();
	plci.pushConstantRangeCount = 1;
	plci.pPushConstantRanges = &pConstants;
	_pipeline.pipeinfo.layout = vulkan.device.createPipelineLayout(plci);

	// Render Pass
	_pipeline.pipeinfo.renderPass = ssr.renderPass;

	// Subpass (Index of subpass this pipeline will be used in)
	_pipeline.pipeinfo.subpass = 0;

	// Base Pipeline Handle
	_pipeline.pipeinfo.basePipelineHandle = nullptr;

	// Base Pipeline Index
	_pipeline.pipeinfo.basePipelineIndex = -1;

	_pipeline.pipeline = vulkan.device.createGraphicsPipelines(nullptr, _pipeline.pipeinfo).at(0);

	// destroy Shader Modules
	vulkan.device.destroyShaderModule(vertModule);
	vulkan.device.destroyShaderModule(fragModule);

	return _pipeline;
}

Pipeline vm::Context::createFXAAPipeline()
{
	Pipeline _pipeline;

	// Shader stages
	std::vector<char> vertCode = readFile("shaders/Common/vert.spv");
	vk::ShaderModuleCreateInfo vsmci;
	vsmci.codeSize = vertCode.size();
	vsmci.pCode = reinterpret_cast<const uint32_t*>(vertCode.data());
	vk::ShaderModule vertModule = vulkan.device.createShaderModule(vsmci);

	std::vector<char> fragCode = readFile("shaders/FXAA/frag.spv");
	vk::ShaderModuleCreateInfo fsmci;
	fsmci.codeSize = fragCode.size();
	fsmci.pCode = reinterpret_cast<const uint32_t*>(fragCode.data());
	vk::ShaderModule fragModule = vulkan.device.createShaderModule(fsmci);

	vk::PipelineShaderStageCreateInfo pssci1;
	pssci1.stage = vk::ShaderStageFlagBits::eVertex;
	pssci1.module = vertModule;
	pssci1.pName = "main";

	vk::PipelineShaderStageCreateInfo pssci2;
	pssci2.stage = vk::ShaderStageFlagBits::eFragment;
	pssci2.module = fragModule;
	pssci2.pName = "main";

	std::vector<vk::PipelineShaderStageCreateInfo> stages{ pssci1, pssci2 };
	_pipeline.pipeinfo.stageCount = static_cast<uint32_t>(stages.size());
	_pipeline.pipeinfo.pStages = stages.data();

	// Vertex Input state
	vk::PipelineVertexInputStateCreateInfo pvisci;
	_pipeline.pipeinfo.pVertexInputState = &pvisci;

	// Input Assembly stage
	vk::PipelineInputAssemblyStateCreateInfo piasci;
	piasci.topology = vk::PrimitiveTopology::eTriangleList;
	piasci.primitiveRestartEnable = VK_FALSE;
	_pipeline.pipeinfo.pInputAssemblyState = &piasci;

	// Viewports and Scissors
	vk::Viewport vp;
	vp.x = 0.0f;
	vp.y = 0.0f;
	vp.width = WIDTH_f;
	vp.height = HEIGHT_f;
	vp.minDepth = 0.0f;
	vp.maxDepth = 1.0f;

	vk::Rect2D r2d;
	r2d.extent = vulkan.surface->actualExtent;

	vk::PipelineViewportStateCreateInfo pvsci;
	pvsci.viewportCount = 1;
	pvsci.pViewports = &vp;
	pvsci.scissorCount = 1;
	pvsci.pScissors = &r2d;
	_pipeline.pipeinfo.pViewportState = &pvsci;

	// Rasterization state
	vk::PipelineRasterizationStateCreateInfo prsci;
	prsci.depthClampEnable = VK_FALSE;
	prsci.rasterizerDiscardEnable = VK_FALSE;
	prsci.polygonMode = vk::PolygonMode::eFill;
	prsci.cullMode = vk::CullModeFlagBits::eBack;
	prsci.frontFace = vk::FrontFace::eClockwise;
	prsci.depthBiasEnable = VK_FALSE;
	prsci.depthBiasConstantFactor = 0.0f;
	prsci.depthBiasClamp = 0.0f;
	prsci.depthBiasSlopeFactor = 0.0f;
	prsci.lineWidth = 1.0f;
	_pipeline.pipeinfo.pRasterizationState = &prsci;

	// Multisample state
	vk::PipelineMultisampleStateCreateInfo pmsci;
	pmsci.rasterizationSamples = vk::SampleCountFlagBits::e1;
	pmsci.sampleShadingEnable = VK_FALSE;
	pmsci.minSampleShading = 1.0f;
	pmsci.pSampleMask = nullptr;
	pmsci.alphaToCoverageEnable = VK_FALSE;
	pmsci.alphaToOneEnable = VK_FALSE;
	_pipeline.pipeinfo.pMultisampleState = &pmsci;

	// Depth stencil state
	vk::PipelineDepthStencilStateCreateInfo pdssci;
	pdssci.depthTestEnable = VK_TRUE;
	pdssci.depthWriteEnable = VK_TRUE;
	pdssci.depthCompareOp = vk::CompareOp::eGreater;
	pdssci.depthBoundsTestEnable = VK_FALSE;
	pdssci.stencilTestEnable = VK_FALSE;
	pdssci.front.compareOp = vk::CompareOp::eAlways;
	pdssci.back.compareOp = vk::CompareOp::eAlways;
	pdssci.minDepthBounds = 0.0f;
	pdssci.maxDepthBounds = 0.0f;
	_pipeline.pipeinfo.pDepthStencilState = &pdssci;

	// Color Blending state
	vk::PipelineColorBlendAttachmentState cba;
	cba.blendEnable = VK_TRUE;
	cba.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
	cba.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
	cba.colorBlendOp = vk::BlendOp::eAdd;
	cba.srcAlphaBlendFactor = vk::BlendFactor::eOne;
	cba.dstAlphaBlendFactor = vk::BlendFactor::eZero;
	cba.alphaBlendOp = vk::BlendOp::eAdd;
	cba.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;

	std::vector<vk::PipelineColorBlendAttachmentState> colorBlendAttachments = { cba, cba };

	vk::PipelineColorBlendStateCreateInfo pcbsci;
	pcbsci.logicOpEnable = VK_FALSE;
	pcbsci.logicOp = vk::LogicOp::eCopy;
	pcbsci.attachmentCount = (uint32_t)colorBlendAttachments.size();
	pcbsci.pAttachments = colorBlendAttachments.data();
	float blendConstants[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	memcpy(pcbsci.blendConstants, blendConstants, 4 * sizeof(float));
	_pipeline.pipeinfo.pColorBlendState = &pcbsci;

	// Dynamic state
	_pipeline.pipeinfo.pDynamicState = nullptr;

	// Pipeline Layout
	if (!fxaa.DSLayout)
	{
		auto layoutBinding = [](uint32_t binding, vk::DescriptorType descriptorType) {
			return vk::DescriptorSetLayoutBinding{ binding, descriptorType, 1, vk::ShaderStageFlagBits::eFragment, nullptr };
		};
		std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings{
			layoutBinding(0, vk::DescriptorType::eCombinedImageSampler),
		};
		vk::DescriptorSetLayoutCreateInfo descriptorLayout;
		descriptorLayout.bindingCount = (uint32_t)setLayoutBindings.size();
		descriptorLayout.pBindings = setLayoutBindings.data();
		fxaa.DSLayout = vulkan.device.createDescriptorSetLayout(descriptorLayout);
	}

	std::vector<vk::DescriptorSetLayout> descriptorSetLayouts = { fxaa.DSLayout };

	vk::PipelineLayoutCreateInfo plci;
	plci.setLayoutCount = (uint32_t)descriptorSetLayouts.size();
	plci.pSetLayouts = descriptorSetLayouts.data();
	_pipeline.pipeinfo.layout = vulkan.device.createPipelineLayout(plci);

	// Render Pass
	_pipeline.pipeinfo.renderPass = fxaa.renderPass;

	// Subpass (Index of subpass this pipeline will be used in)
	_pipeline.pipeinfo.subpass = 0;

	// Base Pipeline Handle
	_pipeline.pipeinfo.basePipelineHandle = nullptr;

	// Base Pipeline Index
	_pipeline.pipeinfo.basePipelineIndex = -1;

	_pipeline.pipeline = vulkan.device.createGraphicsPipelines(nullptr, _pipeline.pipeinfo).at(0);

	// destroy Shader Modules
	vulkan.device.destroyShaderModule(vertModule);
	vulkan.device.destroyShaderModule(fragModule);

	return _pipeline;
}

Pipeline vm::Context::createBrightFilterPipeline()
{
	Pipeline _pipeline;

	// Shader stages
	std::vector<char> vertCode = readFile("shaders/Common/vert.spv");
	vk::ShaderModuleCreateInfo vsmci;
	vsmci.codeSize = vertCode.size();
	vsmci.pCode = reinterpret_cast<const uint32_t*>(vertCode.data());
	vk::ShaderModule vertModule = vulkan.device.createShaderModule(vsmci);

	std::vector<char> fragCode = readFile("shaders/Bloom/fragBrightFilter.spv");
	vk::ShaderModuleCreateInfo fsmci;
	fsmci.codeSize = fragCode.size();
	fsmci.pCode = reinterpret_cast<const uint32_t*>(fragCode.data());
	vk::ShaderModule fragModule = vulkan.device.createShaderModule(fsmci);

	vk::PipelineShaderStageCreateInfo pssci1;
	pssci1.stage = vk::ShaderStageFlagBits::eVertex;
	pssci1.module = vertModule;
	pssci1.pName = "main";

	vk::PipelineShaderStageCreateInfo pssci2;
	pssci2.stage = vk::ShaderStageFlagBits::eFragment;
	pssci2.module = fragModule;
	pssci2.pName = "main";

	std::vector<vk::PipelineShaderStageCreateInfo> stages{ pssci1, pssci2 };
	_pipeline.pipeinfo.stageCount = static_cast<uint32_t>(stages.size());
	_pipeline.pipeinfo.pStages = stages.data();

	// Vertex Input state
	vk::PipelineVertexInputStateCreateInfo pvisci;
	_pipeline.pipeinfo.pVertexInputState = &pvisci;

	// Input Assembly stage
	vk::PipelineInputAssemblyStateCreateInfo piasci;
	piasci.topology = vk::PrimitiveTopology::eTriangleList;
	piasci.primitiveRestartEnable = VK_FALSE;
	_pipeline.pipeinfo.pInputAssemblyState = &piasci;

	// Viewports and Scissors
	vk::Viewport vp;
	vp.x = 0.0f;
	vp.y = 0.0f;
	vp.width = WIDTH_f;
	vp.height = HEIGHT_f;
	vp.minDepth = 0.0f;
	vp.maxDepth = 1.0f;

	vk::Rect2D r2d;
	r2d.extent = vulkan.surface->actualExtent;

	vk::PipelineViewportStateCreateInfo pvsci;
	pvsci.viewportCount = 1;
	pvsci.pViewports = &vp;
	pvsci.scissorCount = 1;
	pvsci.pScissors = &r2d;
	_pipeline.pipeinfo.pViewportState = &pvsci;

	// Rasterization state
	vk::PipelineRasterizationStateCreateInfo prsci;
	prsci.depthClampEnable = VK_FALSE;
	prsci.rasterizerDiscardEnable = VK_FALSE;
	prsci.polygonMode = vk::PolygonMode::eFill;
	prsci.cullMode = vk::CullModeFlagBits::eBack;
	prsci.frontFace = vk::FrontFace::eClockwise;
	prsci.depthBiasEnable = VK_FALSE;
	prsci.depthBiasConstantFactor = 0.0f;
	prsci.depthBiasClamp = 0.0f;
	prsci.depthBiasSlopeFactor = 0.0f;
	prsci.lineWidth = 1.0f;
	_pipeline.pipeinfo.pRasterizationState = &prsci;

	// Multisample state
	vk::PipelineMultisampleStateCreateInfo pmsci;
	pmsci.rasterizationSamples = vk::SampleCountFlagBits::e1;
	pmsci.sampleShadingEnable = VK_FALSE;
	pmsci.minSampleShading = 1.0f;
	pmsci.pSampleMask = nullptr;
	pmsci.alphaToCoverageEnable = VK_FALSE;
	pmsci.alphaToOneEnable = VK_FALSE;
	_pipeline.pipeinfo.pMultisampleState = &pmsci;

	// Depth stencil state
	vk::PipelineDepthStencilStateCreateInfo pdssci;
	pdssci.depthTestEnable = VK_TRUE;
	pdssci.depthWriteEnable = VK_TRUE;
	pdssci.depthCompareOp = vk::CompareOp::eGreater;
	pdssci.depthBoundsTestEnable = VK_FALSE;
	pdssci.stencilTestEnable = VK_FALSE;
	pdssci.front.compareOp = vk::CompareOp::eAlways;
	pdssci.back.compareOp = vk::CompareOp::eAlways;
	pdssci.minDepthBounds = 0.0f;
	pdssci.maxDepthBounds = 0.0f;
	_pipeline.pipeinfo.pDepthStencilState = &pdssci;

	// Color Blending state
	vk::PipelineColorBlendAttachmentState cba;
	cba.blendEnable = VK_TRUE;
	cba.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
	cba.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
	cba.colorBlendOp = vk::BlendOp::eAdd;
	cba.srcAlphaBlendFactor = vk::BlendFactor::eOne;
	cba.dstAlphaBlendFactor = vk::BlendFactor::eZero;
	cba.alphaBlendOp = vk::BlendOp::eAdd;
	cba.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;

	std::vector<vk::PipelineColorBlendAttachmentState> colorBlendAttachments = { cba };

	vk::PipelineColorBlendStateCreateInfo pcbsci;
	pcbsci.logicOpEnable = VK_FALSE;
	pcbsci.logicOp = vk::LogicOp::eCopy;
	pcbsci.attachmentCount = (uint32_t)colorBlendAttachments.size();
	pcbsci.pAttachments = colorBlendAttachments.data();
	float blendConstants[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	memcpy(pcbsci.blendConstants, blendConstants, 4 * sizeof(float));
	_pipeline.pipeinfo.pColorBlendState = &pcbsci;

	// Dynamic state
	_pipeline.pipeinfo.pDynamicState = nullptr;

	// Pipeline Layout
	if (!bloom.DSLayoutBrightFilter)
	{
		auto layoutBinding = [](uint32_t binding, vk::DescriptorType descriptorType) {
			return vk::DescriptorSetLayoutBinding{ binding, descriptorType, 1, vk::ShaderStageFlagBits::eFragment, nullptr };
		};
		std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings{
			layoutBinding(0, vk::DescriptorType::eCombinedImageSampler),
		};
		vk::DescriptorSetLayoutCreateInfo descriptorLayout;
		descriptorLayout.bindingCount = (uint32_t)setLayoutBindings.size();
		descriptorLayout.pBindings = setLayoutBindings.data();
		bloom.DSLayoutBrightFilter = vulkan.device.createDescriptorSetLayout(descriptorLayout);
	}

	std::vector<vk::DescriptorSetLayout> descriptorSetLayouts = { bloom.DSLayoutBrightFilter };

	vk::PushConstantRange pConstants;
	pConstants.stageFlags = vk::ShaderStageFlagBits::eFragment;
	pConstants.offset = 0;
	pConstants.size = 5 * sizeof(vec4);

	vk::PipelineLayoutCreateInfo plci;
	plci.setLayoutCount = (uint32_t)descriptorSetLayouts.size();
	plci.pSetLayouts = descriptorSetLayouts.data();
	plci.pushConstantRangeCount = 1;
	plci.pPushConstantRanges = &pConstants;
	_pipeline.pipeinfo.layout = vulkan.device.createPipelineLayout(plci);

	// Render Pass
	_pipeline.pipeinfo.renderPass = bloom.renderPassBrightFilter;

	// Subpass (Index of subpass this pipeline will be used in)
	_pipeline.pipeinfo.subpass = 0;

	// Base Pipeline Handle
	_pipeline.pipeinfo.basePipelineHandle = nullptr;

	// Base Pipeline Index
	_pipeline.pipeinfo.basePipelineIndex = -1;

	_pipeline.pipeline = vulkan.device.createGraphicsPipelines(nullptr, _pipeline.pipeinfo).at(0);

	// destroy Shader Modules
	vulkan.device.destroyShaderModule(vertModule);
	vulkan.device.destroyShaderModule(fragModule);

	return _pipeline;
}

Pipeline vm::Context::createGaussianBlurHorizontalPipeline()
{
	Pipeline _pipeline;

	// Shader stages
	std::vector<char> vertCode = readFile("shaders/Common/vert.spv");
	vk::ShaderModuleCreateInfo vsmci;
	vsmci.codeSize = vertCode.size();
	vsmci.pCode = reinterpret_cast<const uint32_t*>(vertCode.data());
	vk::ShaderModule vertModule = vulkan.device.createShaderModule(vsmci);

	std::vector<char> fragCode = readFile("shaders/Bloom/fragGaussianBlurHorizontal.spv");
	vk::ShaderModuleCreateInfo fsmci;
	fsmci.codeSize = fragCode.size();
	fsmci.pCode = reinterpret_cast<const uint32_t*>(fragCode.data());
	vk::ShaderModule fragModule = vulkan.device.createShaderModule(fsmci);

	vk::PipelineShaderStageCreateInfo pssci1;
	pssci1.stage = vk::ShaderStageFlagBits::eVertex;
	pssci1.module = vertModule;
	pssci1.pName = "main";

	vk::PipelineShaderStageCreateInfo pssci2;
	pssci2.stage = vk::ShaderStageFlagBits::eFragment;
	pssci2.module = fragModule;
	pssci2.pName = "main";

	std::vector<vk::PipelineShaderStageCreateInfo> stages{ pssci1, pssci2 };
	_pipeline.pipeinfo.stageCount = static_cast<uint32_t>(stages.size());
	_pipeline.pipeinfo.pStages = stages.data();

	// Vertex Input state
	vk::PipelineVertexInputStateCreateInfo pvisci;
	_pipeline.pipeinfo.pVertexInputState = &pvisci;

	// Input Assembly stage
	vk::PipelineInputAssemblyStateCreateInfo piasci;
	piasci.topology = vk::PrimitiveTopology::eTriangleList;
	piasci.primitiveRestartEnable = VK_FALSE;
	_pipeline.pipeinfo.pInputAssemblyState = &piasci;

	// Viewports and Scissors
	vk::Viewport vp;
	vp.x = 0.0f;
	vp.y = 0.0f;
	vp.width = WIDTH_f;
	vp.height = HEIGHT_f;
	vp.minDepth = 0.0f;
	vp.maxDepth = 1.0f;

	vk::Rect2D r2d;
	r2d.extent = vulkan.surface->actualExtent;

	vk::PipelineViewportStateCreateInfo pvsci;
	pvsci.viewportCount = 1;
	pvsci.pViewports = &vp;
	pvsci.scissorCount = 1;
	pvsci.pScissors = &r2d;
	_pipeline.pipeinfo.pViewportState = &pvsci;

	// Rasterization state
	vk::PipelineRasterizationStateCreateInfo prsci;
	prsci.depthClampEnable = VK_FALSE;
	prsci.rasterizerDiscardEnable = VK_FALSE;
	prsci.polygonMode = vk::PolygonMode::eFill;
	prsci.cullMode = vk::CullModeFlagBits::eBack;
	prsci.frontFace = vk::FrontFace::eClockwise;
	prsci.depthBiasEnable = VK_FALSE;
	prsci.depthBiasConstantFactor = 0.0f;
	prsci.depthBiasClamp = 0.0f;
	prsci.depthBiasSlopeFactor = 0.0f;
	prsci.lineWidth = 1.0f;
	_pipeline.pipeinfo.pRasterizationState = &prsci;

	// Multisample state
	vk::PipelineMultisampleStateCreateInfo pmsci;
	pmsci.rasterizationSamples = vk::SampleCountFlagBits::e1;
	pmsci.sampleShadingEnable = VK_FALSE;
	pmsci.minSampleShading = 1.0f;
	pmsci.pSampleMask = nullptr;
	pmsci.alphaToCoverageEnable = VK_FALSE;
	pmsci.alphaToOneEnable = VK_FALSE;
	_pipeline.pipeinfo.pMultisampleState = &pmsci;

	// Depth stencil state
	vk::PipelineDepthStencilStateCreateInfo pdssci;
	pdssci.depthTestEnable = VK_TRUE;
	pdssci.depthWriteEnable = VK_TRUE;
	pdssci.depthCompareOp = vk::CompareOp::eGreater;
	pdssci.depthBoundsTestEnable = VK_FALSE;
	pdssci.stencilTestEnable = VK_FALSE;
	pdssci.front.compareOp = vk::CompareOp::eAlways;
	pdssci.back.compareOp = vk::CompareOp::eAlways;
	pdssci.minDepthBounds = 0.0f;
	pdssci.maxDepthBounds = 0.0f;
	_pipeline.pipeinfo.pDepthStencilState = &pdssci;

	// Color Blending state
	vk::PipelineColorBlendAttachmentState cba;
	cba.blendEnable = VK_TRUE;
	cba.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
	cba.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
	cba.colorBlendOp = vk::BlendOp::eAdd;
	cba.srcAlphaBlendFactor = vk::BlendFactor::eOne;
	cba.dstAlphaBlendFactor = vk::BlendFactor::eZero;
	cba.alphaBlendOp = vk::BlendOp::eAdd;
	cba.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;

	std::vector<vk::PipelineColorBlendAttachmentState> colorBlendAttachments = { cba };

	vk::PipelineColorBlendStateCreateInfo pcbsci;
	pcbsci.logicOpEnable = VK_FALSE;
	pcbsci.logicOp = vk::LogicOp::eCopy;
	pcbsci.attachmentCount = (uint32_t)colorBlendAttachments.size();
	pcbsci.pAttachments = colorBlendAttachments.data();
	float blendConstants[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	memcpy(pcbsci.blendConstants, blendConstants, 4 * sizeof(float));
	_pipeline.pipeinfo.pColorBlendState = &pcbsci;

	// Dynamic state
	_pipeline.pipeinfo.pDynamicState = nullptr;

	// Pipeline Layout
	if (!bloom.DSLayoutGaussianBlurHorizontal)
	{
		auto layoutBinding = [](uint32_t binding, vk::DescriptorType descriptorType) {
			return vk::DescriptorSetLayoutBinding{ binding, descriptorType, 1, vk::ShaderStageFlagBits::eFragment, nullptr };
		};
		std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings{
			layoutBinding(0, vk::DescriptorType::eCombinedImageSampler),
		};
		vk::DescriptorSetLayoutCreateInfo descriptorLayout;
		descriptorLayout.bindingCount = (uint32_t)setLayoutBindings.size();
		descriptorLayout.pBindings = setLayoutBindings.data();
		bloom.DSLayoutGaussianBlurHorizontal = vulkan.device.createDescriptorSetLayout(descriptorLayout);
	}

	std::vector<vk::DescriptorSetLayout> descriptorSetLayouts = { bloom.DSLayoutGaussianBlurHorizontal };

	vk::PushConstantRange pConstants;
	pConstants.stageFlags = vk::ShaderStageFlagBits::eFragment;
	pConstants.offset = 0;
	pConstants.size = 5 * sizeof(vec4);

	vk::PipelineLayoutCreateInfo plci;
	plci.setLayoutCount = (uint32_t)descriptorSetLayouts.size();
	plci.pSetLayouts = descriptorSetLayouts.data();
	plci.pushConstantRangeCount = 1;
	plci.pPushConstantRanges = &pConstants;
	_pipeline.pipeinfo.layout = vulkan.device.createPipelineLayout(plci);

	// Render Pass
	_pipeline.pipeinfo.renderPass = bloom.renderPassGaussianBlur;

	// Subpass (Index of subpass this pipeline will be used in)
	_pipeline.pipeinfo.subpass = 0;

	// Base Pipeline Handle
	_pipeline.pipeinfo.basePipelineHandle = nullptr;

	// Base Pipeline Index
	_pipeline.pipeinfo.basePipelineIndex = -1;

	_pipeline.pipeline = vulkan.device.createGraphicsPipelines(nullptr, _pipeline.pipeinfo).at(0);

	// destroy Shader Modules
	vulkan.device.destroyShaderModule(vertModule);
	vulkan.device.destroyShaderModule(fragModule);

	return _pipeline;
}

Pipeline vm::Context::createGaussianBlurVerticalPipeline()
{
	Pipeline _pipeline;

	// Shader stages
	std::vector<char> vertCode = readFile("shaders/Common/vert.spv");
	vk::ShaderModuleCreateInfo vsmci;
	vsmci.codeSize = vertCode.size();
	vsmci.pCode = reinterpret_cast<const uint32_t*>(vertCode.data());
	vk::ShaderModule vertModule = vulkan.device.createShaderModule(vsmci);

	std::vector<char> fragCode = readFile("shaders/Bloom/fragGaussianBlurVertical.spv");
	vk::ShaderModuleCreateInfo fsmci;
	fsmci.codeSize = fragCode.size();
	fsmci.pCode = reinterpret_cast<const uint32_t*>(fragCode.data());
	vk::ShaderModule fragModule = vulkan.device.createShaderModule(fsmci);

	vk::PipelineShaderStageCreateInfo pssci1;
	pssci1.stage = vk::ShaderStageFlagBits::eVertex;
	pssci1.module = vertModule;
	pssci1.pName = "main";

	vk::PipelineShaderStageCreateInfo pssci2;
	pssci2.stage = vk::ShaderStageFlagBits::eFragment;
	pssci2.module = fragModule;
	pssci2.pName = "main";

	std::vector<vk::PipelineShaderStageCreateInfo> stages{ pssci1, pssci2 };
	_pipeline.pipeinfo.stageCount = static_cast<uint32_t>(stages.size());
	_pipeline.pipeinfo.pStages = stages.data();

	// Vertex Input state
	vk::PipelineVertexInputStateCreateInfo pvisci;
	_pipeline.pipeinfo.pVertexInputState = &pvisci;

	// Input Assembly stage
	vk::PipelineInputAssemblyStateCreateInfo piasci;
	piasci.topology = vk::PrimitiveTopology::eTriangleList;
	piasci.primitiveRestartEnable = VK_FALSE;
	_pipeline.pipeinfo.pInputAssemblyState = &piasci;

	// Viewports and Scissors
	vk::Viewport vp;
	vp.x = 0.0f;
	vp.y = 0.0f;
	vp.width = WIDTH_f;
	vp.height = HEIGHT_f;
	vp.minDepth = 0.0f;
	vp.maxDepth = 1.0f;

	vk::Rect2D r2d;
	r2d.extent = vulkan.surface->actualExtent;

	vk::PipelineViewportStateCreateInfo pvsci;
	pvsci.viewportCount = 1;
	pvsci.pViewports = &vp;
	pvsci.scissorCount = 1;
	pvsci.pScissors = &r2d;
	_pipeline.pipeinfo.pViewportState = &pvsci;

	// Rasterization state
	vk::PipelineRasterizationStateCreateInfo prsci;
	prsci.depthClampEnable = VK_FALSE;
	prsci.rasterizerDiscardEnable = VK_FALSE;
	prsci.polygonMode = vk::PolygonMode::eFill;
	prsci.cullMode = vk::CullModeFlagBits::eBack;
	prsci.frontFace = vk::FrontFace::eClockwise;
	prsci.depthBiasEnable = VK_FALSE;
	prsci.depthBiasConstantFactor = 0.0f;
	prsci.depthBiasClamp = 0.0f;
	prsci.depthBiasSlopeFactor = 0.0f;
	prsci.lineWidth = 1.0f;
	_pipeline.pipeinfo.pRasterizationState = &prsci;

	// Multisample state
	vk::PipelineMultisampleStateCreateInfo pmsci;
	pmsci.rasterizationSamples = vk::SampleCountFlagBits::e1;
	pmsci.sampleShadingEnable = VK_FALSE;
	pmsci.minSampleShading = 1.0f;
	pmsci.pSampleMask = nullptr;
	pmsci.alphaToCoverageEnable = VK_FALSE;
	pmsci.alphaToOneEnable = VK_FALSE;
	_pipeline.pipeinfo.pMultisampleState = &pmsci;

	// Depth stencil state
	vk::PipelineDepthStencilStateCreateInfo pdssci;
	pdssci.depthTestEnable = VK_TRUE;
	pdssci.depthWriteEnable = VK_TRUE;
	pdssci.depthCompareOp = vk::CompareOp::eGreater;
	pdssci.depthBoundsTestEnable = VK_FALSE;
	pdssci.stencilTestEnable = VK_FALSE;
	pdssci.front.compareOp = vk::CompareOp::eAlways;
	pdssci.back.compareOp = vk::CompareOp::eAlways;
	pdssci.minDepthBounds = 0.0f;
	pdssci.maxDepthBounds = 0.0f;
	_pipeline.pipeinfo.pDepthStencilState = &pdssci;

	// Color Blending state
	vk::PipelineColorBlendAttachmentState cba;
	cba.blendEnable = VK_TRUE;
	cba.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
	cba.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
	cba.colorBlendOp = vk::BlendOp::eAdd;
	cba.srcAlphaBlendFactor = vk::BlendFactor::eOne;
	cba.dstAlphaBlendFactor = vk::BlendFactor::eZero;
	cba.alphaBlendOp = vk::BlendOp::eAdd;
	cba.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;

	std::vector<vk::PipelineColorBlendAttachmentState> colorBlendAttachments = { cba };

	vk::PipelineColorBlendStateCreateInfo pcbsci;
	pcbsci.logicOpEnable = VK_FALSE;
	pcbsci.logicOp = vk::LogicOp::eCopy;
	pcbsci.attachmentCount = (uint32_t)colorBlendAttachments.size();
	pcbsci.pAttachments = colorBlendAttachments.data();
	float blendConstants[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	memcpy(pcbsci.blendConstants, blendConstants, 4 * sizeof(float));
	_pipeline.pipeinfo.pColorBlendState = &pcbsci;

	// Dynamic state
	_pipeline.pipeinfo.pDynamicState = nullptr;

	// Pipeline Layout
	if (!bloom.DSLayoutGaussianBlurVertical)
	{
		auto layoutBinding = [](uint32_t binding, vk::DescriptorType descriptorType) {
			return vk::DescriptorSetLayoutBinding{ binding, descriptorType, 1, vk::ShaderStageFlagBits::eFragment, nullptr };
		};
		std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings{
			layoutBinding(0, vk::DescriptorType::eCombinedImageSampler),
		};
		vk::DescriptorSetLayoutCreateInfo descriptorLayout;
		descriptorLayout.bindingCount = (uint32_t)setLayoutBindings.size();
		descriptorLayout.pBindings = setLayoutBindings.data();
		bloom.DSLayoutGaussianBlurVertical = vulkan.device.createDescriptorSetLayout(descriptorLayout);
	}

	std::vector<vk::DescriptorSetLayout> descriptorSetLayouts = { bloom.DSLayoutGaussianBlurVertical };
	vk::PushConstantRange pConstants;
	pConstants.stageFlags = vk::ShaderStageFlagBits::eFragment;
	pConstants.offset = 0;
	pConstants.size = 5 * sizeof(vec4);

	vk::PipelineLayoutCreateInfo plci;
	plci.setLayoutCount = (uint32_t)descriptorSetLayouts.size();
	plci.pSetLayouts = descriptorSetLayouts.data();
	plci.pushConstantRangeCount = 1;
	plci.pPushConstantRanges = &pConstants;
	_pipeline.pipeinfo.layout = vulkan.device.createPipelineLayout(plci);

	// Render Pass
	_pipeline.pipeinfo.renderPass = bloom.renderPassGaussianBlur;

	// Subpass (Index of subpass this pipeline will be used in)
	_pipeline.pipeinfo.subpass = 0;

	// Base Pipeline Handle
	_pipeline.pipeinfo.basePipelineHandle = nullptr;

	// Base Pipeline Index
	_pipeline.pipeinfo.basePipelineIndex = -1;

	_pipeline.pipeline = vulkan.device.createGraphicsPipelines(nullptr, _pipeline.pipeinfo).at(0);

	// destroy Shader Modules
	vulkan.device.destroyShaderModule(vertModule);
	vulkan.device.destroyShaderModule(fragModule);

	return _pipeline;
}

Pipeline vm::Context::createCombinePipeline()
{
	Pipeline _pipeline;

	// Shader stages
	std::vector<char> vertCode = readFile("shaders/Common/vert.spv");
	vk::ShaderModuleCreateInfo vsmci;
	vsmci.codeSize = vertCode.size();
	vsmci.pCode = reinterpret_cast<const uint32_t*>(vertCode.data());
	vk::ShaderModule vertModule = vulkan.device.createShaderModule(vsmci);

	std::vector<char> fragCode = readFile("shaders/Bloom/fragCombine.spv");
	vk::ShaderModuleCreateInfo fsmci;
	fsmci.codeSize = fragCode.size();
	fsmci.pCode = reinterpret_cast<const uint32_t*>(fragCode.data());
	vk::ShaderModule fragModule = vulkan.device.createShaderModule(fsmci);

	vk::PipelineShaderStageCreateInfo pssci1;
	pssci1.stage = vk::ShaderStageFlagBits::eVertex;
	pssci1.module = vertModule;
	pssci1.pName = "main";

	vk::PipelineShaderStageCreateInfo pssci2;
	pssci2.stage = vk::ShaderStageFlagBits::eFragment;
	pssci2.module = fragModule;
	pssci2.pName = "main";

	std::vector<vk::PipelineShaderStageCreateInfo> stages{ pssci1, pssci2 };
	_pipeline.pipeinfo.stageCount = static_cast<uint32_t>(stages.size());
	_pipeline.pipeinfo.pStages = stages.data();

	// Vertex Input state
	vk::PipelineVertexInputStateCreateInfo pvisci;
	_pipeline.pipeinfo.pVertexInputState = &pvisci;

	// Input Assembly stage
	vk::PipelineInputAssemblyStateCreateInfo piasci;
	piasci.topology = vk::PrimitiveTopology::eTriangleList;
	piasci.primitiveRestartEnable = VK_FALSE;
	_pipeline.pipeinfo.pInputAssemblyState = &piasci;

	// Viewports and Scissors
	vk::Viewport vp;
	vp.x = 0.0f;
	vp.y = 0.0f;
	vp.width = WIDTH_f;
	vp.height = HEIGHT_f;
	vp.minDepth = 0.0f;
	vp.maxDepth = 1.0f;

	vk::Rect2D r2d;
	r2d.extent = vulkan.surface->actualExtent;

	vk::PipelineViewportStateCreateInfo pvsci;
	pvsci.viewportCount = 1;
	pvsci.pViewports = &vp;
	pvsci.scissorCount = 1;
	pvsci.pScissors = &r2d;
	_pipeline.pipeinfo.pViewportState = &pvsci;

	// Rasterization state
	vk::PipelineRasterizationStateCreateInfo prsci;
	prsci.depthClampEnable = VK_FALSE;
	prsci.rasterizerDiscardEnable = VK_FALSE;
	prsci.polygonMode = vk::PolygonMode::eFill;
	prsci.cullMode = vk::CullModeFlagBits::eBack;
	prsci.frontFace = vk::FrontFace::eClockwise;
	prsci.depthBiasEnable = VK_FALSE;
	prsci.depthBiasConstantFactor = 0.0f;
	prsci.depthBiasClamp = 0.0f;
	prsci.depthBiasSlopeFactor = 0.0f;
	prsci.lineWidth = 1.0f;
	_pipeline.pipeinfo.pRasterizationState = &prsci;

	// Multisample state
	vk::PipelineMultisampleStateCreateInfo pmsci;
	pmsci.rasterizationSamples = vk::SampleCountFlagBits::e1;
	pmsci.sampleShadingEnable = VK_FALSE;
	pmsci.minSampleShading = 1.0f;
	pmsci.pSampleMask = nullptr;
	pmsci.alphaToCoverageEnable = VK_FALSE;
	pmsci.alphaToOneEnable = VK_FALSE;
	_pipeline.pipeinfo.pMultisampleState = &pmsci;

	// Depth stencil state
	vk::PipelineDepthStencilStateCreateInfo pdssci;
	pdssci.depthTestEnable = VK_TRUE;
	pdssci.depthWriteEnable = VK_TRUE;
	pdssci.depthCompareOp = vk::CompareOp::eGreater;
	pdssci.depthBoundsTestEnable = VK_FALSE;
	pdssci.stencilTestEnable = VK_FALSE;
	pdssci.front.compareOp = vk::CompareOp::eAlways;
	pdssci.back.compareOp = vk::CompareOp::eAlways;
	pdssci.minDepthBounds = 0.0f;
	pdssci.maxDepthBounds = 0.0f;
	_pipeline.pipeinfo.pDepthStencilState = &pdssci;

	// Color Blending state
	vk::PipelineColorBlendAttachmentState cba;
	cba.blendEnable = VK_TRUE;
	cba.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
	cba.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
	cba.colorBlendOp = vk::BlendOp::eAdd;
	cba.srcAlphaBlendFactor = vk::BlendFactor::eOne;
	cba.dstAlphaBlendFactor = vk::BlendFactor::eZero;
	cba.alphaBlendOp = vk::BlendOp::eAdd;
	cba.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;

	std::vector<vk::PipelineColorBlendAttachmentState> colorBlendAttachments = { cba, cba };

	vk::PipelineColorBlendStateCreateInfo pcbsci;
	pcbsci.logicOpEnable = VK_FALSE;
	pcbsci.logicOp = vk::LogicOp::eCopy;
	pcbsci.attachmentCount = (uint32_t)colorBlendAttachments.size();
	pcbsci.pAttachments = colorBlendAttachments.data();
	float blendConstants[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	memcpy(pcbsci.blendConstants, blendConstants, 4 * sizeof(float));
	_pipeline.pipeinfo.pColorBlendState = &pcbsci;

	// Dynamic state
	_pipeline.pipeinfo.pDynamicState = nullptr;

	// Pipeline Layout
	if (!bloom.DSLayoutCombine)
	{
		auto layoutBinding = [](uint32_t binding, vk::DescriptorType descriptorType) {
			return vk::DescriptorSetLayoutBinding{ binding, descriptorType, 1, vk::ShaderStageFlagBits::eFragment, nullptr };
		};
		std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings{
			layoutBinding(0, vk::DescriptorType::eCombinedImageSampler),
			layoutBinding(1, vk::DescriptorType::eCombinedImageSampler),
		};
		vk::DescriptorSetLayoutCreateInfo descriptorLayout;
		descriptorLayout.bindingCount = (uint32_t)setLayoutBindings.size();
		descriptorLayout.pBindings = setLayoutBindings.data();
		bloom.DSLayoutCombine = vulkan.device.createDescriptorSetLayout(descriptorLayout);
	}

	std::vector<vk::DescriptorSetLayout> descriptorSetLayouts = { bloom.DSLayoutCombine };

	vk::PushConstantRange pConstants;
	pConstants.stageFlags = vk::ShaderStageFlagBits::eFragment;
	pConstants.offset = 0;
	pConstants.size = 5 * sizeof(vec4);

	vk::PipelineLayoutCreateInfo plci;
	plci.setLayoutCount = (uint32_t)descriptorSetLayouts.size();
	plci.pSetLayouts = descriptorSetLayouts.data();
	plci.pushConstantRangeCount = 1;
	plci.pPushConstantRanges = &pConstants;
	_pipeline.pipeinfo.layout = vulkan.device.createPipelineLayout(plci);

	// Render Pass
	_pipeline.pipeinfo.renderPass = bloom.renderPassCombine;

	// Subpass (Index of subpass this pipeline will be used in)
	_pipeline.pipeinfo.subpass = 0;

	// Base Pipeline Handle
	_pipeline.pipeinfo.basePipelineHandle = nullptr;

	// Base Pipeline Index
	_pipeline.pipeinfo.basePipelineIndex = -1;

	_pipeline.pipeline = vulkan.device.createGraphicsPipelines(nullptr, _pipeline.pipeinfo).at(0);

	// destroy Shader Modules
	vulkan.device.destroyShaderModule(vertModule);
	vulkan.device.destroyShaderModule(fragModule);

	return _pipeline;
}

Pipeline Context::createSSAOPipeline()
{
	Pipeline _pipeline;

	// Shader stages
	std::vector<char> vertCode = readFile("shaders/Common/vert.spv");
	vk::ShaderModuleCreateInfo vsmci;
	vsmci.codeSize = vertCode.size();
	vsmci.pCode = reinterpret_cast<const uint32_t*>(vertCode.data());
	vk::ShaderModule vertModule = vulkan.device.createShaderModule(vsmci);

	std::vector<char> fragCode = readFile("shaders/SSAO/frag.spv");
	vk::ShaderModuleCreateInfo fsmci;
	fsmci.codeSize = fragCode.size();
	fsmci.pCode = reinterpret_cast<const uint32_t*>(fragCode.data());
	vk::ShaderModule fragModule = vulkan.device.createShaderModule(fsmci);

	vk::PipelineShaderStageCreateInfo pssci1;
	pssci1.stage = vk::ShaderStageFlagBits::eVertex;
	pssci1.module = vertModule;
	pssci1.pName = "main";

	vk::PipelineShaderStageCreateInfo pssci2;
	pssci2.stage = vk::ShaderStageFlagBits::eFragment;
	pssci2.module = fragModule;
	pssci2.pName = "main";

	std::vector<vk::PipelineShaderStageCreateInfo> stages{ pssci1, pssci2 };
	_pipeline.pipeinfo.stageCount = static_cast<uint32_t>(stages.size());
	_pipeline.pipeinfo.pStages = stages.data();

	// Vertex Input state
	vk::PipelineVertexInputStateCreateInfo pvisci;
	_pipeline.pipeinfo.pVertexInputState = &pvisci;

	// Input Assembly stage
	vk::PipelineInputAssemblyStateCreateInfo piasci;
	piasci.topology = vk::PrimitiveTopology::eTriangleList;
	piasci.primitiveRestartEnable = VK_FALSE;
	_pipeline.pipeinfo.pInputAssemblyState = &piasci;

	// Viewports and Scissors
	vk::Viewport vp;
	vp.x = 0.0f;
	vp.y = 0.0f;
	vp.width = WIDTH_f;
	vp.height = HEIGHT_f;
	vp.minDepth = 0.0f;
	vp.maxDepth = 1.0f;

	vk::Rect2D r2d;
	r2d.extent = vulkan.surface->actualExtent;

	vk::PipelineViewportStateCreateInfo pvsci;
	pvsci.viewportCount = 1;
	pvsci.pViewports = &vp;
	pvsci.scissorCount = 1;
	pvsci.pScissors = &r2d;
	_pipeline.pipeinfo.pViewportState = &pvsci;

	// Rasterization state
	vk::PipelineRasterizationStateCreateInfo prsci;
	prsci.depthClampEnable = VK_FALSE;
	prsci.rasterizerDiscardEnable = VK_FALSE;
	prsci.polygonMode = vk::PolygonMode::eFill;
	prsci.cullMode = vk::CullModeFlagBits::eBack;
	prsci.frontFace = vk::FrontFace::eClockwise;
	prsci.depthBiasEnable = VK_FALSE;
	prsci.depthBiasConstantFactor = 0.0f;
	prsci.depthBiasClamp = 0.0f;
	prsci.depthBiasSlopeFactor = 0.0f;
	prsci.lineWidth = 1.0f;
	_pipeline.pipeinfo.pRasterizationState = &prsci;

	// Multisample state
	vk::PipelineMultisampleStateCreateInfo pmsci;
	pmsci.rasterizationSamples = vk::SampleCountFlagBits::e1;
	pmsci.sampleShadingEnable = VK_FALSE;
	pmsci.minSampleShading = 1.0f;
	pmsci.pSampleMask = nullptr;
	pmsci.alphaToCoverageEnable = VK_FALSE;
	pmsci.alphaToOneEnable = VK_FALSE;
	_pipeline.pipeinfo.pMultisampleState = &pmsci;

	// Depth stencil state
	vk::PipelineDepthStencilStateCreateInfo pdssci;
	pdssci.depthTestEnable = VK_TRUE;
	pdssci.depthWriteEnable = VK_TRUE;
	pdssci.depthCompareOp = vk::CompareOp::eGreater;
	pdssci.depthBoundsTestEnable = VK_FALSE;
	pdssci.stencilTestEnable = VK_FALSE;
	pdssci.front.compareOp = vk::CompareOp::eAlways;
	pdssci.back.compareOp = vk::CompareOp::eAlways;
	pdssci.minDepthBounds = 0.0f;
	pdssci.maxDepthBounds = 0.0f;
	_pipeline.pipeinfo.pDepthStencilState = &pdssci;

	// Color Blending state
	vk::PipelineColorBlendAttachmentState cba;
	cba.blendEnable = VK_TRUE;
	cba.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
	cba.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
	cba.colorBlendOp = vk::BlendOp::eAdd;
	cba.srcAlphaBlendFactor = vk::BlendFactor::eOne;
	cba.dstAlphaBlendFactor = vk::BlendFactor::eZero;
	cba.alphaBlendOp = vk::BlendOp::eAdd;
	cba.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;

	std::vector<vk::PipelineColorBlendAttachmentState> colorBlendAttachments = { cba };

	vk::PipelineColorBlendStateCreateInfo pcbsci;
	pcbsci.logicOpEnable = VK_FALSE;
	pcbsci.logicOp = vk::LogicOp::eCopy;
	pcbsci.attachmentCount = (uint32_t)colorBlendAttachments.size();
	pcbsci.pAttachments = colorBlendAttachments.data();
	float blendConstants[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	memcpy(pcbsci.blendConstants, blendConstants, 4 * sizeof(float));
	_pipeline.pipeinfo.pColorBlendState = &pcbsci;

	// Dynamic state
	_pipeline.pipeinfo.pDynamicState = nullptr;

	// Pipeline Layout
	if (!ssao.DSLayout)
	{
		auto layoutBinding = [](uint32_t binding, vk::DescriptorType descriptorType) {
			return vk::DescriptorSetLayoutBinding{ binding, descriptorType, 1, vk::ShaderStageFlagBits::eFragment, nullptr };
		};
		std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings{
			layoutBinding(0, vk::DescriptorType::eCombinedImageSampler),
			layoutBinding(1, vk::DescriptorType::eCombinedImageSampler),
			layoutBinding(2, vk::DescriptorType::eCombinedImageSampler),
			layoutBinding(3, vk::DescriptorType::eUniformBuffer),
			layoutBinding(4, vk::DescriptorType::eUniformBuffer),
		};
		vk::DescriptorSetLayoutCreateInfo descriptorLayout;
		descriptorLayout.bindingCount = (uint32_t)setLayoutBindings.size();
		descriptorLayout.pBindings = setLayoutBindings.data();
		ssao.DSLayout = vulkan.device.createDescriptorSetLayout(descriptorLayout);
	}

	std::vector<vk::DescriptorSetLayout> descriptorSetLayouts = { ssao.DSLayout };

	vk::PushConstantRange pConstants;
	pConstants.stageFlags = vk::ShaderStageFlagBits::eFragment;
	pConstants.offset = 0;
	pConstants.size = 4 * sizeof(vec4);

	vk::PipelineLayoutCreateInfo plci;
	plci.setLayoutCount = (uint32_t)descriptorSetLayouts.size();
	plci.pSetLayouts = descriptorSetLayouts.data();
	plci.pushConstantRangeCount = 1;
	plci.pPushConstantRanges = &pConstants;
	_pipeline.pipeinfo.layout = vulkan.device.createPipelineLayout(plci);

	// Render Pass
	_pipeline.pipeinfo.renderPass = ssao.renderPass;

	// Subpass (Index of subpass this pipeline will be used in)
	_pipeline.pipeinfo.subpass = 0;

	// Base Pipeline Handle
	_pipeline.pipeinfo.basePipelineHandle = nullptr;

	// Base Pipeline Index
	_pipeline.pipeinfo.basePipelineIndex = -1;

	_pipeline.pipeline = vulkan.device.createGraphicsPipelines(nullptr, _pipeline.pipeinfo).at(0);

	// destroy Shader Modules
	vulkan.device.destroyShaderModule(vertModule);
	vulkan.device.destroyShaderModule(fragModule);

	return _pipeline;
}

Pipeline Context::createSSAOBlurPipeline()
{
	Pipeline _pipeline;

	// Shader stages
	std::vector<char> vertCode = readFile("shaders/Common/vert.spv");
	vk::ShaderModuleCreateInfo vsmci;
	vsmci.codeSize = vertCode.size();
	vsmci.pCode = reinterpret_cast<const uint32_t*>(vertCode.data());
	vk::ShaderModule vertModule = vulkan.device.createShaderModule(vsmci);

	std::vector<char> fragCode = readFile("shaders/SSAO/fragBlur.spv");
	vk::ShaderModuleCreateInfo fsmci;
	fsmci.codeSize = fragCode.size();
	fsmci.pCode = reinterpret_cast<const uint32_t*>(fragCode.data());
	vk::ShaderModule fragModule = vulkan.device.createShaderModule(fsmci);

	vk::PipelineShaderStageCreateInfo pssci1;
	pssci1.stage = vk::ShaderStageFlagBits::eVertex;
	pssci1.module = vertModule;
	pssci1.pName = "main";

	vk::PipelineShaderStageCreateInfo pssci2;
	pssci2.stage = vk::ShaderStageFlagBits::eFragment;
	pssci2.module = fragModule;
	pssci2.pName = "main";

	std::vector<vk::PipelineShaderStageCreateInfo> stages{ pssci1, pssci2 };
	_pipeline.pipeinfo.stageCount = static_cast<uint32_t>(stages.size());
	_pipeline.pipeinfo.pStages = stages.data();

	// Vertex Input state
	vk::PipelineVertexInputStateCreateInfo pvisci;
	_pipeline.pipeinfo.pVertexInputState = &pvisci;

	// Input Assembly stage
	vk::PipelineInputAssemblyStateCreateInfo piasci;
	piasci.topology = vk::PrimitiveTopology::eTriangleList;
	piasci.primitiveRestartEnable = VK_FALSE;
	_pipeline.pipeinfo.pInputAssemblyState = &piasci;

	// Viewports and Scissors
	vk::Viewport vp;
	vp.x = 0.0f;
	vp.y = 0.0f;
	vp.width = WIDTH_f;
	vp.height = HEIGHT_f;
	vp.minDepth = 0.0f;
	vp.maxDepth = 1.0f;

	vk::Rect2D r2d;
	r2d.extent = vulkan.surface->actualExtent;

	vk::PipelineViewportStateCreateInfo pvsci;
	pvsci.viewportCount = 1;
	pvsci.pViewports = &vp;
	pvsci.scissorCount = 1;
	pvsci.pScissors = &r2d;
	_pipeline.pipeinfo.pViewportState = &pvsci;

	// Rasterization state
	vk::PipelineRasterizationStateCreateInfo prsci;
	prsci.depthClampEnable = VK_FALSE;
	prsci.rasterizerDiscardEnable = VK_FALSE;
	prsci.polygonMode = vk::PolygonMode::eFill;
	prsci.cullMode = vk::CullModeFlagBits::eBack;
	prsci.frontFace = vk::FrontFace::eClockwise;
	prsci.depthBiasEnable = VK_FALSE;
	prsci.depthBiasConstantFactor = 0.0f;
	prsci.depthBiasClamp = 0.0f;
	prsci.depthBiasSlopeFactor = 0.0f;
	prsci.lineWidth = 1.0f;
	_pipeline.pipeinfo.pRasterizationState = &prsci;

	// Multisample state
	vk::PipelineMultisampleStateCreateInfo pmsci;
	pmsci.rasterizationSamples = vk::SampleCountFlagBits::e1;
	pmsci.sampleShadingEnable = VK_FALSE;
	pmsci.minSampleShading = 1.0f;
	pmsci.pSampleMask = nullptr;
	pmsci.alphaToCoverageEnable = VK_FALSE;
	pmsci.alphaToOneEnable = VK_FALSE;
	_pipeline.pipeinfo.pMultisampleState = &pmsci;

	// Depth stencil state
	vk::PipelineDepthStencilStateCreateInfo pdssci;
	pdssci.depthTestEnable = VK_TRUE;
	pdssci.depthWriteEnable = VK_TRUE;
	pdssci.depthCompareOp = vk::CompareOp::eGreater;
	pdssci.depthBoundsTestEnable = VK_FALSE;
	pdssci.stencilTestEnable = VK_FALSE;
	pdssci.front.compareOp = vk::CompareOp::eAlways;
	pdssci.back.compareOp = vk::CompareOp::eAlways;
	pdssci.minDepthBounds = 0.0f;
	pdssci.maxDepthBounds = 0.0f;
	_pipeline.pipeinfo.pDepthStencilState = &pdssci;

	// Color Blending state
	vk::PipelineColorBlendAttachmentState cba;
	cba.blendEnable = VK_TRUE;
	cba.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
	cba.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
	cba.colorBlendOp = vk::BlendOp::eAdd;
	cba.srcAlphaBlendFactor = vk::BlendFactor::eOne;
	cba.dstAlphaBlendFactor = vk::BlendFactor::eZero;
	cba.alphaBlendOp = vk::BlendOp::eAdd;
	cba.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;

	std::vector<vk::PipelineColorBlendAttachmentState> colorBlendAttachments = { cba };

	vk::PipelineColorBlendStateCreateInfo pcbsci;
	pcbsci.logicOpEnable = VK_FALSE;
	pcbsci.logicOp = vk::LogicOp::eCopy;
	pcbsci.attachmentCount = (uint32_t)colorBlendAttachments.size();
	pcbsci.pAttachments = colorBlendAttachments.data();
	float blendConstants[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	memcpy(pcbsci.blendConstants, blendConstants, 4 * sizeof(float));
	_pipeline.pipeinfo.pColorBlendState = &pcbsci;

	// Dynamic state
	_pipeline.pipeinfo.pDynamicState = nullptr;

	// Pipeline Layout
	if (!ssao.DSLayoutBlur)
	{
		auto layoutBinding = [](uint32_t binding, vk::DescriptorType descriptorType) {
			return vk::DescriptorSetLayoutBinding{ binding, descriptorType, 1, vk::ShaderStageFlagBits::eFragment, nullptr };
		};
		std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings{
			layoutBinding(0, vk::DescriptorType::eCombinedImageSampler),
		};
		vk::DescriptorSetLayoutCreateInfo descriptorLayout;
		descriptorLayout.bindingCount = (uint32_t)setLayoutBindings.size();
		descriptorLayout.pBindings = setLayoutBindings.data();
		ssao.DSLayoutBlur = vulkan.device.createDescriptorSetLayout(descriptorLayout);
	}

	std::vector<vk::DescriptorSetLayout> descriptorSetLayouts = { ssao.DSLayoutBlur };

	vk::PipelineLayoutCreateInfo plci;
	plci.setLayoutCount = (uint32_t)descriptorSetLayouts.size();
	plci.pSetLayouts = descriptorSetLayouts.data();

	_pipeline.pipeinfo.layout = vulkan.device.createPipelineLayout(plci);

	// Render Pass
	_pipeline.pipeinfo.renderPass = ssao.blurRenderPass;

	// Subpass (Index of subpass this pipeline will be used in)
	_pipeline.pipeinfo.subpass = 0;

	// Base Pipeline Handle
	_pipeline.pipeinfo.basePipelineHandle = nullptr;

	// Base Pipeline Index
	_pipeline.pipeinfo.basePipelineIndex = -1;

	_pipeline.pipeline = vulkan.device.createGraphicsPipelines(nullptr, _pipeline.pipeinfo).at(0);

	// destroy Shader Modules
	vulkan.device.destroyShaderModule(vertModule);
	vulkan.device.destroyShaderModule(fragModule);

	return _pipeline;
}

Pipeline Context::createMotionBlurPipeline()
{
	Pipeline _pipeline;

	// Shader stages
	std::vector<char> vertCode = readFile("shaders/Common/vert.spv");
	vk::ShaderModuleCreateInfo vsmci;
	vsmci.codeSize = vertCode.size();
	vsmci.pCode = reinterpret_cast<const uint32_t*>(vertCode.data());
	vk::ShaderModule vertModule = vulkan.device.createShaderModule(vsmci);

	std::vector<char> fragCode = readFile("shaders/MotionBlur/frag.spv");
	vk::ShaderModuleCreateInfo fsmci;
	fsmci.codeSize = fragCode.size();
	fsmci.pCode = reinterpret_cast<const uint32_t*>(fragCode.data());
	vk::ShaderModule fragModule = vulkan.device.createShaderModule(fsmci);

	vk::PipelineShaderStageCreateInfo pssci1;
	pssci1.stage = vk::ShaderStageFlagBits::eVertex;
	pssci1.module = vertModule;
	pssci1.pName = "main";

	vk::PipelineShaderStageCreateInfo pssci2;
	pssci2.stage = vk::ShaderStageFlagBits::eFragment;
	pssci2.module = fragModule;
	pssci2.pName = "main";

	std::vector<vk::PipelineShaderStageCreateInfo> stages{ pssci1, pssci2 };
	_pipeline.pipeinfo.stageCount = static_cast<uint32_t>(stages.size());
	_pipeline.pipeinfo.pStages = stages.data();

	// Vertex Input state
	vk::PipelineVertexInputStateCreateInfo pvisci;
	_pipeline.pipeinfo.pVertexInputState = &pvisci;

	// Input Assembly stage
	vk::PipelineInputAssemblyStateCreateInfo piasci;
	piasci.topology = vk::PrimitiveTopology::eTriangleList;
	piasci.primitiveRestartEnable = VK_FALSE;
	_pipeline.pipeinfo.pInputAssemblyState = &piasci;

	// Viewports and Scissors
	vk::Viewport vp;
	vp.x = 0.0f;
	vp.y = 0.0f;
	vp.width = WIDTH_f;
	vp.height = HEIGHT_f;
	vp.minDepth = 0.0f;
	vp.maxDepth = 1.0f;

	vk::Rect2D r2d;
	r2d.extent = vulkan.surface->actualExtent;

	vk::PipelineViewportStateCreateInfo pvsci;
	pvsci.viewportCount = 1;
	pvsci.pViewports = &vp;
	pvsci.scissorCount = 1;
	pvsci.pScissors = &r2d;
	_pipeline.pipeinfo.pViewportState = &pvsci;

	// Rasterization state
	vk::PipelineRasterizationStateCreateInfo prsci;
	prsci.depthClampEnable = VK_FALSE;
	prsci.rasterizerDiscardEnable = VK_FALSE;
	prsci.polygonMode = vk::PolygonMode::eFill;
	prsci.cullMode = vk::CullModeFlagBits::eBack;
	prsci.frontFace = vk::FrontFace::eClockwise;
	prsci.depthBiasEnable = VK_FALSE;
	prsci.depthBiasConstantFactor = 0.0f;
	prsci.depthBiasClamp = 0.0f;
	prsci.depthBiasSlopeFactor = 0.0f;
	prsci.lineWidth = 1.0f;
	_pipeline.pipeinfo.pRasterizationState = &prsci;

	// Multisample state
	vk::PipelineMultisampleStateCreateInfo pmsci;
	pmsci.rasterizationSamples = vk::SampleCountFlagBits::e1;
	pmsci.sampleShadingEnable = VK_FALSE;
	pmsci.minSampleShading = 1.0f;
	pmsci.pSampleMask = nullptr;
	pmsci.alphaToCoverageEnable = VK_FALSE;
	pmsci.alphaToOneEnable = VK_FALSE;
	_pipeline.pipeinfo.pMultisampleState = &pmsci;

	// Depth stencil state
	vk::PipelineDepthStencilStateCreateInfo pdssci;
	pdssci.depthTestEnable = VK_TRUE;
	pdssci.depthWriteEnable = VK_TRUE;
	pdssci.depthCompareOp = vk::CompareOp::eGreater;
	pdssci.depthBoundsTestEnable = VK_FALSE;
	pdssci.stencilTestEnable = VK_FALSE;
	pdssci.front.compareOp = vk::CompareOp::eAlways;
	pdssci.back.compareOp = vk::CompareOp::eAlways;
	pdssci.minDepthBounds = 0.0f;
	pdssci.maxDepthBounds = 0.0f;
	_pipeline.pipeinfo.pDepthStencilState = &pdssci;

	// Color Blending state
	vk::PipelineColorBlendAttachmentState cba;
	cba.blendEnable = VK_TRUE;
	cba.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
	cba.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
	cba.colorBlendOp = vk::BlendOp::eAdd;
	cba.srcAlphaBlendFactor = vk::BlendFactor::eOne;
	cba.dstAlphaBlendFactor = vk::BlendFactor::eZero;
	cba.alphaBlendOp = vk::BlendOp::eAdd;
	cba.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;

	std::vector<vk::PipelineColorBlendAttachmentState> colorBlendAttachments = { cba };

	vk::PipelineColorBlendStateCreateInfo pcbsci;
	pcbsci.logicOpEnable = VK_FALSE;
	pcbsci.logicOp = vk::LogicOp::eCopy;
	pcbsci.attachmentCount = (uint32_t)colorBlendAttachments.size();
	pcbsci.pAttachments = colorBlendAttachments.data();
	float blendConstants[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	memcpy(pcbsci.blendConstants, blendConstants, 4 * sizeof(float));
	_pipeline.pipeinfo.pColorBlendState = &pcbsci;

	// Dynamic state
	_pipeline.pipeinfo.pDynamicState = nullptr;

	// Pipeline Layout
	if (!motionBlur.DSLayoutMotionBlur)
	{
		auto layoutBinding = [](uint32_t binding, vk::DescriptorType descriptorType) {
			return vk::DescriptorSetLayoutBinding{ binding, descriptorType, 1, vk::ShaderStageFlagBits::eFragment, nullptr };
		};
		std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings{
			layoutBinding(0, vk::DescriptorType::eCombinedImageSampler),
			layoutBinding(1, vk::DescriptorType::eCombinedImageSampler),
			layoutBinding(2, vk::DescriptorType::eCombinedImageSampler),
			layoutBinding(3, vk::DescriptorType::eUniformBuffer),
		};
		vk::DescriptorSetLayoutCreateInfo descriptorLayout;
		descriptorLayout.bindingCount = (uint32_t)setLayoutBindings.size();
		descriptorLayout.pBindings = setLayoutBindings.data();
		motionBlur.DSLayoutMotionBlur = vulkan.device.createDescriptorSetLayout(descriptorLayout);
	}

	std::vector<vk::DescriptorSetLayout> descriptorSetLayouts = { motionBlur.DSLayoutMotionBlur };

	vk::PushConstantRange pConstants;
	pConstants.stageFlags = vk::ShaderStageFlagBits::eFragment;
	pConstants.offset = 0;
	pConstants.size = 8 * sizeof(vec4);

	vk::PipelineLayoutCreateInfo plci;
	plci.setLayoutCount = (uint32_t)descriptorSetLayouts.size();
	plci.pSetLayouts = descriptorSetLayouts.data();
	plci.pushConstantRangeCount = 1;
	plci.pPushConstantRanges = &pConstants;
	_pipeline.pipeinfo.layout = vulkan.device.createPipelineLayout(plci);

	// Render Pass
	_pipeline.pipeinfo.renderPass = motionBlur.renderPass;

	// Subpass (Index of subpass this pipeline will be used in)
	_pipeline.pipeinfo.subpass = 0;

	// Base Pipeline Handle
	_pipeline.pipeinfo.basePipelineHandle = nullptr;

	// Base Pipeline Index
	_pipeline.pipeinfo.basePipelineIndex = -1;

	_pipeline.pipeline = vulkan.device.createGraphicsPipelines(nullptr, _pipeline.pipeinfo).at(0);

	// destroy Shader Modules
	vulkan.device.destroyShaderModule(vertModule);
	vulkan.device.destroyShaderModule(fragModule);

	return _pipeline;
}

vk::DescriptorPool Context::createDescriptorPool(uint32_t maxDescriptorSets)
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
	createInfo.poolSizeCount = (uint32_t)descPoolsize.size();
	createInfo.pPoolSizes = descPoolsize.data();
	createInfo.maxSets = maxDescriptorSets;

	return vulkan.device.createDescriptorPool(createInfo);
}

std::vector<vk::Fence> Context::createFences(const uint32_t fenceCount)
{
	std::vector<vk::Fence> _fences(fenceCount);
	vk::FenceCreateInfo fi;

	for (uint32_t i = 0; i < fenceCount; i++) {
		_fences[i] = vulkan.device.createFence(fi);
	}

	return _fences;
}

std::vector<vk::Semaphore> Context::createSemaphores(const uint32_t semaphoresCount)
{
	std::vector<vk::Semaphore> _semaphores(semaphoresCount);
	vk::SemaphoreCreateInfo si;

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

	if (vulkan.instance) {
		vulkan.instance.destroy();
	}
}

vk::DescriptorSetLayout Context::getDescriptorSetLayoutLights()
{
	// binding for model mvp matrix
	if (!lightUniforms.descriptorSetLayout) {
		vk::DescriptorSetLayoutBinding descriptorSetLayoutBinding;
		descriptorSetLayoutBinding.binding = 0;
		descriptorSetLayoutBinding.descriptorCount = 1;
		descriptorSetLayoutBinding.descriptorType = vk::DescriptorType::eUniformBuffer;
		descriptorSetLayoutBinding.stageFlags = vk::ShaderStageFlagBits::eFragment;

		vk::DescriptorSetLayoutCreateInfo createInfo;
		createInfo.bindingCount = 1;
		createInfo.pBindings = &descriptorSetLayoutBinding;

		lightUniforms.descriptorSetLayout = vulkan.device.createDescriptorSetLayout(createInfo);
	}
	return lightUniforms.descriptorSetLayout;
}

PipelineInfo Context::getPipelineSpecificationsShadows()
{
	// Shadows Pipeline
	static PipelineInfo shadowsSpecific;
	shadowsSpecific.shaders = { "shaders/Shadows/vert.spv" };
	shadowsSpecific.renderPass = shadows.renderPass;
	shadowsSpecific.viewportSize = { Shadows::imageSize, Shadows::imageSize };
	shadowsSpecific.useBlendState = false;
	shadowsSpecific.sampleCount = vk::SampleCountFlagBits::e1;
	shadowsSpecific.descriptorSetLayouts = { Shadows::getDescriptorSetLayout(vulkan.device), Mesh::getDescriptorSetLayout() , Model::getDescriptorSetLayout() };
	shadowsSpecific.vertexInputBindingDescriptions = Vertex::getBindingDescriptionGeneral();
	shadowsSpecific.vertexInputAttributeDescriptions = Vertex::getAttributeDescriptionGeneral();
	shadowsSpecific.pushConstantRange = vk::PushConstantRange();
	shadowsSpecific.cull = vk::CullModeFlagBits::eBack;
	shadowsSpecific.face = vk::FrontFace::eCounterClockwise;
	shadowsSpecific.depthBiasEnable = VK_TRUE;
	shadowsSpecific.dynamicStates = { vk::DynamicState::eDepthBias };
	shadowsSpecific.dynamicStateInfo = {
		vk::PipelineDynamicStateCreateFlags(),
		static_cast<uint32_t>(shadowsSpecific.dynamicStates.size()),
		shadowsSpecific.dynamicStates.data()
	};


	return shadowsSpecific;
}

PipelineInfo Context::getPipelineSpecificationsSkyBox(SkyBox& skybox)
{
	// SkyBox Pipeline
	static PipelineInfo skyBoxSpecific;
	skyBoxSpecific.shaders = { "shaders/SkyBox/vert.spv", "shaders/SkyBox/frag.spv" };
	skyBoxSpecific.renderPass = skybox.renderPass;
	skyBoxSpecific.viewportSize = { WIDTH, HEIGHT };
	skyBoxSpecific.descriptorSetLayouts = { SkyBox::getDescriptorSetLayout(vulkan.device) };
	skyBoxSpecific.vertexInputBindingDescriptions = Vertex::getBindingDescriptionSkyBox();
	skyBoxSpecific.vertexInputAttributeDescriptions = Vertex::getAttributeDescriptionSkyBox();
	skyBoxSpecific.cull = vk::CullModeFlagBits::eFront;
	skyBoxSpecific.face = vk::FrontFace::eClockwise;
	skyBoxSpecific.sampleCount = vk::SampleCountFlagBits::e1;
	skyBoxSpecific.pushConstantRange = vk::PushConstantRange();
	skyBoxSpecific.blendAttachmentStates[0].blendEnable = VK_FALSE;
	skyBoxSpecific.blendAttachmentStates = {
		skyBoxSpecific.blendAttachmentStates[0],
	};
	skyBoxSpecific.dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
	skyBoxSpecific.dynamicStateInfo = {
		vk::PipelineDynamicStateCreateFlags(),
		static_cast<uint32_t>(skyBoxSpecific.dynamicStates.size()),
		skyBoxSpecific.dynamicStates.data()
	};


	return skyBoxSpecific;
}

PipelineInfo Context::getPipelineSpecificationsGUI()
{
	// GUI Pipeline
	static PipelineInfo GUISpecific;
	GUISpecific.shaders = { "shaders/GUI/vert.spv", "shaders/GUI/frag.spv" };
	GUISpecific.renderPass = gui.renderPass;
	GUISpecific.viewportSize = { WIDTH, HEIGHT };
	GUISpecific.descriptorSetLayouts = { GUI::getDescriptorSetLayout(vulkan.device) };
	GUISpecific.vertexInputBindingDescriptions = Vertex::getBindingDescriptionGUI();
	GUISpecific.vertexInputAttributeDescriptions = Vertex::getAttributeDescriptionGUI();
	GUISpecific.cull = vk::CullModeFlagBits::eBack;
	GUISpecific.face = vk::FrontFace::eClockwise;
	GUISpecific.pushConstantRange = vk::PushConstantRange{ vk::ShaderStageFlagBits::eVertex, 0, sizeof(float) * 4 };
	GUISpecific.sampleCount = vk::SampleCountFlagBits::e1;
	GUISpecific.dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
	GUISpecific.dynamicStateInfo = {
		vk::PipelineDynamicStateCreateFlags(),
		static_cast<uint32_t>(GUISpecific.dynamicStates.size()),
		GUISpecific.dynamicStates.data()
	};

	return GUISpecific;
}

PipelineInfo Context::getPipelineSpecificationsDeferred()
{
	// Deferred Pipeline
	static PipelineInfo deferredSpecific;
	deferredSpecific.vertexInputBindingDescriptions = Vertex::getBindingDescriptionGeneral();
	deferredSpecific.vertexInputAttributeDescriptions = Vertex::getAttributeDescriptionGeneral();
	deferredSpecific.pushConstantRange = vk::PushConstantRange();//{ vk::ShaderStageFlagBits::eFragment, 0, sizeof(vec4) };
	deferredSpecific.shaders = { "shaders/Deferred/vert.spv", "shaders/Deferred/frag.spv" };
	deferredSpecific.renderPass = deferred.renderPass;
	deferredSpecific.viewportSize = { WIDTH, HEIGHT };
	deferredSpecific.cull = vk::CullModeFlagBits::eFront;
	deferredSpecific.face = vk::FrontFace::eClockwise;
	deferredSpecific.sampleCount = vk::SampleCountFlagBits::e1;
	deferredSpecific.descriptorSetLayouts = { Mesh::getDescriptorSetLayout(), Primitive::getDescriptorSetLayout(), Model::getDescriptorSetLayout() };
	deferredSpecific.specializationInfo = vk::SpecializationInfo();
	deferredSpecific.blendAttachmentStates[0].blendEnable = VK_FALSE;
	deferredSpecific.blendAttachmentStates = {
		deferredSpecific.blendAttachmentStates[0],
		deferredSpecific.blendAttachmentStates[0],
		deferredSpecific.blendAttachmentStates[0],
		deferredSpecific.blendAttachmentStates[0],
		deferredSpecific.blendAttachmentStates[0],
		deferredSpecific.blendAttachmentStates[0],
	};
	deferredSpecific.dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
	deferredSpecific.dynamicStateInfo = {
		vk::PipelineDynamicStateCreateFlags(),
		static_cast<uint32_t>(deferredSpecific.dynamicStates.size()),
		deferredSpecific.dynamicStates.data()
	};

	return deferredSpecific;
}