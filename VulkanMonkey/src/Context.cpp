#include "../include/Context.h"
#include "../include/Errors.h"
#include <iostream>
#include <fstream>

Context* Context::info = nullptr;

vk::DescriptorSetLayout getDescriptorSetLayoutLights(Context* info)
{
	// binding for model mvp matrix
	if (!info->lightUniforms.descriptorSetLayout) {
		std::vector<vk::DescriptorSetLayoutBinding> descriptorSetLayoutBinding{};
		descriptorSetLayoutBinding.push_back(vk::DescriptorSetLayoutBinding()
			.setBinding(0) // binding number in shader stages
			.setDescriptorCount(1) // number of descriptors contained
			.setDescriptorType(vk::DescriptorType::eUniformBuffer)
			.setStageFlags(vk::ShaderStageFlagBits::eFragment)); // which pipeline shader stages can access
		auto const createInfo = vk::DescriptorSetLayoutCreateInfo()
			.setBindingCount((uint32_t)descriptorSetLayoutBinding.size())
			.setPBindings(descriptorSetLayoutBinding.data());
		VkCheck(info->device.createDescriptorSetLayout(&createInfo, nullptr, &info->lightUniforms.descriptorSetLayout));
		std::cout << "Descriptor Set Layout created\n";
	}
	return info->lightUniforms.descriptorSetLayout;
}
PipelineInfo Context::getPipelineSpecificationsModel()
{
	// General Pipeline
	static PipelineInfo generalSpecific;
	generalSpecific.shaders = { "shaders/General/vert.spv", "shaders/General/frag.spv" };
	generalSpecific.renderPass = info->forward.forwardRenderPass;
	generalSpecific.viewportSize = { info->surface.actualExtent.width, info->surface.actualExtent.height };
	generalSpecific.descriptorSetLayouts = { Shadows::getDescriptorSetLayout(info->device), Mesh::getDescriptorSetLayout(info->device), Model::getDescriptorSetLayout(info->device), getDescriptorSetLayoutLights(info) };
	generalSpecific.vertexInputBindingDescriptions = Vertex::getBindingDescriptionGeneral();
	generalSpecific.vertexInputAttributeDescriptions = Vertex::getAttributeDescriptionGeneral();
	generalSpecific.pushConstantRange = vk::PushConstantRange();
	generalSpecific.specializationInfo = vk::SpecializationInfo();

	return generalSpecific;
}

PipelineInfo Context::getPipelineSpecificationsShadows()
{
	// Shadows Pipeline
	static PipelineInfo shadowsSpecific;
	shadowsSpecific.shaders = { "shaders/Shadows/vert.spv" };
	shadowsSpecific.renderPass = Shadows::getRenderPass(info->device, info->depth);
	shadowsSpecific.viewportSize = { Shadows::imageSize, Shadows::imageSize };
	shadowsSpecific.useBlendState = false;
	shadowsSpecific.sampleCount = vk::SampleCountFlagBits::e1;
	shadowsSpecific.descriptorSetLayouts = { Shadows::getDescriptorSetLayout(info->device) };
	shadowsSpecific.vertexInputBindingDescriptions = Vertex::getBindingDescriptionGeneral();
	shadowsSpecific.vertexInputAttributeDescriptions = Vertex::getAttributeDescriptionGeneral();
	shadowsSpecific.pushConstantRange = vk::PushConstantRange();

	return shadowsSpecific;
}

PipelineInfo Context::getPipelineSpecificationsSkyBox()
{
	// SkyBox Pipeline
	static PipelineInfo skyBoxSpecific;
	skyBoxSpecific.shaders = { "shaders/SkyBox/vert.spv", "shaders/SkyBox/frag.spv" };
	skyBoxSpecific.renderPass = info->forward.forwardRenderPass;
	skyBoxSpecific.viewportSize = { info->surface.actualExtent.width, info->surface.actualExtent.height };
	skyBoxSpecific.descriptorSetLayouts = { SkyBox::getDescriptorSetLayout(info->device) };
	skyBoxSpecific.vertexInputBindingDescriptions = Vertex::getBindingDescriptionSkyBox();
	skyBoxSpecific.vertexInputAttributeDescriptions = Vertex::getAttributeDescriptionSkyBox();
	skyBoxSpecific.sampleCount = vk::SampleCountFlagBits::e4;
	skyBoxSpecific.pushConstantRange = vk::PushConstantRange();

	return skyBoxSpecific;
}

PipelineInfo Context::getPipelineSpecificationsTerrain()
{
	// Terrain Pipeline
	static PipelineInfo terrainSpecific;
	terrainSpecific.shaders = { "shaders/Terrain/vert.spv", "shaders/Terrain/frag.spv" };
	terrainSpecific.renderPass = info->forward.forwardRenderPass;
	terrainSpecific.viewportSize = { info->surface.actualExtent.width, info->surface.actualExtent.height };
	terrainSpecific.descriptorSetLayouts = { Terrain::getDescriptorSetLayout(info->device) };
	terrainSpecific.vertexInputBindingDescriptions = Vertex::getBindingDescriptionGeneral();
	terrainSpecific.vertexInputAttributeDescriptions = Vertex::getAttributeDescriptionGeneral();
	terrainSpecific.pushConstantRange = vk::PushConstantRange();

	return terrainSpecific;
}

PipelineInfo Context::getPipelineSpecificationsGUI()
{
	// GUI Pipeline
	static PipelineInfo GUISpecific;
	GUISpecific.shaders = { "shaders/GUI/vert.spv", "shaders/GUI/frag.spv" };
	GUISpecific.renderPass = info->gui.guiRenderPass;
	GUISpecific.viewportSize = { info->surface.actualExtent.width, info->surface.actualExtent.height };
	GUISpecific.descriptorSetLayouts = { GUI::getDescriptorSetLayout(info->device) };
	GUISpecific.vertexInputBindingDescriptions = Vertex::getBindingDescriptionGUI();
	GUISpecific.vertexInputAttributeDescriptions = Vertex::getAttributeDescriptionGUI();
	GUISpecific.cull = vk::CullModeFlagBits::eBack;
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
	deferredSpecific.pushConstantRange = vk::PushConstantRange();
	deferredSpecific.shaders = { "shaders/Deferred/vert.spv", "shaders/Deferred/frag.spv" };
	deferredSpecific.renderPass = info->deferred.deferredRenderPass;
	deferredSpecific.viewportSize = { info->surface.actualExtent.width, info->surface.actualExtent.height };
	deferredSpecific.sampleCount = vk::SampleCountFlagBits::e1;
	deferredSpecific.descriptorSetLayouts = { Model::getDescriptorSetLayout(info->device), Mesh::getDescriptorSetLayout(info->device) };
	deferredSpecific.specializationInfo = vk::SpecializationInfo();
	deferredSpecific.blendAttachmentStates[0].blendEnable = VK_FALSE;
	deferredSpecific.blendAttachmentStates = {
		deferredSpecific.blendAttachmentStates[0],
		deferredSpecific.blendAttachmentStates[0],
		deferredSpecific.blendAttachmentStates[0],
		deferredSpecific.blendAttachmentStates[0]
	};

	return deferredSpecific;
}

void Context::initVulkanContext()
{
	instance = createInstance();
	surface = createSurface();
	graphicsFamilyId = getGraphicsFamilyId();
	presentFamilyId = getPresentFamilyId();
	computeFamilyId = getComputeFamilyId();
	gpu = findGpu();
	gpuProperties = getGPUProperties();
	gpuFeatures = getGPUFeatures();
	surface.capabilities = getSurfaceCapabilities();
	surface.formatKHR = getSurfaceFormat();
	surface.presentModeKHR = getPresentationMode();
	device = createDevice();
	graphicsQueue = getGraphicsQueue();
	presentQueue = getPresentQueue();
	computeQueue = getComputeQueue();
	semaphores = createSemaphores(3);
	fences = createFences(2);
	swapchain = createSwapchain();
	commandPool = createCommandPool();
	commandPoolCompute = createComputeCommadPool();
	descriptorPool = createDescriptorPool(1000); // max number of all descriptor sets to allocate
	dynamicCmdBuffer = createCmdBuffer();
	shadowCmdBuffer = createCmdBuffer();
	computeCmdBuffer = createComputeCmdBuffer();
	depth = createDepthResources();
	renderTargets = createRenderTargets({ 
		{"position", vk::Format::eR16G16B16A16Sfloat},
		{"normal", vk::Format::eR16G16B16A16Sfloat},
		{"albedo", vk::Format::eR8G8B8A8Unorm},
		{"specular", vk::Format::eR16G16B16A16Sfloat},
		{"ssao", vk::Format::eR16Sfloat},
		{"ssaoBlur", vk::Format::eR16Sfloat} });
}

void Context::initRendering()
{
	// render passes
	forward.forwardRenderPass = createRenderPass();
	deferred.deferredRenderPass = createDeferredRenderPass();
	deferred.compositionRenderPass = createCompositionRenderPass();
	ssr.ssrRenderPass = createReflectionRenderPass();
	ssao.ssaoRenderPass = createSSAORenderPass();
	ssao.ssaoBlurRenderPass = createSSAOBlurRenderPass();
	gui.guiRenderPass = createGUIRenderPass();

	// frame buffers
	forward.frameBuffers = createFrameBuffers();
	deferred.deferredFrameBuffers = createDeferredFrameBuffers();
	deferred.compositionFrameBuffers = createCompositionFrameBuffers();
	ssr.ssrFrameBuffers = createReflectionFrameBuffers();
	ssao.ssaoFrameBuffers = createSSAOFrameBuffers();
	ssao.ssaoBlurFrameBuffers = createSSAOBlurFrameBuffers();
	gui.guiFrameBuffers = createGUIFrameBuffers();
	shadows.createFrameBuffers(device, gpu, depth, static_cast<uint32_t>(swapchain.images.size()));

	// pipelines
	PipelineInfo gpi = getPipelineSpecificationsModel();
	gpi.specializationInfo = vk::SpecializationInfo{ 1, &vk::SpecializationMapEntry{ 0, 0, sizeof(MAX_LIGHTS) }, sizeof(MAX_LIGHTS), &MAX_LIGHTS };

	forward.pipeline = createPipeline(gpi);
	terrain.pipelineTerrain = createPipeline(getPipelineSpecificationsTerrain());
	shadows.pipelineShadows = createPipeline(getPipelineSpecificationsShadows());
	skyBox.pipelineSkyBox = createPipeline(getPipelineSpecificationsSkyBox());
	gui.pipelineGUI = createPipeline(getPipelineSpecificationsGUI());
	deferred.pipelineDeferred = createPipeline(getPipelineSpecificationsDeferred());
	deferred.pipelineComposition = createCompositionPipeline();
	ssr.pipelineSSR = createReflectionPipeline();
	compute.pipelineCompute = createComputePipeline();
	ssao.pipelineSSAO = createSSAOPipeline();
	ssao.pipelineSSAOBlur = createSSAOBlurPipeline();
}

void Context::loadResources()
{
	// SKYBOX LOAD
	skyBox.loadSkyBox(
		{ "objects/sky/right.png", "objects/sky/left.png", "objects/sky/top.png", "objects/sky/bottom.png", "objects/sky/back.png", "objects/sky/front.png" },
		1024,
		device,
		gpu,
		commandPool,
		graphicsQueue,
		descriptorPool
	);
	// GUI LOAD
	gui.loadGUI("ImGuiDemo", device, gpu, dynamicCmdBuffer, graphicsQueue, descriptorPool, window);
	// TERRAIN LOAD
	terrain.generateTerrain("", device, gpu, commandPool, graphicsQueue, descriptorPool);
	// MODELS LOAD
	models.push_back(Model::loadModel("objects/sponza/", "sponza.obj", device, gpu, commandPool, graphicsQueue, descriptorPool));
}

void Context::createUniforms()
{
	// DESCRIPTOR SETS FOR SHADOWS
	shadows.createDynamicUniformBuffer(device, gpu, models.size());
	shadows.createDescriptorSet(device, descriptorPool, Shadows::getDescriptorSetLayout(device));
	// DESCRIPTOR SETS FOR LIGHTS
	lightUniforms.createLightUniforms(device, gpu, descriptorPool);
	// DESCRIPTOR SETS FOR SSAO
	ssao.createSSAOUniforms(renderTargets, device, gpu, commandPool, graphicsQueue, descriptorPool);
	// DESCRIPTOR SETS FOR COMPOSITION PIPELINE
	deferred.createDeferredUniforms(renderTargets, lightUniforms, device, descriptorPool);
	// DESCRIPTOR SET FOR REFLECTION PIPELINE
	ssr.createSSRUniforms(renderTargets, device, gpu, descriptorPool);
	// DESCRIPTOR SET FOR COMPUTE PIPELINE
	compute.createComputeUniforms(device, gpu, descriptorPool);
}

vk::Instance Context::createInstance()
{
	vk::Instance _instance;
	unsigned extCount;
	if (!SDL_Vulkan_GetInstanceExtensions(window, &extCount, nullptr))
	{
		std::cout << SDL_GetError();
		exit(-1);
	}
	std::vector<const char*> instanceExtensions(extCount);
	if (!SDL_Vulkan_GetInstanceExtensions(window, &extCount, instanceExtensions.data()))
	{
		std::cout << SDL_GetError();
		exit(-1);
	}
	auto const appInfo = vk::ApplicationInfo()
		.setPApplicationName("VulkanMonkey3D")
		.setApplicationVersion(0)
		.setPEngineName("VulkanMonkey3D")
		.setEngineVersion(0)
		.setApiVersion(VK_MAKE_VERSION(1, 1, 0));
	auto const instInfo = vk::InstanceCreateInfo()
		.setPApplicationInfo(&appInfo)
		.setEnabledLayerCount(0)
		.setPpEnabledLayerNames(nullptr)
		.setEnabledExtensionCount((uint32_t)(instanceExtensions.size()))
		.setPpEnabledExtensionNames(instanceExtensions.data())
		.setPNext(nullptr);
	VkCheck(vk::createInstance(&instInfo, nullptr, &_instance));
	std::cout << "Instance created\n";

	return _instance;
}

Surface Context::createSurface()
{
	VkSurfaceKHR _vkSurface;
	if (!SDL_Vulkan_CreateSurface(window, VkInstance(instance), &_vkSurface))
	{
		std::cout << SDL_GetError();
		exit(-2);
	}
	Surface _surface;
	int width, height;
	SDL_GL_GetDrawableSize(window, &width, &height);
	_surface.actualExtent = { static_cast<uint32_t>(width), static_cast<uint32_t>(height) };
	_surface.surface = vk::SurfaceKHR(_vkSurface);

	return _surface;
}

int Context::getGraphicsFamilyId()
{
	uint32_t gpuCount = 0;
	instance.enumeratePhysicalDevices(&gpuCount, nullptr);
	std::vector<vk::PhysicalDevice> gpuList(gpuCount);
	instance.enumeratePhysicalDevices(&gpuCount, gpuList.data());

	for (const auto& gpu : gpuList) {
		uint32_t familyCount = 0;
		gpu.getQueueFamilyProperties(&familyCount, nullptr);
		std::vector<vk::QueueFamilyProperties> properties(familyCount);
		gpu.getQueueFamilyProperties(&familyCount, properties.data());

		for (uint32_t i = 0; i < familyCount; ++i) {
			//find graphics queue family index
			if (properties[i].queueFlags & vk::QueueFlagBits::eGraphics)
				return i;
		}
	}
	return -1;
}

int Context::getPresentFamilyId()
{
	uint32_t gpuCount = 0;
	instance.enumeratePhysicalDevices(&gpuCount, nullptr);
	std::vector<vk::PhysicalDevice> gpuList(gpuCount);
	instance.enumeratePhysicalDevices(&gpuCount, gpuList.data());

	for (const auto& gpu : gpuList) {
		uint32_t familyCount = 0;
		gpu.getQueueFamilyProperties(&familyCount, nullptr);
		std::vector<vk::QueueFamilyProperties> properties(familyCount);
		gpu.getQueueFamilyProperties(&familyCount, properties.data());

		for (uint32_t i = 0; i < familyCount; ++i) {
			// find present queue family index
			vk::Bool32 presentSupport = false;
			gpu.getSurfaceSupportKHR(i, surface.surface, &presentSupport);
			if (properties[i].queueCount > 0 && presentSupport)
				return i;
		}
	}
	return -1;
}

int Context::getComputeFamilyId()
{
	uint32_t gpuCount = 0;
	instance.enumeratePhysicalDevices(&gpuCount, nullptr);
	std::vector<vk::PhysicalDevice> gpuList(gpuCount);
	instance.enumeratePhysicalDevices(&gpuCount, gpuList.data());

	for (const auto& gpu : gpuList) {
		uint32_t familyCount = 0;
		gpu.getQueueFamilyProperties(&familyCount, nullptr);
		std::vector<vk::QueueFamilyProperties> properties(familyCount);
		gpu.getQueueFamilyProperties(&familyCount, properties.data());

		for (uint32_t i = 0; i < familyCount; ++i) {
			//find compute queue family index
			if (properties[i].queueFlags & vk::QueueFlagBits::eCompute)
				return i;
		}
	}
	return -1;
}

vk::PhysicalDevice Context::findGpu()
{
	uint32_t gpuCount = 0;
	instance.enumeratePhysicalDevices(&gpuCount, nullptr);
	std::vector<vk::PhysicalDevice> gpuList(gpuCount);
	instance.enumeratePhysicalDevices(&gpuCount, gpuList.data());

	for (const auto& gpu : gpuList)
		if (graphicsFamilyId >= 0 && presentFamilyId >= 0 && computeFamilyId >= 0)
			return gpu;

	return nullptr;
}

vk::SurfaceCapabilitiesKHR Context::getSurfaceCapabilities()
{
	vk::SurfaceCapabilitiesKHR _surfaceCapabilities;
	gpu.getSurfaceCapabilitiesKHR(surface.surface, &_surfaceCapabilities);

	return _surfaceCapabilities;
}

vk::SurfaceFormatKHR Context::getSurfaceFormat()
{
	uint32_t formatCount = 0;
	std::vector<vk::SurfaceFormatKHR> formats{};
	gpu.getSurfaceFormatsKHR(surface.surface, &formatCount, nullptr);
	if (formatCount == 0) exit(-5);
	formats.resize(formatCount);
	gpu.getSurfaceFormatsKHR(surface.surface, &formatCount, formats.data());

	for (const auto& i : formats)
		if (i.format == vk::Format::eB8G8R8A8Unorm && i.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear)
			return i;

	return formats[0];
}

vk::PresentModeKHR Context::getPresentationMode()
{
	uint32_t presentCount = 0;
	std::vector<vk::PresentModeKHR> presentModes{};
	gpu.getSurfacePresentModesKHR(surface.surface, &presentCount, nullptr);
	if (presentCount == 0) exit(-5);
	presentModes.resize(presentCount);
	gpu.getSurfacePresentModesKHR(surface.surface, &presentCount, presentModes.data());

	for (const auto& i : presentModes)
		if (i == vk::PresentModeKHR::eImmediate || i == vk::PresentModeKHR::eMailbox)
			return i;

	return vk::PresentModeKHR::eFifo;
}

vk::PhysicalDeviceProperties Context::getGPUProperties()
{
	vk::PhysicalDeviceProperties _gpuProperties;
	gpu.getProperties(&_gpuProperties);

	return _gpuProperties;
}

vk::PhysicalDeviceFeatures Context::getGPUFeatures()
{
	vk::PhysicalDeviceFeatures _gpuFeatures;
	gpu.getFeatures(&_gpuFeatures);

	return _gpuFeatures;
}

vk::Device Context::createDevice()
{
	vk::Device _device;

	uint32_t count;
	gpu.enumerateDeviceExtensionProperties(nullptr, &count, nullptr);
	std::vector<vk::ExtensionProperties> extensionProperties(count);
	gpu.enumerateDeviceExtensionProperties(nullptr, &count, extensionProperties.data());

	std::vector<const char*> deviceExtensions{};
	for (auto& i : extensionProperties) {
		if (std::string(i.extensionName) == VK_KHR_SWAPCHAIN_EXTENSION_NAME && i.specVersion < 110)
			deviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
	}

	float priorities[]{ 1.0f }; // range : [0.0, 1.0]
	auto const queueCreateInfo = vk::DeviceQueueCreateInfo()
		.setQueueFamilyIndex(graphicsFamilyId)
		.setQueueCount(1)
		.setPQueuePriorities(priorities);
	auto const deviceCreateInfo = vk::DeviceCreateInfo()
		.setQueueCreateInfoCount(1)
		.setPQueueCreateInfos(&queueCreateInfo)
		.setEnabledLayerCount(0)
		.setPpEnabledLayerNames(nullptr)
		.setEnabledExtensionCount((uint32_t)deviceExtensions.size())
		.setPpEnabledExtensionNames(deviceExtensions.data())
		.setPEnabledFeatures(&gpuFeatures);

	VkCheck(gpu.createDevice(&deviceCreateInfo, nullptr, &_device));
	std::cout << "Device created\n";

	return _device;
}

vk::Queue Context::getGraphicsQueue()
{
	vk::Queue _graphicsQueue = device.getQueue(graphicsFamilyId, 0);
	if (!_graphicsQueue) exit(-23);

	return _graphicsQueue;
}

vk::Queue Context::getPresentQueue()
{
	vk::Queue _presentQueue = device.getQueue(presentFamilyId, 0);
	if (!_presentQueue) exit(-23);

	return _presentQueue;
}

vk::Queue Context::getComputeQueue()
{
	vk::Queue _computeQueue = device.getQueue(computeFamilyId, 0);
	if (!_computeQueue) exit(-23);

	return _computeQueue;
}

Swapchain Context::createSwapchain()
{
	Swapchain _swapchain;
	VkExtent2D extent = surface.actualExtent;

	uint32_t swapchainImageCount = surface.capabilities.minImageCount + 1;
	if (surface.capabilities.maxImageCount > 0 &&
		swapchainImageCount > surface.capabilities.maxImageCount) {
		swapchainImageCount = surface.capabilities.maxImageCount;
	}

	vk::SwapchainKHR oldSwapchain = _swapchain.swapchain;
	auto const swapchainCreateInfo = vk::SwapchainCreateInfoKHR()
		.setSurface(surface.surface)
		.setMinImageCount(swapchainImageCount)
		.setImageFormat(surface.formatKHR.format)
		.setImageColorSpace(surface.formatKHR.colorSpace)
		.setImageExtent(extent)
		.setImageArrayLayers(1)
		.setImageUsage(vk::ImageUsageFlagBits::eColorAttachment)
		.setPreTransform(surface.capabilities.currentTransform)
		.setCompositeAlpha(vk::CompositeAlphaFlagBitsKHR::eOpaque)
		.setPresentMode(surface.presentModeKHR)
		.setClipped(VK_TRUE)
		.setOldSwapchain(oldSwapchain);

	// new swapchain with old create info
	vk::SwapchainKHR newSwapchain;
	VkCheck(device.createSwapchainKHR(&swapchainCreateInfo, nullptr, &newSwapchain));
	std::cout << "Swapchain created\n";

	if (_swapchain.swapchain)
		device.destroySwapchainKHR(_swapchain.swapchain);
	_swapchain.swapchain = newSwapchain;

	// get the swapchain image handlers
	std::vector<vk::Image> images{};
	device.getSwapchainImagesKHR(_swapchain.swapchain, &swapchainImageCount, nullptr);
	images.resize(swapchainImageCount);
	device.getSwapchainImagesKHR(_swapchain.swapchain, &swapchainImageCount, images.data());

	_swapchain.images.resize(images.size());
	for (unsigned i = 0; i < images.size(); i++)
		_swapchain.images[i].image = images[i]; // hold the image handlers

	// create image views for each swapchain image
	for (auto &image : _swapchain.images) {
		auto const imageViewCreateInfo = vk::ImageViewCreateInfo()
			.setImage(image.image)
			.setViewType(vk::ImageViewType::e2D)
			.setFormat(surface.formatKHR.format)
			.setComponents(vk::ComponentMapping()
				.setR(vk::ComponentSwizzle::eIdentity)
				.setG(vk::ComponentSwizzle::eIdentity)
				.setB(vk::ComponentSwizzle::eIdentity)
				.setA(vk::ComponentSwizzle::eIdentity))
			.setSubresourceRange(vk::ImageSubresourceRange()
				.setAspectMask(vk::ImageAspectFlagBits::eColor)
				.setBaseMipLevel(0)
				.setLevelCount(1)
				.setBaseArrayLayer(0)
				.setLayerCount(1));
		VkCheck(device.createImageView(&imageViewCreateInfo, nullptr, &image.view));
		std::cout << "Swapchain ImageView created\n";
	}

	return _swapchain;
}

vk::CommandPool Context::createCommandPool()
{
	vk::CommandPool _commandPool;
	auto const cpci = vk::CommandPoolCreateInfo()
		.setQueueFamilyIndex(graphicsFamilyId)
		.setFlags(vk::CommandPoolCreateFlagBits::eTransient | vk::CommandPoolCreateFlagBits::eResetCommandBuffer);
	VkCheck(device.createCommandPool(&cpci, nullptr, &_commandPool));
	std::cout << "CommandPool created\n";
	return _commandPool;
}

vk::CommandPool Context::createComputeCommadPool()
{
	vk::CommandPool _commandPool;
	auto const cpci = vk::CommandPoolCreateInfo()
		.setQueueFamilyIndex(computeFamilyId)
		.setFlags(vk::CommandPoolCreateFlagBits::eTransient | vk::CommandPoolCreateFlagBits::eResetCommandBuffer);
	VkCheck(device.createCommandPool(&cpci, nullptr, &_commandPool));
	std::cout << "Compute CommandPool created\n";
	return _commandPool;;
}

std::map<std::string, Image> Context::createRenderTargets(std::vector<std::tuple<std::string, vk::Format>> RTtuples)
{
	std::map<std::string, Image> RT;
	for (auto& t : RTtuples)
	{
		RT[std::get<0>(t)].format = std::get<1>(t);
		RT[std::get<0>(t)].initialLayout = vk::ImageLayout::eUndefined;
		RT[std::get<0>(t)].createImage(device, gpu, surface.actualExtent.width, surface.actualExtent.height, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled, vk::MemoryPropertyFlagBits::eDeviceLocal);
		RT[std::get<0>(t)].createImageView(device, vk::ImageAspectFlagBits::eColor);
		RT[std::get<0>(t)].createSampler(device);
	}
	return RT;
}

vk::RenderPass Context::createRenderPass()
{
	vk::RenderPass _renderPass;
	std::array<vk::AttachmentDescription, 4> attachments = {};

	// for Multisampling
	attachments[0] = vk::AttachmentDescription() // color attachment disc
		.setFormat(surface.formatKHR.format)
		.setSamples(sampleCount)
		.setLoadOp(vk::AttachmentLoadOp::eClear)
		.setStoreOp(vk::AttachmentStoreOp::eStore)
		.setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
		.setStencilStoreOp(vk::AttachmentStoreOp::eDontCare)
		.setInitialLayout(vk::ImageLayout::eUndefined)
		.setFinalLayout(vk::ImageLayout::eColorAttachmentOptimal);

	// the multisampled image will be resolved to this image and presented to swapchain
	attachments[1] = vk::AttachmentDescription() // color attachment disc
		.setFormat(surface.formatKHR.format)
		.setSamples(vk::SampleCountFlagBits::e1)
		.setLoadOp(vk::AttachmentLoadOp::eDontCare)
		.setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
		.setStencilStoreOp(vk::AttachmentStoreOp::eDontCare)
		.setStoreOp(vk::AttachmentStoreOp::eStore)
		.setInitialLayout(vk::ImageLayout::eUndefined)
		.setFinalLayout(vk::ImageLayout::ePresentSrcKHR);

	// multisampled depth
	attachments[2] = vk::AttachmentDescription()
		.setFormat(depth.format)
		.setSamples(sampleCount)
		.setLoadOp(vk::AttachmentLoadOp::eClear)
		.setStoreOp(vk::AttachmentStoreOp::eStore)
		.setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
		.setStencilStoreOp(vk::AttachmentStoreOp::eStore)
		.setInitialLayout(vk::ImageLayout::eUndefined)
		.setFinalLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal);

	// Depth resolve
	attachments[3] = vk::AttachmentDescription()
		.setFormat(depth.format)
		.setSamples(vk::SampleCountFlagBits::e1)
		.setLoadOp(vk::AttachmentLoadOp::eDontCare)
		.setStoreOp(vk::AttachmentStoreOp::eStore)
		.setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
		.setStencilStoreOp(vk::AttachmentStoreOp::eStore)
		.setInitialLayout(vk::ImageLayout::eUndefined)
		.setFinalLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal);

	auto const colorRef = vk::AttachmentReference() // color attachment ref
		.setAttachment(0)
		.setLayout(vk::ImageLayout::eColorAttachmentOptimal);

	auto const resolveRef = vk::AttachmentReference()
		.setAttachment(1)
		.setLayout(vk::ImageLayout::eColorAttachmentOptimal);

	auto const depthAttachmentRef = vk::AttachmentReference()
		.setAttachment(2)
		.setLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal);

	auto const subpassDesc = vk::SubpassDescription() // subpass desc (there can be multiple subpasses)
		.setPipelineBindPoint(vk::PipelineBindPoint::eGraphics)
		.setColorAttachmentCount(1)
		.setPColorAttachments(&colorRef)
		.setPResolveAttachments(&resolveRef)
		.setPDepthStencilAttachment(&depthAttachmentRef);

	auto const rpci = vk::RenderPassCreateInfo()
		.setAttachmentCount(static_cast<uint32_t>(attachments.size()))
		.setPAttachments(attachments.data())
		.setSubpassCount(1)
		.setPSubpasses(&subpassDesc);

	VkCheck(device.createRenderPass(&rpci, nullptr, &_renderPass));
	std::cout << "RenderPass created\n";

	return _renderPass;
}

vk::RenderPass Context::createDeferredRenderPass()
{
	vk::RenderPass _renderPass;
	std::array<vk::AttachmentDescription, 5> attachments{};
	// Deferred targets
	// Position
	attachments[0].format = renderTargets["position"].format;
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
	// Specular
	attachments[3].format = renderTargets["specular"].format;
	attachments[3].samples = vk::SampleCountFlagBits::e1;
	attachments[3].loadOp = vk::AttachmentLoadOp::eClear;
	attachments[3].storeOp = vk::AttachmentStoreOp::eStore;
	attachments[3].stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
	attachments[3].stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
	attachments[3].initialLayout = vk::ImageLayout::eUndefined;
	attachments[3].finalLayout = vk::ImageLayout::eColorAttachmentOptimal;

	// Depth
	attachments[4].format = info->depth.format;
	attachments[4].samples = vk::SampleCountFlagBits::e1;
	attachments[4].loadOp = vk::AttachmentLoadOp::eClear;
	attachments[4].storeOp = vk::AttachmentStoreOp::eDontCare;
	attachments[4].stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
	attachments[4].stencilStoreOp = vk::AttachmentStoreOp::eStore;
	attachments[4].initialLayout = vk::ImageLayout::eUndefined;
	attachments[4].finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

	std::array<vk::SubpassDescription, 1> subpassDescriptions{};

	std::vector<vk::AttachmentReference> colorReferences{
		{ 0, vk::ImageLayout::eColorAttachmentOptimal },
		{ 1, vk::ImageLayout::eColorAttachmentOptimal },
		{ 2, vk::ImageLayout::eColorAttachmentOptimal },
		{ 3, vk::ImageLayout::eColorAttachmentOptimal }
	};
	vk::AttachmentReference depthReference = { 4, vk::ImageLayout::eDepthStencilAttachmentOptimal };

	subpassDescriptions[0].pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
	subpassDescriptions[0].colorAttachmentCount = static_cast<uint32_t>(colorReferences.size());
	subpassDescriptions[0].pColorAttachments = colorReferences.data();
	subpassDescriptions[0].pDepthStencilAttachment = &depthReference;


	// Subpass dependencies for layout transitions
	std::vector<vk::SubpassDependency> dependencies{
		vk::SubpassDependency{
			VK_SUBPASS_EXTERNAL,									// uint32_t srcSubpass;
			0,														// uint32_t dstSubpass;
			vk::PipelineStageFlagBits::eBottomOfPipe,				// PipelineStageFlags srcStageMask;
			vk::PipelineStageFlagBits::eColorAttachmentOutput,		// PipelineStageFlags dstStageMask;
			vk::AccessFlagBits::eMemoryRead,						// AccessFlags srcAccessMask;
			vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite,	// AccessFlags dstAccessMask;
			vk::DependencyFlagBits::eByRegion						// DependencyFlags dependencyFlags;
		},
		vk::SubpassDependency{
			0,														// uint32_t srcSubpass;
			VK_SUBPASS_EXTERNAL,									// uint32_t dstSubpass;
			vk::PipelineStageFlagBits::eColorAttachmentOutput,		// PipelineStageFlags srcStageMask;
			vk::PipelineStageFlagBits::eBottomOfPipe,				// PipelineStageFlags dstStageMask;
			vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite,	// AccessFlags srcAccessMask;
			vk::AccessFlagBits::eMemoryRead,						// AccessFlags dstAccessMask;
			vk::DependencyFlagBits::eByRegion						// DependencyFlags dependencyFlags;
		},
	};

	vk::RenderPassCreateInfo renderPassInfo = {};
	renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
	renderPassInfo.pAttachments = attachments.data();
	renderPassInfo.subpassCount = static_cast<uint32_t>(subpassDescriptions.size());
	renderPassInfo.pSubpasses = subpassDescriptions.data();
	renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
	renderPassInfo.pDependencies = dependencies.data();

	VkCheck(info->device.createRenderPass(&renderPassInfo, nullptr, &_renderPass));

	return _renderPass;
}

vk::RenderPass Context::createCompositionRenderPass()
{
	vk::RenderPass _renderPass;

	std::array<vk::AttachmentDescription, 1> attachments{};
	// Color target
	attachments[0].format = info->surface.formatKHR.format;
	attachments[0].samples = vk::SampleCountFlagBits::e1;
	attachments[0].loadOp = vk::AttachmentLoadOp::eClear;
	attachments[0].storeOp = vk::AttachmentStoreOp::eStore;
	attachments[0].stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
	attachments[0].stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
	attachments[0].initialLayout = vk::ImageLayout::eUndefined;
	attachments[0].finalLayout = vk::ImageLayout::ePresentSrcKHR;

	std::array<vk::SubpassDescription, 1> subpassDescriptions{};

	std::vector<vk::AttachmentReference> colorReferences{ { 0, vk::ImageLayout::eColorAttachmentOptimal } };

	subpassDescriptions[0].pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
	subpassDescriptions[0].colorAttachmentCount = static_cast<uint32_t>(colorReferences.size());
	subpassDescriptions[0].pColorAttachments = colorReferences.data();
	subpassDescriptions[0].pDepthStencilAttachment = nullptr;


	// Subpass dependencies for layout transitions
	std::vector<vk::SubpassDependency> dependencies{
		vk::SubpassDependency{
			VK_SUBPASS_EXTERNAL,									// uint32_t srcSubpass;
			0,														// uint32_t dstSubpass;
			vk::PipelineStageFlagBits::eBottomOfPipe,				// PipelineStageFlags srcStageMask;
			vk::PipelineStageFlagBits::eColorAttachmentOutput,		// PipelineStageFlags dstStageMask;
			vk::AccessFlagBits::eMemoryRead,						// AccessFlags srcAccessMask;
			vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite,	// AccessFlags dstAccessMask;
			vk::DependencyFlagBits::eByRegion						// DependencyFlags dependencyFlags;
		},
		vk::SubpassDependency{
			0,														// uint32_t srcSubpass;
			VK_SUBPASS_EXTERNAL,									// uint32_t dstSubpass;
			vk::PipelineStageFlagBits::eColorAttachmentOutput,		// PipelineStageFlags srcStageMask;
			vk::PipelineStageFlagBits::eBottomOfPipe,				// PipelineStageFlags dstStageMask;
			vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite,	// AccessFlags srcAccessMask;
			vk::AccessFlagBits::eMemoryRead,						// AccessFlags dstAccessMask;
			vk::DependencyFlagBits::eByRegion						// DependencyFlags dependencyFlags;
		},
	};

	vk::RenderPassCreateInfo renderPassInfo = {};
	renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
	renderPassInfo.pAttachments = attachments.data();
	renderPassInfo.subpassCount = static_cast<uint32_t>(subpassDescriptions.size());
	renderPassInfo.pSubpasses = subpassDescriptions.data();
	renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
	renderPassInfo.pDependencies = dependencies.data();

	VkCheck(info->device.createRenderPass(&renderPassInfo, nullptr, &_renderPass));

	return _renderPass;
}

vk::RenderPass Context::createSSAORenderPass()
{
	vk::RenderPass _renderPass;

	std::array<vk::AttachmentDescription, 1> attachments{};
	// Color attachment
	attachments[0].format = info->renderTargets["ssao"].format;
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
	std::vector<vk::SubpassDependency> dependencies{
		vk::SubpassDependency{
			VK_SUBPASS_EXTERNAL,									// uint32_t srcSubpass;
			0,														// uint32_t dstSubpass;
			vk::PipelineStageFlagBits::eBottomOfPipe,				// PipelineStageFlags srcStageMask;
			vk::PipelineStageFlagBits::eColorAttachmentOutput,		// PipelineStageFlags dstStageMask;
			vk::AccessFlagBits::eMemoryRead,						// AccessFlags srcAccessMask;
			vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite,	// AccessFlags dstAccessMask;
			vk::DependencyFlagBits::eByRegion						// DependencyFlags dependencyFlags;
		},
		vk::SubpassDependency{
			0,														// uint32_t srcSubpass;
			VK_SUBPASS_EXTERNAL,									// uint32_t dstSubpass;
			vk::PipelineStageFlagBits::eColorAttachmentOutput,		// PipelineStageFlags srcStageMask;
			vk::PipelineStageFlagBits::eBottomOfPipe,				// PipelineStageFlags dstStageMask;
			vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite,	// AccessFlags srcAccessMask;
			vk::AccessFlagBits::eMemoryRead,						// AccessFlags dstAccessMask;
			vk::DependencyFlagBits::eByRegion						// DependencyFlags dependencyFlags;
		}
	};

	vk::RenderPassCreateInfo renderPassInfo = {};
	renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
	renderPassInfo.pAttachments = attachments.data();
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpassDescription;
	renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
	renderPassInfo.pDependencies = dependencies.data();

	VkCheck(info->device.createRenderPass(&renderPassInfo, nullptr, &_renderPass));

	return _renderPass;
}

vk::RenderPass Context::createSSAOBlurRenderPass()
{
	vk::RenderPass _renderPass;

	std::array<vk::AttachmentDescription, 1> attachments{};
	// Color attachment
	attachments[0].format = info->renderTargets["ssaoBlur"].format;
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
	std::vector<vk::SubpassDependency> dependencies{
		vk::SubpassDependency{
			VK_SUBPASS_EXTERNAL,									// uint32_t srcSubpass;
			0,														// uint32_t dstSubpass;
			vk::PipelineStageFlagBits::eBottomOfPipe,				// PipelineStageFlags srcStageMask;
			vk::PipelineStageFlagBits::eColorAttachmentOutput,		// PipelineStageFlags dstStageMask;
			vk::AccessFlagBits::eMemoryRead,						// AccessFlags srcAccessMask;
			vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite,	// AccessFlags dstAccessMask;
			vk::DependencyFlagBits::eByRegion						// DependencyFlags dependencyFlags;
		},
		vk::SubpassDependency{
			0,														// uint32_t srcSubpass;
			VK_SUBPASS_EXTERNAL,									// uint32_t dstSubpass;
			vk::PipelineStageFlagBits::eColorAttachmentOutput,		// PipelineStageFlags srcStageMask;
			vk::PipelineStageFlagBits::eBottomOfPipe,				// PipelineStageFlags dstStageMask;
			vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite,	// AccessFlags srcAccessMask;
			vk::AccessFlagBits::eMemoryRead,						// AccessFlags dstAccessMask;
			vk::DependencyFlagBits::eByRegion						// DependencyFlags dependencyFlags;
		}
	};

	vk::RenderPassCreateInfo renderPassInfo = {};
	renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
	renderPassInfo.pAttachments = attachments.data();
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpassDescription;
	renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
	renderPassInfo.pDependencies = dependencies.data();

	VkCheck(info->device.createRenderPass(&renderPassInfo, nullptr, &_renderPass));

	return _renderPass;
}

vk::RenderPass Context::createReflectionRenderPass()
{
	vk::RenderPass _renderPass;

	std::array<vk::AttachmentDescription, 1> attachments{};
	// Color attachment
	attachments[0].format = info->surface.formatKHR.format;
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
	std::vector<vk::SubpassDependency> dependencies{
		vk::SubpassDependency{
			VK_SUBPASS_EXTERNAL,									// uint32_t srcSubpass;
			0,														// uint32_t dstSubpass;
			vk::PipelineStageFlagBits::eBottomOfPipe,				// PipelineStageFlags srcStageMask;
			vk::PipelineStageFlagBits::eColorAttachmentOutput,		// PipelineStageFlags dstStageMask;
			vk::AccessFlagBits::eMemoryRead,						// AccessFlags srcAccessMask;
			vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite,	// AccessFlags dstAccessMask;
			vk::DependencyFlagBits::eByRegion						// DependencyFlags dependencyFlags;
		},
		vk::SubpassDependency{
			0,														// uint32_t srcSubpass;
			VK_SUBPASS_EXTERNAL,									// uint32_t dstSubpass;
			vk::PipelineStageFlagBits::eColorAttachmentOutput,		// PipelineStageFlags srcStageMask;
			vk::PipelineStageFlagBits::eBottomOfPipe,				// PipelineStageFlags dstStageMask;
			vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite,	// AccessFlags srcAccessMask;
			vk::AccessFlagBits::eMemoryRead,						// AccessFlags dstAccessMask;
			vk::DependencyFlagBits::eByRegion						// DependencyFlags dependencyFlags;
		}
	};

	vk::RenderPassCreateInfo renderPassInfo = {};
	renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
	renderPassInfo.pAttachments = attachments.data();
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpassDescription;
	renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
	renderPassInfo.pDependencies = dependencies.data();

	VkCheck(info->device.createRenderPass(&renderPassInfo, nullptr, &_renderPass));

	return _renderPass;
}

vk::RenderPass Context::createGUIRenderPass()
{
	vk::RenderPass _renderPass;

	std::array<vk::AttachmentDescription, 1> attachments{};
	// Color attachment
	attachments[0].format = info->surface.formatKHR.format;
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
	std::vector<vk::SubpassDependency> dependencies{
		vk::SubpassDependency{
			VK_SUBPASS_EXTERNAL,									// uint32_t srcSubpass;
			0,														// uint32_t dstSubpass;
			vk::PipelineStageFlagBits::eBottomOfPipe,				// PipelineStageFlags srcStageMask;
			vk::PipelineStageFlagBits::eColorAttachmentOutput,		// PipelineStageFlags dstStageMask;
			vk::AccessFlagBits::eMemoryRead,						// AccessFlags srcAccessMask;
			vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite,	// AccessFlags dstAccessMask;
			vk::DependencyFlagBits::eByRegion						// DependencyFlags dependencyFlags;
		},
		vk::SubpassDependency{
			0,														// uint32_t srcSubpass;
			VK_SUBPASS_EXTERNAL,									// uint32_t dstSubpass;
			vk::PipelineStageFlagBits::eColorAttachmentOutput,		// PipelineStageFlags srcStageMask;
			vk::PipelineStageFlagBits::eBottomOfPipe,				// PipelineStageFlags dstStageMask;
			vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite,	// AccessFlags srcAccessMask;
			vk::AccessFlagBits::eMemoryRead,						// AccessFlags dstAccessMask;
			vk::DependencyFlagBits::eByRegion						// DependencyFlags dependencyFlags;
		}
	};

	vk::RenderPassCreateInfo renderPassInfo = {};
	renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
	renderPassInfo.pAttachments = attachments.data();
	renderPassInfo.subpassCount = static_cast<uint32_t>(subpassDescriptions.size());
	renderPassInfo.pSubpasses = subpassDescriptions.data();
	renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
	renderPassInfo.pDependencies = dependencies.data();

	VkCheck(info->device.createRenderPass(&renderPassInfo, nullptr, &_renderPass));

	return _renderPass;
}

Image Context::createDepthResources()
{
	Image _image;
	_image.format = vk::Format::eUndefined;
	std::vector<vk::Format> candidates = { vk::Format::eD32SfloatS8Uint, vk::Format::eD32Sfloat, vk::Format::eD24UnormS8Uint };
	for (auto &format : candidates) {
		vk::FormatProperties props = gpu.getFormatProperties(format);
		if ((props.optimalTilingFeatures & vk::FormatFeatureFlagBits::eDepthStencilAttachment) == vk::FormatFeatureFlagBits::eDepthStencilAttachment) {
			_image.format = format;
			break;
		}
	}
	if (_image.format == vk::Format::eUndefined)
	{
		std::cout << "No suitable format found!\n";
		exit(-9);
	}
	_image.createImage(device, gpu, surface.actualExtent.width, surface.actualExtent.height,
		vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eDepthStencilAttachment, vk::MemoryPropertyFlagBits::eDeviceLocal);
	_image.createImageView(device, vk::ImageAspectFlagBits::eDepth);

	_image.addressMode = vk::SamplerAddressMode::eClampToEdge;
	_image.maxAnisotropy = 1.f;
	_image.borderColor = vk::BorderColor::eFloatOpaqueWhite;
	_image.samplerCompareEnable = VK_TRUE;
	_image.createSampler(device);

	_image.transitionImageLayout(device, commandPool, graphicsQueue, vk::ImageLayout::eUndefined, vk::ImageLayout::eDepthStencilAttachmentOptimal);

	return _image;
}

std::vector<vk::Framebuffer> Context::createFrameBuffers()
{
	assert((VkSampleCountFlags(gpuProperties.limits.framebufferColorSampleCounts) >= VkSampleCountFlags(sampleCount))
		&& (VkSampleCountFlags(gpuProperties.limits.framebufferDepthSampleCounts) >= VkSampleCountFlags(sampleCount)));

	forward.MSColorImage.format = surface.formatKHR.format;
	forward.MSColorImage.createImage(device, gpu, surface.actualExtent.width, surface.actualExtent.height, vk::ImageTiling::eOptimal,
		vk::ImageUsageFlagBits::eTransientAttachment | vk::ImageUsageFlagBits::eColorAttachment, vk::MemoryPropertyFlagBits::eLazilyAllocated, sampleCount);
	forward.MSColorImage.createImageView(device, vk::ImageAspectFlagBits::eColor);

	forward.MSDepthImage.format = depth.format;
	forward.MSDepthImage.createImage(device, gpu, surface.actualExtent.width, surface.actualExtent.height, vk::ImageTiling::eOptimal,
		vk::ImageUsageFlagBits::eTransientAttachment | vk::ImageUsageFlagBits::eDepthStencilAttachment, vk::MemoryPropertyFlagBits::eLazilyAllocated, sampleCount);
	forward.MSDepthImage.createImageView(device, vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil);

	std::vector<vk::Framebuffer> _frameBuffers(swapchain.images.size());

	for (size_t i = 0; i < _frameBuffers.size(); ++i) {
		std::array<vk::ImageView, 4> attachments = {
			forward.MSColorImage.view,
			swapchain.images[i].view,
			forward.MSDepthImage.view,
			depth.view
		};

		auto const fbci = vk::FramebufferCreateInfo()
			.setRenderPass(forward.forwardRenderPass)
			.setAttachmentCount(static_cast<uint32_t>(attachments.size()))
			.setPAttachments(attachments.data())
			.setWidth(surface.actualExtent.width)
			.setHeight(surface.actualExtent.height)
			.setLayers(1);
		VkCheck(device.createFramebuffer(&fbci, nullptr, &_frameBuffers[i]));
		std::cout << "Framebuffer created\n";
	}

	return _frameBuffers;
}

std::vector<vk::Framebuffer> Context::createDeferredFrameBuffers()
{
	std::vector<vk::Framebuffer> _frameBuffers(swapchain.images.size());

	for (size_t i = 0; i < _frameBuffers.size(); ++i) {
		std::vector<vk::ImageView> attachments = {
			renderTargets["position"].view,
			renderTargets["normal"].view,
			renderTargets["albedo"].view,
			renderTargets["specular"].view,
			depth.view
		};

		auto const fbci = vk::FramebufferCreateInfo()
			.setRenderPass(deferred.deferredRenderPass)
			.setAttachmentCount(static_cast<uint32_t>(attachments.size()))
			.setPAttachments(attachments.data())
			.setWidth(surface.actualExtent.width)
			.setHeight(surface.actualExtent.height)
			.setLayers(1);
		VkCheck(device.createFramebuffer(&fbci, nullptr, &_frameBuffers[i]));
		std::cout << "Framebuffer created\n";
	}

	return _frameBuffers;
}

std::vector<vk::Framebuffer> Context::createCompositionFrameBuffers()
{
	std::vector<vk::Framebuffer> _frameBuffers(swapchain.images.size());

	for (size_t i = 0; i < _frameBuffers.size(); ++i) {
		std::vector<vk::ImageView> attachments = {
			swapchain.images[i].view
		};

		auto const fbci = vk::FramebufferCreateInfo()
			.setRenderPass(deferred.compositionRenderPass)
			.setAttachmentCount(static_cast<uint32_t>(attachments.size()))
			.setPAttachments(attachments.data())
			.setWidth(surface.actualExtent.width)
			.setHeight(surface.actualExtent.height)
			.setLayers(1);
		VkCheck(device.createFramebuffer(&fbci, nullptr, &_frameBuffers[i]));
		std::cout << "Framebuffer created\n";
	}

	return _frameBuffers;
}

std::vector<vk::Framebuffer> Context::createReflectionFrameBuffers()
{
	std::vector<vk::Framebuffer> _frameBuffers(swapchain.images.size());

	for (size_t i = 0; i < _frameBuffers.size(); ++i) {
		std::vector<vk::ImageView> attachments = {
			swapchain.images[i].view
		};

		auto const fbci = vk::FramebufferCreateInfo()
			.setRenderPass(ssr.ssrRenderPass)
			.setAttachmentCount(static_cast<uint32_t>(attachments.size()))
			.setPAttachments(attachments.data())
			.setWidth(surface.actualExtent.width)
			.setHeight(surface.actualExtent.height)
			.setLayers(1);
		VkCheck(device.createFramebuffer(&fbci, nullptr, &_frameBuffers[i]));
		std::cout << "Framebuffer created\n";
	}

	return _frameBuffers;
}

std::vector<vk::Framebuffer> Context::createSSAOFrameBuffers()
{
	std::vector<vk::Framebuffer> _frameBuffers(swapchain.images.size());

	for (size_t i = 0; i < _frameBuffers.size(); ++i) {
		std::vector<vk::ImageView> attachments = {
			renderTargets["ssao"].view
		};

		auto const fbci = vk::FramebufferCreateInfo()
			.setRenderPass(ssao.ssaoRenderPass)
			.setAttachmentCount(static_cast<uint32_t>(attachments.size()))
			.setPAttachments(attachments.data())
			.setWidth(surface.actualExtent.width)
			.setHeight(surface.actualExtent.height)
			.setLayers(1);
		VkCheck(device.createFramebuffer(&fbci, nullptr, &_frameBuffers[i]));
		std::cout << "Framebuffer created\n";
	}

	return _frameBuffers;
}

std::vector<vk::Framebuffer> Context::createSSAOBlurFrameBuffers()
{
	std::vector<vk::Framebuffer> _frameBuffers(swapchain.images.size());

	for (size_t i = 0; i < _frameBuffers.size(); ++i) {
		std::vector<vk::ImageView> attachments = {
			renderTargets["ssaoBlur"].view
		};

		auto const fbci = vk::FramebufferCreateInfo()
			.setRenderPass(ssao.ssaoRenderPass)
			.setAttachmentCount(static_cast<uint32_t>(attachments.size()))
			.setPAttachments(attachments.data())
			.setWidth(surface.actualExtent.width)
			.setHeight(surface.actualExtent.height)
			.setLayers(1);
		VkCheck(device.createFramebuffer(&fbci, nullptr, &_frameBuffers[i]));
		std::cout << "Framebuffer created\n";
	}

	return _frameBuffers;
}

std::vector<vk::Framebuffer> Context::createGUIFrameBuffers()
{
	std::vector<vk::Framebuffer> _frameBuffers(swapchain.images.size());

	for (size_t i = 0; i < _frameBuffers.size(); ++i) {
		std::vector<vk::ImageView> attachments = {
			swapchain.images[i].view
		};

		auto const fbci = vk::FramebufferCreateInfo()
			.setRenderPass(gui.guiRenderPass)
			.setAttachmentCount(static_cast<uint32_t>(attachments.size()))
			.setPAttachments(attachments.data())
			.setWidth(surface.actualExtent.width)
			.setHeight(surface.actualExtent.height)
			.setLayers(1);
		VkCheck(device.createFramebuffer(&fbci, nullptr, &_frameBuffers[i]));
		std::cout << "Framebuffer created\n";
	}

	return _frameBuffers;
}

std::vector<vk::CommandBuffer> Context::createCmdBuffers(const uint32_t bufferCount)
{
	std::vector<vk::CommandBuffer> _cmdBuffers(bufferCount);
	auto const cbai = vk::CommandBufferAllocateInfo()
		.setCommandPool(commandPool)
		.setLevel(vk::CommandBufferLevel::ePrimary)
		.setCommandBufferCount(bufferCount);
	VkCheck(device.allocateCommandBuffers(&cbai, _cmdBuffers.data()));
	std::cout << "Command Buffers allocated\n";

	return _cmdBuffers;
}

vk::CommandBuffer Context::createCmdBuffer()
{
	vk::CommandBuffer _cmdBuffer;
	auto const cbai = vk::CommandBufferAllocateInfo()
		.setCommandPool(commandPool)
		.setLevel(vk::CommandBufferLevel::ePrimary)
		.setCommandBufferCount(1);
	VkCheck(device.allocateCommandBuffers(&cbai, &_cmdBuffer));
	std::cout << "Command Buffer allocated\n";

	return _cmdBuffer;
}

vk::CommandBuffer Context::createComputeCmdBuffer()
{
	vk::CommandBuffer _cmdBuffer;
	auto const cbai = vk::CommandBufferAllocateInfo()
		.setCommandPool(commandPoolCompute)
		.setLevel(vk::CommandBufferLevel::ePrimary)
		.setCommandBufferCount(1);
	VkCheck(device.allocateCommandBuffers(&cbai, &_cmdBuffer));
	std::cout << "Command Buffer allocated\n";

	return _cmdBuffer;
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

		vsmci = vk::ShaderModuleCreateInfo{
			vk::ShaderModuleCreateFlags(),						// ShaderModuleCreateFlags flags;
			vertCode.size(),									// size_t codeSize;
			reinterpret_cast<const uint32_t*>(vertCode.data())	// const uint32_t* pCode;
		};
		if (specificInfo.shaders.size() > 1) {
			fsmci = vk::ShaderModuleCreateInfo{
				vk::ShaderModuleCreateFlags(),						// ShaderModuleCreateFlags flags;
				fragCode.size(),									// size_t codeSize;
				reinterpret_cast<const uint32_t*>(fragCode.data())	// const uint32_t* pCode;
			};
		}
		VkCheck(device.createShaderModule(&vsmci, nullptr, &vertModule));
		std::cout << "Vertex Shader Module created\n";
		if (specificInfo.shaders.size() > 1) {
			VkCheck(device.createShaderModule(&fsmci, nullptr, &fragModule));
			std::cout << "Fragment Shader Module created\n";
		}
	}
	else
		exit(-22);

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
	_pipeline.pipeinfo.pVertexInputState = &vk::PipelineVertexInputStateCreateInfo{
		vk::PipelineVertexInputStateCreateFlags(),
		(uint32_t)specificInfo.vertexInputBindingDescriptions.size(),
		specificInfo.vertexInputBindingDescriptions.data(),
		(uint32_t)specificInfo.vertexInputAttributeDescriptions.size(),
		specificInfo.vertexInputAttributeDescriptions.data()
	};

	// Input Assembly stage
	_pipeline.pipeinfo.pInputAssemblyState = &vk::PipelineInputAssemblyStateCreateInfo{
		vk::PipelineInputAssemblyStateCreateFlags(),		// PipelineInputAssemblyStateCreateFlags flags;
		vk::PrimitiveTopology::eTriangleList,				// PrimitiveTopology topology;
		VK_FALSE											// Bool32 primitiveRestartEnable;
	};

	// Viewports and Scissors
	_pipeline.pipeinfo.pViewportState = &vk::PipelineViewportStateCreateInfo{
		vk::PipelineViewportStateCreateFlags(),					// PipelineViewportStateCreateFlags flags;
		1,														// uint32_t viewportCount;
		&vk::Viewport{											// const Viewport* pViewports;
			0.0f,													// float x;
			0.0f,													// float y;
			(float)specificInfo.viewportSize.width, 				// float width;
			(float)specificInfo.viewportSize.height,				// float height;
			0.0f,													// float minDepth;
			1.0f },													// float maxDepth;
		1,														// uint32_t scissorCount;
		&vk::Rect2D{											// const Rect2D* pScissors;
			vk::Offset2D(),											// Offset2D offset;
			specificInfo.viewportSize }								// Extent2D extent;
	};

	// Rasterization state
	_pipeline.pipeinfo.pRasterizationState = &vk::PipelineRasterizationStateCreateInfo{
		vk::PipelineRasterizationStateCreateFlags(),	// PipelineRasterizationStateCreateFlags flags;
		VK_FALSE,										// Bool32 depthClampEnable;
		VK_FALSE,										// Bool32 rasterizerDiscardEnable;
		vk::PolygonMode::eFill,							// PolygonMode polygonMode;
		specificInfo.cull,								// CullModeFlags cullMode;
		specificInfo.face,								// FrontFace frontFace;
		VK_FALSE,										// Bool32 depthBiasEnable;
		0.0f,											// float depthBiasConstantFactor;
		0.0f,											// float depthBiasClamp;
		0.0f,											// float depthBiasSlopeFactor;
		1.0f											// float lineWidth;
	};

	// Multisample state
	_pipeline.pipeinfo.pMultisampleState = &vk::PipelineMultisampleStateCreateInfo{
		vk::PipelineMultisampleStateCreateFlags(),	// PipelineMultisampleStateCreateFlags flags;
		specificInfo.sampleCount,					// SampleCountFlagBits rasterizationSamples;
		VK_FALSE,									// Bool32 sampleShadingEnable;
		1.0f,										// float minSampleShading;
		nullptr,									// const SampleMask* pSampleMask;
		VK_FALSE,									// Bool32 alphaToCoverageEnable;
		VK_FALSE									// Bool32 alphaToOneEnable;
	};

	// Depth ans stencil state
	_pipeline.pipeinfo.pDepthStencilState = &vk::PipelineDepthStencilStateCreateInfo{
		vk::PipelineDepthStencilStateCreateFlags{},					// PipelineDepthStencilStateCreateFlags flags;
		VK_TRUE,													// Bool32 depthTestEnable;
		VK_TRUE,													// Bool32 depthWriteEnable;
		specificInfo.compareOp,										// CompareOp depthCompareOp;
		VK_FALSE,													// Bool32 depthBoundsTestEnable;
		VK_FALSE,													// Bool32 stencilTestEnable;
		vk::StencilOpState().setCompareOp(vk::CompareOp::eAlways),	// StencilOpState front;
		vk::StencilOpState().setCompareOp(vk::CompareOp::eAlways),	// StencilOpState back;
		0.0f,														// float minDepthBounds;
		0.0f														// float maxDepthBounds;
	};

	// Color Blending state
	if (specificInfo.useBlendState) {
		_pipeline.pipeinfo.pColorBlendState = &vk::PipelineColorBlendStateCreateInfo{
			vk::PipelineColorBlendStateCreateFlags{},				// PipelineColorBlendStateCreateFlags flags;
			VK_FALSE,												// Bool32 logicOpEnable; // this must be false is we want alpha blend
			vk::LogicOp::eCopy,										// LogicOp logicOp;
			(uint32_t)specificInfo.blendAttachmentStates.size(),	// uint32_t attachmentCount;
			specificInfo.blendAttachmentStates.data(),				// const PipelineColorBlendAttachmentState* pAttachments;
			{0.0f, 0.0f, 0.0f, 0.0f}								// float blendConstants[4];
		};
	}
	else
		_pipeline.pipeinfo.pColorBlendState = nullptr;

	// Dynamic state
	_pipeline.pipeinfo.pDynamicState = specificInfo.dynamicStates.size() > 0 ? &specificInfo.dynamicStateInfo : nullptr;

	// Push Constant Range
	auto pushConstantRange = specificInfo.pushConstantRange;
	uint32_t numOfConsts = pushConstantRange == vk::PushConstantRange() ? 0 : 1;

	// Pipeline Layout
	VkCheck(device.createPipelineLayout(
		&vk::PipelineLayoutCreateInfo{ vk::PipelineLayoutCreateFlags(), (uint32_t)specificInfo.descriptorSetLayouts.size(), specificInfo.descriptorSetLayouts.data(), numOfConsts, &pushConstantRange },
		nullptr,
		&_pipeline.pipeinfo.layout));
	std::cout << "Pipeline Layout created\n";

	// Render Pass
	_pipeline.pipeinfo.renderPass = specificInfo.renderPass;

	// Subpass
	_pipeline.pipeinfo.subpass = specificInfo.subpassTarget;

	// Base Pipeline Handle
	_pipeline.pipeinfo.basePipelineHandle = nullptr;

	// Base Pipeline Index
	_pipeline.pipeinfo.basePipelineIndex = -1;

	VkCheck(device.createGraphicsPipelines(nullptr, 1, &_pipeline.pipeinfo, nullptr, &_pipeline.pipeline));
	std::cout << "Pipeline created\n";

	// destroy Shader Modules
	device.destroyShaderModule(vertModule);
	device.destroyShaderModule(fragModule);

	return _pipeline;
}

Pipeline Context::createCompositionPipeline()
{
	Pipeline _pipeline;

	// Shader stages
	std::vector<char> vertCode{};
	std::vector<char> fragCode{};

	vk::ShaderModuleCreateInfo vsmci;
	vk::ShaderModuleCreateInfo fsmci;

	vk::ShaderModule vertModule;
	vk::ShaderModule fragModule;
	{
		vertCode = readFile("shaders/Deferred/cvert.spv");
		fragCode = readFile("shaders/Deferred/cfrag.spv");

		vsmci = vk::ShaderModuleCreateInfo{
			vk::ShaderModuleCreateFlags(),						// ShaderModuleCreateFlags flags;
			vertCode.size(),									// size_t codeSize;
			reinterpret_cast<const uint32_t*>(vertCode.data())	// const uint32_t* pCode;
		};
		fsmci = vk::ShaderModuleCreateInfo{
			vk::ShaderModuleCreateFlags(),						// ShaderModuleCreateFlags flags;
			fragCode.size(),									// size_t codeSize;
			reinterpret_cast<const uint32_t*>(fragCode.data())	// const uint32_t* pCode;
		};
		VkCheck(device.createShaderModule(&vsmci, nullptr, &vertModule));
		std::cout << "Vertex Shader Module created\n";
		VkCheck(device.createShaderModule(&fsmci, nullptr, &fragModule));
		std::cout << "Fragment Shader Module created\n";
	}

	std::vector<vk::PipelineShaderStageCreateInfo> stages{
		vk::PipelineShaderStageCreateInfo{
			vk::PipelineShaderStageCreateFlags(),	// PipelineShaderStageCreateFlags flags;
			vk::ShaderStageFlagBits::eVertex,		// ShaderStageFlagBits stage;
			vertModule,								// ShaderModule module;
			"main",									// const char* pName;
			nullptr									// const SpecializationInfo* pSpecializationInfo;
		},
		vk::PipelineShaderStageCreateInfo{
			vk::PipelineShaderStageCreateFlags(),	// PipelineShaderStageCreateFlags flags;
			vk::ShaderStageFlagBits::eFragment,		// ShaderStageFlagBits stage;
			fragModule,								// ShaderModule module;
			"main",									// const char* pName;
			&vk::SpecializationInfo{				// const SpecializationInfo* pSpecializationInfo;
				1,										// uint32_t mapEntryCount;
				&vk::SpecializationMapEntry{			// const SpecializationMapEntry* pMapEntries;
					0,										// uint32_t constantID;
					0,										// uint32_t offset;
					sizeof(MAX_LIGHTS) },					// size_t size;
				sizeof(MAX_LIGHTS),						// size_t dataSize;
				&MAX_LIGHTS								// const void* pData;
			}
		}
	};
	_pipeline.pipeinfo.stageCount = static_cast<uint32_t>(stages.size());
	_pipeline.pipeinfo.pStages = stages.data();

	// Vertex Input state
	_pipeline.pipeinfo.pVertexInputState = &vk::PipelineVertexInputStateCreateInfo();

	// Input Assembly stage
	_pipeline.pipeinfo.pInputAssemblyState = &vk::PipelineInputAssemblyStateCreateInfo{
		vk::PipelineInputAssemblyStateCreateFlags(),	// PipelineInputAssemblyStateCreateFlags flags;
		vk::PrimitiveTopology::eTriangleList,			// PrimitiveTopology topology;
		VK_FALSE										// Bool32 primitiveRestartEnable;
	};

	// Viewports and Scissors
	_pipeline.pipeinfo.pViewportState = &vk::PipelineViewportStateCreateInfo{
		vk::PipelineViewportStateCreateFlags(),				// PipelineViewportStateCreateFlags flags;
		1,													// uint32_t viewportCount;
		&vk::Viewport{										// const Viewport* pViewports;
			0.0f,												// float x;
			0.0f,												// float y;
			static_cast<float>(surface.actualExtent.width), 	// float width;
			static_cast<float>(surface.actualExtent.height),	// float height;
			0.0f,												// float minDepth;
			1.0f },												// float maxDepth;
		1,													// uint32_t scissorCount;
		&vk::Rect2D{										// const Rect2D* pScissors;
			vk::Offset2D(),										// Offset2D offset;
			surface.actualExtent }								// Extent2D extent;
	};

	// Rasterization state
	_pipeline.pipeinfo.pRasterizationState = &vk::PipelineRasterizationStateCreateInfo{
		vk::PipelineRasterizationStateCreateFlags(),	// PipelineRasterizationStateCreateFlags flags;
		VK_FALSE,										// Bool32 depthClampEnable;
		VK_FALSE,										// Bool32 rasterizerDiscardEnable;
		vk::PolygonMode::eFill,							// PolygonMode polygonMode;
		vk::CullModeFlagBits::eBack,					// CullModeFlags cullMode;
		vk::FrontFace::eCounterClockwise,				// FrontFace frontFace;
		VK_FALSE,										// Bool32 depthBiasEnable;
		0.0f,											// float depthBiasConstantFactor;
		0.0f,											// float depthBiasClamp;
		0.0f,											// float depthBiasSlopeFactor;
		1.0f											// float lineWidth;
	};

	// Multisample state
	_pipeline.pipeinfo.pMultisampleState = &vk::PipelineMultisampleStateCreateInfo{
		vk::PipelineMultisampleStateCreateFlags(),	// PipelineMultisampleStateCreateFlags flags;
		vk::SampleCountFlagBits::e1,				// SampleCountFlagBits rasterizationSamples;
		VK_FALSE,									// Bool32 sampleShadingEnable;
		1.0f,										// float minSampleShading;
		nullptr,									// const SampleMask* pSampleMask;
		VK_FALSE,									// Bool32 alphaToCoverageEnable;
		VK_FALSE									// Bool32 alphaToOneEnable;
	};

	// Depth and stencil state
	_pipeline.pipeinfo.pDepthStencilState = &vk::PipelineDepthStencilStateCreateInfo{
		vk::PipelineDepthStencilStateCreateFlags{},					// PipelineDepthStencilStateCreateFlags flags;
		VK_TRUE,													// Bool32 depthTestEnable;
		VK_TRUE,													// Bool32 depthWriteEnable;
		vk::CompareOp::eLessOrEqual,								// CompareOp depthCompareOp;
		VK_FALSE,													// Bool32 depthBoundsTestEnable;
		VK_FALSE,													// Bool32 stencilTestEnable;
		vk::StencilOpState().setCompareOp(vk::CompareOp::eAlways),	// StencilOpState front;
		vk::StencilOpState().setCompareOp(vk::CompareOp::eAlways),	// StencilOpState back;
		0.0f,														// float minDepthBounds;
		0.0f														// float maxDepthBounds;
	};

	// Color Blending state
	vk::PipelineColorBlendAttachmentState colorBlendAttachment{
				VK_TRUE,												// Bool32 blendEnable;
				vk::BlendFactor::eSrcAlpha,								// BlendFactor srcColorBlendFactor;
				vk::BlendFactor::eOneMinusSrcAlpha,						// BlendFactor dstColorBlendFactor;
				vk::BlendOp::eAdd,										// BlendOp colorBlendOp;
				vk::BlendFactor::eOne,									// BlendFactor srcAlphaBlendFactor;
				vk::BlendFactor::eZero,									// BlendFactor dstAlphaBlendFactor;
				vk::BlendOp::eAdd,										// BlendOp alphaBlendOp;
				vk::ColorComponentFlagBits::eR |						// ColorComponentFlags colorWriteMask;
				vk::ColorComponentFlagBits::eG |
				vk::ColorComponentFlagBits::eB |
				vk::ColorComponentFlagBits::eA
	};
	std::vector<vk::PipelineColorBlendAttachmentState> colorBlendAttachments = { colorBlendAttachment };
	_pipeline.pipeinfo.pColorBlendState = &vk::PipelineColorBlendStateCreateInfo{
		vk::PipelineColorBlendStateCreateFlags{},				// PipelineColorBlendStateCreateFlags flags;
		VK_FALSE,												// Bool32 logicOpEnable; // this must be false is we want alpha blend
		vk::LogicOp::eCopy,										// LogicOp logicOp;
		(uint32_t)colorBlendAttachments.size(),					// uint32_t attachmentCount;
		colorBlendAttachments.data(),							// const PipelineColorBlendAttachmentState* pAttachments;
		{0.0f, 0.0f, 0.0f, 0.0f}								// float blendConstants[4];
	};

	// Dynamic state
	std::vector<vk::DynamicState> dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
	_pipeline.pipeinfo.pDynamicState = &vk::PipelineDynamicStateCreateInfo{
			vk::PipelineDynamicStateCreateFlags(),			//PipelineDynamicStateCreateFlags flags;
			static_cast<uint32_t>(dynamicStates.size()),	//uint32_t dynamicStateCount;
			dynamicStates.data()							//const DynamicState* pDynamicStates;
	};

	// Pipeline Layout
	{// DescriptorSetLayout
		std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings =
		{
			// Binding 0: Position input attachment 
			vk::DescriptorSetLayoutBinding{
				0,										//uint32_t binding;
				vk::DescriptorType::eCombinedImageSampler,//DescriptorType descriptorType;
				1,										//uint32_t descriptorCount;
				vk::ShaderStageFlagBits::eFragment,		//ShaderStageFlags stageFlags;
				nullptr									//const Sampler* pImmutableSamplers;
			},
			// Binding 1: Normal input attachment 
			vk::DescriptorSetLayoutBinding{
				1,										//uint32_t binding;
				vk::DescriptorType::eCombinedImageSampler,//DescriptorType descriptorType;
				1,										//uint32_t descriptorCount;
				vk::ShaderStageFlagBits::eFragment,		//ShaderStageFlags stageFlags;
				nullptr									//const Sampler* pImmutableSamplers;
			},
			// Binding 2: Albedo input attachment 
			vk::DescriptorSetLayoutBinding{
				2,										//uint32_t binding;
				vk::DescriptorType::eCombinedImageSampler,//DescriptorType descriptorType;
				1,										//uint32_t descriptorCount;
				vk::ShaderStageFlagBits::eFragment,		//ShaderStageFlags stageFlags;
				nullptr									//const Sampler* pImmutableSamplers;
			},
			// Binding 3: Specular input attachment 
			vk::DescriptorSetLayoutBinding{
				3,										//uint32_t binding;
				vk::DescriptorType::eCombinedImageSampler,//DescriptorType descriptorType;
				1,										//uint32_t descriptorCount;
				vk::ShaderStageFlagBits::eFragment,		//ShaderStageFlags stageFlags;
				nullptr									//const Sampler* pImmutableSamplers;
			},
			// Binding 4: Light positions
			vk::DescriptorSetLayoutBinding{
				4,										//uint32_t binding;
				vk::DescriptorType::eUniformBuffer,		//DescriptorType descriptorType;
				1,										//uint32_t descriptorCount;
				vk::ShaderStageFlagBits::eFragment,		//ShaderStageFlags stageFlags;
				nullptr									//const Sampler* pImmutableSamplers;
			},
			// Binding 5: SSAO image
			vk::DescriptorSetLayoutBinding{
				5,										//uint32_t binding;
				vk::DescriptorType::eCombinedImageSampler,//DescriptorType descriptorType;
				1,										//uint32_t descriptorCount;
				vk::ShaderStageFlagBits::eFragment,		//ShaderStageFlags stageFlags;
				nullptr									//const Sampler* pImmutableSamplers;
			}
		};

		vk::DescriptorSetLayoutCreateInfo descriptorLayout = vk::DescriptorSetLayoutCreateInfo{
			vk::DescriptorSetLayoutCreateFlags(),		//DescriptorSetLayoutCreateFlags flags;
			(uint32_t)setLayoutBindings.size(),			//uint32_t bindingCount;
			setLayoutBindings.data()					//const DescriptorSetLayoutBinding* pBindings;
		};

		VkCheck(device.createDescriptorSetLayout(&descriptorLayout, nullptr, &deferred.DSLayoutComposition));
	}

	std::vector<vk::DescriptorSetLayout> descriptorSetLayouts = { deferred.DSLayoutComposition, Shadows::getDescriptorSetLayout(device) };


	VkCheck(device.createPipelineLayout(
		&vk::PipelineLayoutCreateInfo{ vk::PipelineLayoutCreateFlags(), (uint32_t)descriptorSetLayouts.size(), descriptorSetLayouts.data(), 1, &vk::PushConstantRange{ vk::ShaderStageFlagBits::eFragment, 0, sizeof(float) } },
		nullptr,
		&_pipeline.pipeinfo.layout));
	std::cout << "Pipeline Layout created\n";

	// Render Pass
	_pipeline.pipeinfo.renderPass = deferred.compositionRenderPass;

	// Subpass (Index of subpass this pipeline will be used in)
	_pipeline.pipeinfo.subpass = 0;

	// Base Pipeline Handle
	_pipeline.pipeinfo.basePipelineHandle = nullptr;

	// Base Pipeline Index
	_pipeline.pipeinfo.basePipelineIndex = -1;

	VkCheck(device.createGraphicsPipelines(nullptr, 1, &_pipeline.pipeinfo, nullptr, &_pipeline.pipeline));
	std::cout << "Pipeline created\n";

	// destroy Shader Modules
	device.destroyShaderModule(vertModule);
	device.destroyShaderModule(fragModule);

	return _pipeline;
}

Pipeline Context::createReflectionPipeline()
{
	Pipeline _pipeline;

	// Shader stages
	std::vector<char> vertCode{};
	std::vector<char> fragCode{};

	vk::ShaderModuleCreateInfo vsmci;
	vk::ShaderModuleCreateInfo fsmci;

	vk::ShaderModule vertModule;
	vk::ShaderModule fragModule;
	{
		vertCode = readFile("shaders/Reflections/vert.spv");
		fragCode = readFile("shaders/Reflections/frag.spv");

		vsmci = vk::ShaderModuleCreateInfo{
			vk::ShaderModuleCreateFlags(),						// ShaderModuleCreateFlags flags;
			vertCode.size(),									// size_t codeSize;
			reinterpret_cast<const uint32_t*>(vertCode.data())	// const uint32_t* pCode;
		};
		fsmci = vk::ShaderModuleCreateInfo{
			vk::ShaderModuleCreateFlags(),						// ShaderModuleCreateFlags flags;
			fragCode.size(),									// size_t codeSize;
			reinterpret_cast<const uint32_t*>(fragCode.data())	// const uint32_t* pCode;
		};
		VkCheck(device.createShaderModule(&vsmci, nullptr, &vertModule));
		std::cout << "Vertex Shader Module created\n";
		VkCheck(device.createShaderModule(&fsmci, nullptr, &fragModule));
		std::cout << "Fragment Shader Module created\n";
	}

	std::vector<vk::PipelineShaderStageCreateInfo> stages{
		vk::PipelineShaderStageCreateInfo{
			vk::PipelineShaderStageCreateFlags(),	// PipelineShaderStageCreateFlags flags;
			vk::ShaderStageFlagBits::eVertex,		// ShaderStageFlagBits stage;
			vertModule,								// ShaderModule module;
			"main",									// const char* pName;
			nullptr									// const SpecializationInfo* pSpecializationInfo;
		},
		vk::PipelineShaderStageCreateInfo{
			vk::PipelineShaderStageCreateFlags(),	// PipelineShaderStageCreateFlags flags;
			vk::ShaderStageFlagBits::eFragment,		// ShaderStageFlagBits stage;
			fragModule,								// ShaderModule module;
			"main",									// const char* pName;
			nullptr									// const SpecializationInfo* pSpecializationInfo;
		}
	};
	_pipeline.pipeinfo.stageCount = static_cast<uint32_t>(stages.size());
	_pipeline.pipeinfo.pStages = stages.data();

	// Vertex Input state
	_pipeline.pipeinfo.pVertexInputState = &vk::PipelineVertexInputStateCreateInfo();

	// Input Assembly stage
	_pipeline.pipeinfo.pInputAssemblyState = &vk::PipelineInputAssemblyStateCreateInfo{
		vk::PipelineInputAssemblyStateCreateFlags(),	// PipelineInputAssemblyStateCreateFlags flags;
		vk::PrimitiveTopology::eTriangleList,			// PrimitiveTopology topology;
		VK_FALSE										// Bool32 primitiveRestartEnable;
	};

	// Viewports and Scissors
	_pipeline.pipeinfo.pViewportState = &vk::PipelineViewportStateCreateInfo{
		vk::PipelineViewportStateCreateFlags(),				// PipelineViewportStateCreateFlags flags;
		1,													// uint32_t viewportCount;
		&vk::Viewport{										// const Viewport* pViewports;
			0.0f,												// float x;
			0.0f,												// float y;
			static_cast<float>(surface.actualExtent.width), 	// float width;
			static_cast<float>(surface.actualExtent.height),	// float height;
			0.0f,												// float minDepth;
			1.0f },												// float maxDepth;
		1,													// uint32_t scissorCount;
		&vk::Rect2D{										// const Rect2D* pScissors;
			vk::Offset2D(),										// Offset2D offset;
			surface.actualExtent }								// Extent2D extent;
	};

	// Rasterization state
	_pipeline.pipeinfo.pRasterizationState = &vk::PipelineRasterizationStateCreateInfo{
		vk::PipelineRasterizationStateCreateFlags(),	// PipelineRasterizationStateCreateFlags flags;
		VK_FALSE,										// Bool32 depthClampEnable;
		VK_FALSE,										// Bool32 rasterizerDiscardEnable;
		vk::PolygonMode::eFill,							// PolygonMode polygonMode;
		vk::CullModeFlagBits::eBack,					// CullModeFlags cullMode;
		vk::FrontFace::eCounterClockwise,						// FrontFace frontFace;
		VK_FALSE,										// Bool32 depthBiasEnable;
		0.0f,											// float depthBiasConstantFactor;
		0.0f,											// float depthBiasClamp;
		0.0f,											// float depthBiasSlopeFactor;
		1.0f											// float lineWidth;
	};

	// Multisample state
	_pipeline.pipeinfo.pMultisampleState = &vk::PipelineMultisampleStateCreateInfo{
		vk::PipelineMultisampleStateCreateFlags(),	// PipelineMultisampleStateCreateFlags flags;
		vk::SampleCountFlagBits::e1,				// SampleCountFlagBits rasterizationSamples;
		VK_FALSE,									// Bool32 sampleShadingEnable;
		1.0f,										// float minSampleShading;
		nullptr,									// const SampleMask* pSampleMask;
		VK_FALSE,									// Bool32 alphaToCoverageEnable;
		VK_FALSE									// Bool32 alphaToOneEnable;
	};

	// Depth and stencil state
	_pipeline.pipeinfo.pDepthStencilState = &vk::PipelineDepthStencilStateCreateInfo{
		vk::PipelineDepthStencilStateCreateFlags{},					// PipelineDepthStencilStateCreateFlags flags;
		VK_TRUE,													// Bool32 depthTestEnable;
		VK_TRUE,													// Bool32 depthWriteEnable;
		vk::CompareOp::eLessOrEqual,								// CompareOp depthCompareOp;
		VK_FALSE,													// Bool32 depthBoundsTestEnable;
		VK_FALSE,													// Bool32 stencilTestEnable;
		vk::StencilOpState().setCompareOp(vk::CompareOp::eAlways),	// StencilOpState front;
		vk::StencilOpState().setCompareOp(vk::CompareOp::eAlways),	// StencilOpState back;
		0.0f,														// float minDepthBounds;
		0.0f														// float maxDepthBounds;
	};

	// Color Blending state
	_pipeline.pipeinfo.pColorBlendState = &vk::PipelineColorBlendStateCreateInfo{
		vk::PipelineColorBlendStateCreateFlags{},				// PipelineColorBlendStateCreateFlags flags;
		VK_FALSE,												// Bool32 logicOpEnable; // this must be false is we want alpha blend
		vk::LogicOp::eCopy,										// LogicOp logicOp;
		1,														// uint32_t attachmentCount;
		&vk::PipelineColorBlendAttachmentState{					// const PipelineColorBlendAttachmentState* pAttachments;
			VK_TRUE,												// Bool32 blendEnable;
			vk::BlendFactor::eSrcAlpha,								// BlendFactor srcColorBlendFactor;
			vk::BlendFactor::eOneMinusSrcAlpha,						// BlendFactor dstColorBlendFactor;
			vk::BlendOp::eAdd,										// BlendOp colorBlendOp;
			vk::BlendFactor::eOne,									// BlendFactor srcAlphaBlendFactor;
			vk::BlendFactor::eZero,									// BlendFactor dstAlphaBlendFactor;
			vk::BlendOp::eAdd,										// BlendOp alphaBlendOp;
			vk::ColorComponentFlagBits::eR |						// ColorComponentFlags colorWriteMask;
			vk::ColorComponentFlagBits::eG |
			vk::ColorComponentFlagBits::eB |
			vk::ColorComponentFlagBits::eA
		},
		{0.0f, 0.0f, 0.0f, 0.0f}								// float blendConstants[4];
	};

	// Dynamic state
	std::vector<vk::DynamicState> dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
	_pipeline.pipeinfo.pDynamicState = &vk::PipelineDynamicStateCreateInfo{
			vk::PipelineDynamicStateCreateFlags(),			//PipelineDynamicStateCreateFlags flags;
			static_cast<uint32_t>(dynamicStates.size()),	//uint32_t dynamicStateCount;
			dynamicStates.data()							//const DynamicState* pDynamicStates;
	};

	// Pipeline Layout
	{// DescriptorSetLayout
		std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings =
		{
			// Binding 0: Albedo image Sampler 
			vk::DescriptorSetLayoutBinding{
				0,											//uint32_t binding;
				vk::DescriptorType::eCombinedImageSampler,	//DescriptorType descriptorType;
				1,											//uint32_t descriptorCount;
				vk::ShaderStageFlagBits::eFragment,			//ShaderStageFlags stageFlags;
				nullptr										//const Sampler* pImmutableSamplers;
			},
			// Binding 1: Positions image sampler  
			vk::DescriptorSetLayoutBinding{
				1,											//uint32_t binding;
				vk::DescriptorType::eCombinedImageSampler,	//DescriptorType descriptorType;
				1,											//uint32_t descriptorCount;
				vk::ShaderStageFlagBits::eFragment,			//ShaderStageFlags stageFlags;
				nullptr										//const Sampler* pImmutableSamplers;
			},
			// Binding 2: Normals image sampler  
			vk::DescriptorSetLayoutBinding{
				2,											//uint32_t binding;
				vk::DescriptorType::eCombinedImageSampler,	//DescriptorType descriptorType;
				1,											//uint32_t descriptorCount;
				vk::ShaderStageFlagBits::eFragment,			//ShaderStageFlags stageFlags;
				nullptr										//const Sampler* pImmutableSamplers;
			},
			// Binding 3: Depth image sampler  
			vk::DescriptorSetLayoutBinding{
				3,											//uint32_t binding;
				vk::DescriptorType::eCombinedImageSampler,	//DescriptorType descriptorType;
				1,											//uint32_t descriptorCount;
				vk::ShaderStageFlagBits::eFragment,			//ShaderStageFlags stageFlags;
				nullptr										//const Sampler* pImmutableSamplers;
			},
			// Binding 4: Camera Position
			vk::DescriptorSetLayoutBinding{
				4,											//uint32_t binding;
				vk::DescriptorType::eUniformBuffer,			//DescriptorType descriptorType;
				1,											//uint32_t descriptorCount;
				vk::ShaderStageFlagBits::eFragment,			//ShaderStageFlags stageFlags;
				nullptr										//const Sampler* pImmutableSamplers;
			}
		};

		vk::DescriptorSetLayoutCreateInfo descriptorLayout = vk::DescriptorSetLayoutCreateInfo{
			vk::DescriptorSetLayoutCreateFlags(),		//DescriptorSetLayoutCreateFlags flags;
			(uint32_t)setLayoutBindings.size(),			//uint32_t bindingCount;
			setLayoutBindings.data()					//const DescriptorSetLayoutBinding* pBindings;
		};

		VkCheck(device.createDescriptorSetLayout(&descriptorLayout, nullptr, &ssr.DSLayoutReflection));

	}

	std::vector<vk::DescriptorSetLayout> descriptorSetLayouts = { ssr.DSLayoutReflection };
	VkCheck(device.createPipelineLayout(
		&vk::PipelineLayoutCreateInfo{ vk::PipelineLayoutCreateFlags(), (uint32_t)descriptorSetLayouts.size(), descriptorSetLayouts.data(), 0, &vk::PushConstantRange() },
		nullptr,
		&_pipeline.pipeinfo.layout));
	std::cout << "Pipeline Layout created\n";

	// Render Pass
	_pipeline.pipeinfo.renderPass = ssr.ssrRenderPass;

	// Subpass (Index of subpass this pipeline will be used in)
	_pipeline.pipeinfo.subpass = 0;

	// Base Pipeline Handle
	_pipeline.pipeinfo.basePipelineHandle = nullptr;

	// Base Pipeline Index
	_pipeline.pipeinfo.basePipelineIndex = -1;

	VkCheck(device.createGraphicsPipelines(nullptr, 1, &_pipeline.pipeinfo, nullptr, &_pipeline.pipeline));
	std::cout << "Pipeline created\n";

	// destroy Shader Modules
	device.destroyShaderModule(vertModule);
	device.destroyShaderModule(fragModule);

	return _pipeline;
}

Pipeline Context::createSSAOPipeline()
{
	Pipeline _pipeline;

	// Shader stages
	std::vector<char> vertCode{};
	std::vector<char> fragCode{};

	vk::ShaderModuleCreateInfo vsmci;
	vk::ShaderModuleCreateInfo fsmci;

	vk::ShaderModule vertModule;
	vk::ShaderModule fragModule;
	{
		vertCode = readFile("shaders/SSAO/vert.spv");
		fragCode = readFile("shaders/SSAO/frag.spv");

		vsmci = vk::ShaderModuleCreateInfo{
			vk::ShaderModuleCreateFlags(),						// ShaderModuleCreateFlags flags;
			vertCode.size(),									// size_t codeSize;
			reinterpret_cast<const uint32_t*>(vertCode.data())	// const uint32_t* pCode;
		};
		fsmci = vk::ShaderModuleCreateInfo{
			vk::ShaderModuleCreateFlags(),						// ShaderModuleCreateFlags flags;
			fragCode.size(),									// size_t codeSize;
			reinterpret_cast<const uint32_t*>(fragCode.data())	// const uint32_t* pCode;
		};
		VkCheck(device.createShaderModule(&vsmci, nullptr, &vertModule));
		std::cout << "Vertex Shader Module created\n";
		VkCheck(device.createShaderModule(&fsmci, nullptr, &fragModule));
		std::cout << "Fragment Shader Module created\n";
	}

	std::vector<vk::PipelineShaderStageCreateInfo> stages{
		vk::PipelineShaderStageCreateInfo{
			vk::PipelineShaderStageCreateFlags(),	// PipelineShaderStageCreateFlags flags;
			vk::ShaderStageFlagBits::eVertex,		// ShaderStageFlagBits stage;
			vertModule,								// ShaderModule module;
			"main",									// const char* pName;
			nullptr									// const SpecializationInfo* pSpecializationInfo;
		},
		vk::PipelineShaderStageCreateInfo{
			vk::PipelineShaderStageCreateFlags(),	// PipelineShaderStageCreateFlags flags;
			vk::ShaderStageFlagBits::eFragment,		// ShaderStageFlagBits stage;
			fragModule,								// ShaderModule module;
			"main",									// const char* pName;
			nullptr									// const SpecializationInfo* pSpecializationInfo;
		}
	};
	_pipeline.pipeinfo.stageCount = static_cast<uint32_t>(stages.size());
	_pipeline.pipeinfo.pStages = stages.data();

	// Vertex Input state
	_pipeline.pipeinfo.pVertexInputState = &vk::PipelineVertexInputStateCreateInfo();

	// Input Assembly stage
	_pipeline.pipeinfo.pInputAssemblyState = &vk::PipelineInputAssemblyStateCreateInfo{
		vk::PipelineInputAssemblyStateCreateFlags(),	// PipelineInputAssemblyStateCreateFlags flags;
		vk::PrimitiveTopology::eTriangleList,			// PrimitiveTopology topology;
		VK_FALSE										// Bool32 primitiveRestartEnable;
	};

	// Viewports and Scissors
	_pipeline.pipeinfo.pViewportState = &vk::PipelineViewportStateCreateInfo{
		vk::PipelineViewportStateCreateFlags(),				// PipelineViewportStateCreateFlags flags;
		1,													// uint32_t viewportCount;
		&vk::Viewport{										// const Viewport* pViewports;
			0.0f,												// float x;
			0.0f,												// float y;
			static_cast<float>(surface.actualExtent.width), 	// float width;
			static_cast<float>(surface.actualExtent.height),	// float height;
			0.0f,												// float minDepth;
			1.0f },												// float maxDepth;
		1,													// uint32_t scissorCount;
		&vk::Rect2D{										// const Rect2D* pScissors;
			vk::Offset2D(),										// Offset2D offset;
			surface.actualExtent }								// Extent2D extent;
	};

	// Rasterization state
	_pipeline.pipeinfo.pRasterizationState = &vk::PipelineRasterizationStateCreateInfo{
		vk::PipelineRasterizationStateCreateFlags(),	// PipelineRasterizationStateCreateFlags flags;
		VK_FALSE,										// Bool32 depthClampEnable;
		VK_FALSE,										// Bool32 rasterizerDiscardEnable;
		vk::PolygonMode::eFill,							// PolygonMode polygonMode;
		vk::CullModeFlagBits::eBack,					// CullModeFlags cullMode;
		vk::FrontFace::eCounterClockwise,						// FrontFace frontFace;
		VK_FALSE,										// Bool32 depthBiasEnable;
		0.0f,											// float depthBiasConstantFactor;
		0.0f,											// float depthBiasClamp;
		0.0f,											// float depthBiasSlopeFactor;
		1.0f											// float lineWidth;
	};

	// Multisample state
	_pipeline.pipeinfo.pMultisampleState = &vk::PipelineMultisampleStateCreateInfo{
		vk::PipelineMultisampleStateCreateFlags(),	// PipelineMultisampleStateCreateFlags flags;
		vk::SampleCountFlagBits::e1,				// SampleCountFlagBits rasterizationSamples;
		VK_FALSE,									// Bool32 sampleShadingEnable;
		1.0f,										// float minSampleShading;
		nullptr,									// const SampleMask* pSampleMask;
		VK_FALSE,									// Bool32 alphaToCoverageEnable;
		VK_FALSE									// Bool32 alphaToOneEnable;
	};

	// Depth and stencil state
	_pipeline.pipeinfo.pDepthStencilState = &vk::PipelineDepthStencilStateCreateInfo{
		vk::PipelineDepthStencilStateCreateFlags{},					// PipelineDepthStencilStateCreateFlags flags;
		VK_TRUE,													// Bool32 depthTestEnable;
		VK_TRUE,													// Bool32 depthWriteEnable;
		vk::CompareOp::eLessOrEqual,								// CompareOp depthCompareOp;
		VK_FALSE,													// Bool32 depthBoundsTestEnable;
		VK_FALSE,													// Bool32 stencilTestEnable;
		vk::StencilOpState().setCompareOp(vk::CompareOp::eAlways),	// StencilOpState front;
		vk::StencilOpState().setCompareOp(vk::CompareOp::eAlways),	// StencilOpState back;
		0.0f,														// float minDepthBounds;
		0.0f														// float maxDepthBounds;
	};

	// Color Blending state
	_pipeline.pipeinfo.pColorBlendState = &vk::PipelineColorBlendStateCreateInfo{
		vk::PipelineColorBlendStateCreateFlags{},				// PipelineColorBlendStateCreateFlags flags;
		VK_FALSE,												// Bool32 logicOpEnable; // this must be false is we want alpha blend
		vk::LogicOp::eCopy,										// LogicOp logicOp;
		1,														// uint32_t attachmentCount;
		&vk::PipelineColorBlendAttachmentState{					// const PipelineColorBlendAttachmentState* pAttachments;
			VK_TRUE,												// Bool32 blendEnable;
			vk::BlendFactor::eSrcAlpha,								// BlendFactor srcColorBlendFactor;
			vk::BlendFactor::eOneMinusSrcAlpha,						// BlendFactor dstColorBlendFactor;
			vk::BlendOp::eAdd,										// BlendOp colorBlendOp;
			vk::BlendFactor::eOne,									// BlendFactor srcAlphaBlendFactor;
			vk::BlendFactor::eZero,									// BlendFactor dstAlphaBlendFactor;
			vk::BlendOp::eAdd,										// BlendOp alphaBlendOp;
			vk::ColorComponentFlagBits::eR |						// ColorComponentFlags colorWriteMask;
			vk::ColorComponentFlagBits::eG |
			vk::ColorComponentFlagBits::eB |
			vk::ColorComponentFlagBits::eA
		},
		{0.0f, 0.0f, 0.0f, 0.0f}								// float blendConstants[4];
	};

	// Dynamic state
	std::vector<vk::DynamicState> dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
	_pipeline.pipeinfo.pDynamicState = &vk::PipelineDynamicStateCreateInfo{
			vk::PipelineDynamicStateCreateFlags(),			//PipelineDynamicStateCreateFlags flags;
			static_cast<uint32_t>(dynamicStates.size()),	//uint32_t dynamicStateCount;
			dynamicStates.data()							//const DynamicState* pDynamicStates;
	};

	// Pipeline Layout
	{// DescriptorSetLayout
		std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings =
		{
			// Binding 0: Positions image sampler  
			vk::DescriptorSetLayoutBinding{
				0,											//uint32_t binding;
				vk::DescriptorType::eCombinedImageSampler,	//DescriptorType descriptorType;
				1,											//uint32_t descriptorCount;
				vk::ShaderStageFlagBits::eFragment,			//ShaderStageFlags stageFlags;
				nullptr										//const Sampler* pImmutableSamplers;
			},
			// Binding 1: Normals image sampler  
			vk::DescriptorSetLayoutBinding{
				1,											//uint32_t binding;
				vk::DescriptorType::eCombinedImageSampler,	//DescriptorType descriptorType;
				1,											//uint32_t descriptorCount;
				vk::ShaderStageFlagBits::eFragment,			//ShaderStageFlags stageFlags;
				nullptr										//const Sampler* pImmutableSamplers;
			},
			// Binding 2: Noise image sampler  
			vk::DescriptorSetLayoutBinding{
				2,											//uint32_t binding;
				vk::DescriptorType::eCombinedImageSampler,	//DescriptorType descriptorType;
				1,											//uint32_t descriptorCount;
				vk::ShaderStageFlagBits::eFragment,			//ShaderStageFlags stageFlags;
				nullptr										//const Sampler* pImmutableSamplers;
			},
			// Binding 3: Kernel Uniform
			vk::DescriptorSetLayoutBinding{
				3,											//uint32_t binding;
				vk::DescriptorType::eUniformBuffer,			//DescriptorType descriptorType;
				1,											//uint32_t descriptorCount;
				vk::ShaderStageFlagBits::eFragment,			//ShaderStageFlags stageFlags;
				nullptr										//const Sampler* pImmutableSamplers;
			},
			// Binding 4: PVM Uniform
			vk::DescriptorSetLayoutBinding{
				4,											//uint32_t binding;
				vk::DescriptorType::eUniformBuffer,			//DescriptorType descriptorType;
				1,											//uint32_t descriptorCount;
				vk::ShaderStageFlagBits::eFragment,			//ShaderStageFlags stageFlags;
				nullptr										//const Sampler* pImmutableSamplers;
			}
		};

		vk::DescriptorSetLayoutCreateInfo descriptorLayout = vk::DescriptorSetLayoutCreateInfo{
			vk::DescriptorSetLayoutCreateFlags(),		//DescriptorSetLayoutCreateFlags flags;
			(uint32_t)setLayoutBindings.size(),			//uint32_t bindingCount;
			setLayoutBindings.data()					//const DescriptorSetLayoutBinding* pBindings;
		};

		VkCheck(device.createDescriptorSetLayout(&descriptorLayout, nullptr, &ssao.DSLayoutSSAO));

	}

	std::vector<vk::DescriptorSetLayout> descriptorSetLayouts = { ssao.DSLayoutSSAO };
	VkCheck(device.createPipelineLayout(
		&vk::PipelineLayoutCreateInfo{ vk::PipelineLayoutCreateFlags(), (uint32_t)descriptorSetLayouts.size(), descriptorSetLayouts.data(), 0, &vk::PushConstantRange() },
		nullptr,
		&_pipeline.pipeinfo.layout));
	std::cout << "Pipeline Layout created\n";

	// Render Pass
	_pipeline.pipeinfo.renderPass = ssao.ssaoRenderPass;

	// Subpass (Index of subpass this pipeline will be used in)
	_pipeline.pipeinfo.subpass = 0;

	// Base Pipeline Handle
	_pipeline.pipeinfo.basePipelineHandle = nullptr;

	// Base Pipeline Index
	_pipeline.pipeinfo.basePipelineIndex = -1;

	VkCheck(device.createGraphicsPipelines(nullptr, 1, &_pipeline.pipeinfo, nullptr, &_pipeline.pipeline));
	std::cout << "Pipeline created\n";

	// destroy Shader Modules
	device.destroyShaderModule(vertModule);
	device.destroyShaderModule(fragModule);

	return _pipeline;
}

Pipeline Context::createSSAOBlurPipeline()
{
	Pipeline _pipeline;

	// Shader stages
	std::vector<char> vertCode{};
	std::vector<char> fragCode{};

	vk::ShaderModuleCreateInfo vsmci;
	vk::ShaderModuleCreateInfo fsmci;

	vk::ShaderModule vertModule;
	vk::ShaderModule fragModule;
	{
		vertCode = readFile("shaders/SSAO/vertBlur.spv");
		fragCode = readFile("shaders/SSAO/fragBlur.spv");

		vsmci = vk::ShaderModuleCreateInfo{
			vk::ShaderModuleCreateFlags(),						// ShaderModuleCreateFlags flags;
			vertCode.size(),									// size_t codeSize;
			reinterpret_cast<const uint32_t*>(vertCode.data())	// const uint32_t* pCode;
		};
		fsmci = vk::ShaderModuleCreateInfo{
			vk::ShaderModuleCreateFlags(),						// ShaderModuleCreateFlags flags;
			fragCode.size(),									// size_t codeSize;
			reinterpret_cast<const uint32_t*>(fragCode.data())	// const uint32_t* pCode;
		};
		VkCheck(device.createShaderModule(&vsmci, nullptr, &vertModule));
		std::cout << "Vertex Shader Module created\n";
		VkCheck(device.createShaderModule(&fsmci, nullptr, &fragModule));
		std::cout << "Fragment Shader Module created\n";
	}

	std::vector<vk::PipelineShaderStageCreateInfo> stages{
		vk::PipelineShaderStageCreateInfo{
			vk::PipelineShaderStageCreateFlags(),	// PipelineShaderStageCreateFlags flags;
			vk::ShaderStageFlagBits::eVertex,		// ShaderStageFlagBits stage;
			vertModule,								// ShaderModule module;
			"main",									// const char* pName;
			nullptr									// const SpecializationInfo* pSpecializationInfo;
		},
		vk::PipelineShaderStageCreateInfo{
			vk::PipelineShaderStageCreateFlags(),	// PipelineShaderStageCreateFlags flags;
			vk::ShaderStageFlagBits::eFragment,		// ShaderStageFlagBits stage;
			fragModule,								// ShaderModule module;
			"main",									// const char* pName;
			nullptr									// const SpecializationInfo* pSpecializationInfo;
		}
	};
	_pipeline.pipeinfo.stageCount = static_cast<uint32_t>(stages.size());
	_pipeline.pipeinfo.pStages = stages.data();

	// Vertex Input state
	_pipeline.pipeinfo.pVertexInputState = &vk::PipelineVertexInputStateCreateInfo();

	// Input Assembly stage
	_pipeline.pipeinfo.pInputAssemblyState = &vk::PipelineInputAssemblyStateCreateInfo{
		vk::PipelineInputAssemblyStateCreateFlags(),	// PipelineInputAssemblyStateCreateFlags flags;
		vk::PrimitiveTopology::eTriangleList,			// PrimitiveTopology topology;
		VK_FALSE										// Bool32 primitiveRestartEnable;
	};

	// Viewports and Scissors
	_pipeline.pipeinfo.pViewportState = &vk::PipelineViewportStateCreateInfo{
		vk::PipelineViewportStateCreateFlags(),				// PipelineViewportStateCreateFlags flags;
		1,													// uint32_t viewportCount;
		&vk::Viewport{										// const Viewport* pViewports;
			0.0f,												// float x;
			0.0f,												// float y;
			static_cast<float>(surface.actualExtent.width), 	// float width;
			static_cast<float>(surface.actualExtent.height),	// float height;
			0.0f,												// float minDepth;
			1.0f },												// float maxDepth;
		1,													// uint32_t scissorCount;
		&vk::Rect2D{										// const Rect2D* pScissors;
			vk::Offset2D(),										// Offset2D offset;
			surface.actualExtent }								// Extent2D extent;
	};

	// Rasterization state
	_pipeline.pipeinfo.pRasterizationState = &vk::PipelineRasterizationStateCreateInfo{
		vk::PipelineRasterizationStateCreateFlags(),	// PipelineRasterizationStateCreateFlags flags;
		VK_FALSE,										// Bool32 depthClampEnable;
		VK_FALSE,										// Bool32 rasterizerDiscardEnable;
		vk::PolygonMode::eFill,							// PolygonMode polygonMode;
		vk::CullModeFlagBits::eBack,					// CullModeFlags cullMode;
		vk::FrontFace::eCounterClockwise,				// FrontFace frontFace;
		VK_FALSE,										// Bool32 depthBiasEnable;
		0.0f,											// float depthBiasConstantFactor;
		0.0f,											// float depthBiasClamp;
		0.0f,											// float depthBiasSlopeFactor;
		1.0f											// float lineWidth;
	};

	// Multisample state
	_pipeline.pipeinfo.pMultisampleState = &vk::PipelineMultisampleStateCreateInfo{
		vk::PipelineMultisampleStateCreateFlags(),	// PipelineMultisampleStateCreateFlags flags;
		vk::SampleCountFlagBits::e1,				// SampleCountFlagBits rasterizationSamples;
		VK_FALSE,									// Bool32 sampleShadingEnable;
		1.0f,										// float minSampleShading;
		nullptr,									// const SampleMask* pSampleMask;
		VK_FALSE,									// Bool32 alphaToCoverageEnable;
		VK_FALSE									// Bool32 alphaToOneEnable;
	};

	// Depth and stencil state
	_pipeline.pipeinfo.pDepthStencilState = &vk::PipelineDepthStencilStateCreateInfo{
		vk::PipelineDepthStencilStateCreateFlags{},					// PipelineDepthStencilStateCreateFlags flags;
		VK_TRUE,													// Bool32 depthTestEnable;
		VK_TRUE,													// Bool32 depthWriteEnable;
		vk::CompareOp::eLessOrEqual,								// CompareOp depthCompareOp;
		VK_FALSE,													// Bool32 depthBoundsTestEnable;
		VK_FALSE,													// Bool32 stencilTestEnable;
		vk::StencilOpState().setCompareOp(vk::CompareOp::eAlways),	// StencilOpState front;
		vk::StencilOpState().setCompareOp(vk::CompareOp::eAlways),	// StencilOpState back;
		0.0f,														// float minDepthBounds;
		0.0f														// float maxDepthBounds;
	};

	// Color Blending state
	_pipeline.pipeinfo.pColorBlendState = &vk::PipelineColorBlendStateCreateInfo{
		vk::PipelineColorBlendStateCreateFlags{},				// PipelineColorBlendStateCreateFlags flags;
		VK_FALSE,												// Bool32 logicOpEnable; // this must be false is we want alpha blend
		vk::LogicOp::eCopy,										// LogicOp logicOp;
		1,														// uint32_t attachmentCount;
		&vk::PipelineColorBlendAttachmentState{					// const PipelineColorBlendAttachmentState* pAttachments;
			VK_TRUE,												// Bool32 blendEnable;
			vk::BlendFactor::eSrcAlpha,								// BlendFactor srcColorBlendFactor;
			vk::BlendFactor::eOneMinusSrcAlpha,						// BlendFactor dstColorBlendFactor;
			vk::BlendOp::eAdd,										// BlendOp colorBlendOp;
			vk::BlendFactor::eOne,									// BlendFactor srcAlphaBlendFactor;
			vk::BlendFactor::eZero,									// BlendFactor dstAlphaBlendFactor;
			vk::BlendOp::eAdd,										// BlendOp alphaBlendOp;
			vk::ColorComponentFlagBits::eR |						// ColorComponentFlags colorWriteMask;
			vk::ColorComponentFlagBits::eG |
			vk::ColorComponentFlagBits::eB |
			vk::ColorComponentFlagBits::eA
		},
		{0.0f, 0.0f, 0.0f, 0.0f}								// float blendConstants[4];
	};

	// Dynamic state
	std::vector<vk::DynamicState> dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
	_pipeline.pipeinfo.pDynamicState = &vk::PipelineDynamicStateCreateInfo{
			vk::PipelineDynamicStateCreateFlags(),			//PipelineDynamicStateCreateFlags flags;
			static_cast<uint32_t>(dynamicStates.size()),	//uint32_t dynamicStateCount;
			dynamicStates.data()							//const DynamicState* pDynamicStates;
	};

	// Pipeline Layout
	{// DescriptorSetLayout
		std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings =
		{
			// Binding 0: SSAO image sampler  
			vk::DescriptorSetLayoutBinding{
				0,											//uint32_t binding;
				vk::DescriptorType::eCombinedImageSampler,	//DescriptorType descriptorType;
				1,											//uint32_t descriptorCount;
				vk::ShaderStageFlagBits::eFragment,			//ShaderStageFlags stageFlags;
				nullptr										//const Sampler* pImmutableSamplers;
			}
		};

		vk::DescriptorSetLayoutCreateInfo descriptorLayout = vk::DescriptorSetLayoutCreateInfo{
			vk::DescriptorSetLayoutCreateFlags(),		//DescriptorSetLayoutCreateFlags flags;
			(uint32_t)setLayoutBindings.size(),			//uint32_t bindingCount;
			setLayoutBindings.data()					//const DescriptorSetLayoutBinding* pBindings;
		};

		VkCheck(device.createDescriptorSetLayout(&descriptorLayout, nullptr, &ssao.DSLayoutSSAOBlur));

	}

	std::vector<vk::DescriptorSetLayout> descriptorSetLayouts = { ssao.DSLayoutSSAOBlur };
	VkCheck(device.createPipelineLayout(
		&vk::PipelineLayoutCreateInfo{ vk::PipelineLayoutCreateFlags(), (uint32_t)descriptorSetLayouts.size(), descriptorSetLayouts.data(), 0, &vk::PushConstantRange() },
		nullptr,
		&_pipeline.pipeinfo.layout));
	std::cout << "Pipeline Layout created\n";

	// Render Pass
	_pipeline.pipeinfo.renderPass = ssao.ssaoBlurRenderPass;

	// Subpass (Index of subpass this pipeline will be used in)
	_pipeline.pipeinfo.subpass = 0;

	// Base Pipeline Handle
	_pipeline.pipeinfo.basePipelineHandle = nullptr;

	// Base Pipeline Index
	_pipeline.pipeinfo.basePipelineIndex = -1;

	VkCheck(device.createGraphicsPipelines(nullptr, 1, &_pipeline.pipeinfo, nullptr, &_pipeline.pipeline));
	std::cout << "Pipeline created\n";

	// destroy Shader Modules
	device.destroyShaderModule(vertModule);
	device.destroyShaderModule(fragModule);

	return _pipeline;
}

Pipeline Context::createComputePipeline()
{
	Pipeline _pipeline;

	// Shader stages
	std::vector<char> compCode{};
	vk::ShaderModuleCreateInfo csmci;
	vk::ShaderModule compModule;
	{
		compCode = readFile("shaders/Compute/comp.spv");
		csmci = vk::ShaderModuleCreateInfo{
			vk::ShaderModuleCreateFlags(),							// ShaderModuleCreateFlags flags;
			compCode.size(),										// size_t codeSize;
			reinterpret_cast<const uint32_t*>(compCode.data()) };	// const uint32_t* pCode;
		VkCheck(device.createShaderModule(&csmci, nullptr, &compModule));
		std::cout << "Compute Shader Module created\n";
	}

	_pipeline.compinfo.stage.module = compModule;
	_pipeline.compinfo.stage.pName = "main";
	_pipeline.compinfo.stage.stage = vk::ShaderStageFlagBits::eCompute;

	{// DescriptorSetLayout
		std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings =
		{
			// Binding 0 (in)
			vk::DescriptorSetLayoutBinding{
				0,										//uint32_t binding;
				vk::DescriptorType::eStorageBuffer,		//DescriptorType descriptorType;
				1,										//uint32_t descriptorCount;
				vk::ShaderStageFlagBits::eCompute,		//ShaderStageFlags stageFlags;
				nullptr									//const Sampler* pImmutableSamplers;
			}
		};

		vk::DescriptorSetLayoutCreateInfo descriptorLayout = vk::DescriptorSetLayoutCreateInfo {
			vk::DescriptorSetLayoutCreateFlags(),		//DescriptorSetLayoutCreateFlags flags;
			(uint32_t)setLayoutBindings.size(),			//uint32_t bindingCount;
			setLayoutBindings.data()					//const DescriptorSetLayoutBinding* pBindings;
		};
		VkCheck(device.createDescriptorSetLayout(&descriptorLayout, nullptr, &compute.DSLayoutCompute));
	}
	std::vector<vk::DescriptorSetLayout> descriptorSetLayouts = { compute.DSLayoutCompute };
	VkCheck(device.createPipelineLayout(
		&vk::PipelineLayoutCreateInfo{ vk::PipelineLayoutCreateFlags(), (uint32_t)descriptorSetLayouts.size(), descriptorSetLayouts.data(), 0, &vk::PushConstantRange() },
		nullptr,
		&_pipeline.compinfo.layout));
	std::cout << "Compute Pipeline Layout created\n";

	VkCheck(device.createComputePipelines(nullptr, 1, &_pipeline.compinfo, nullptr, &_pipeline.pipeline));

	// destroy Shader Modules
	device.destroyShaderModule(compModule);

	return _pipeline;
}

vk::DescriptorPool Context::createDescriptorPool(uint32_t maxDescriptorSets)
{
	vk::DescriptorPool _descriptorPool;

	std::vector<vk::DescriptorPoolSize> descPoolsize = {};
	// mvp
	descPoolsize.push_back(vk::DescriptorPoolSize()
		.setType(vk::DescriptorType::eUniformBuffer)					//descriptor type
		.setDescriptorCount(maxDescriptorSets));

	// texture sampler2D
	descPoolsize.push_back(vk::DescriptorPoolSize()
		.setType(vk::DescriptorType::eCombinedImageSampler)				//descriptor type
		.setDescriptorCount(maxDescriptorSets));

	// input attachment
	descPoolsize.push_back(vk::DescriptorPoolSize()
		.setType(vk::DescriptorType::eInputAttachment)					//descriptor type
		.setDescriptorCount(maxDescriptorSets));

	// storage buffer
	descPoolsize.push_back(vk::DescriptorPoolSize()
		.setType(vk::DescriptorType::eStorageBuffer)					//descriptor type
		.setDescriptorCount(maxDescriptorSets));

	auto const createInfo = vk::DescriptorPoolCreateInfo()
		.setPoolSizeCount((uint32_t)descPoolsize.size())
		.setPPoolSizes(descPoolsize.data())
		.setMaxSets(maxDescriptorSets);

	VkCheck(device.createDescriptorPool(&createInfo, nullptr, &_descriptorPool));
	std::cout << "DescriptorPool created\n";

	return _descriptorPool;
}

std::vector<vk::Fence> Context::createFences(const uint32_t fenceCount)
{
	std::vector<vk::Fence> _fences(fenceCount);
	auto const fi = vk::FenceCreateInfo();

	for (uint32_t i = 0; i < fenceCount; i++) {
		device.createFence(&fi, nullptr, &_fences[i]);
		std::cout << "Fence created\n";
	}

	return _fences;
}

std::vector<vk::Semaphore> Context::createSemaphores(const uint32_t semaphoresCount)
{
	std::vector<vk::Semaphore> _semaphores(semaphoresCount);
	auto const si = vk::SemaphoreCreateInfo();

	for (uint32_t i = 0; i < semaphoresCount; i++) {
		VkCheck(device.createSemaphore(&si, nullptr, &_semaphores[i]));
		std::cout << "Semaphore created\n";
	}

	return _semaphores;
}

void Context::resizeViewport(uint32_t width, uint32_t height)
{
	device.waitIdle();

	// Free resources
	depth.destroy(device);
	forward.MSColorImage.destroy(device);
	forward.MSDepthImage.destroy(device);
	for (auto& fb : forward.frameBuffers)
		device.destroyFramebuffer(fb);
	forward.pipeline.destroy(device);
	gui.pipelineGUI.destroy(device);
	skyBox.pipelineSkyBox.destroy(device);
	terrain.pipelineTerrain.destroy(device);
	device.destroyRenderPass(forward.forwardRenderPass);
	swapchain.destroy(device);

	// Recreate resources
	surface.actualExtent = { width, height };
	swapchain = createSwapchain();
	depth = createDepthResources();
	forward.forwardRenderPass = createRenderPass(); // ?
	forward.frameBuffers = createFrameBuffers();
	terrain.pipelineTerrain = createPipeline(getPipelineSpecificationsTerrain());
	skyBox.pipelineSkyBox = createPipeline(getPipelineSpecificationsSkyBox());
	gui.pipelineGUI = createPipeline(getPipelineSpecificationsGUI());
	forward.pipeline = createPipeline(getPipelineSpecificationsModel());
}