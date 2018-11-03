#include "../include/Context.h"
#include "../include/Errors.h"
#include <iostream>
#include <fstream>

Context* Context::info = nullptr;

vk::DescriptorSetLayout getDescriptorSetLayoutLights(Context* info)
{
	// binding for model mvp matrix
	if (!info->DSLayoutLights) {
		std::vector<vk::DescriptorSetLayoutBinding> descriptorSetLayoutBinding{};
		descriptorSetLayoutBinding.push_back(vk::DescriptorSetLayoutBinding()
			.setBinding(0) // binding number in shader stages
			.setDescriptorCount(1) // number of descriptors contained
			.setDescriptorType(vk::DescriptorType::eUniformBuffer)
			.setStageFlags(vk::ShaderStageFlagBits::eFragment)); // which pipeline shader stages can access
		auto const createInfo = vk::DescriptorSetLayoutCreateInfo()
			.setBindingCount((uint32_t)descriptorSetLayoutBinding.size())
			.setPBindings(descriptorSetLayoutBinding.data());
		VkCheck(info->device.createDescriptorSetLayout(&createInfo, nullptr, &info->DSLayoutLights));
		std::cout << "Descriptor Set Layout created\n";
	}
	return info->DSLayoutLights;
}
specificGraphicsPipelineCreateInfo Context::getPipelineSpecificationsModel()
{
	// General Pipeline
	specificGraphicsPipelineCreateInfo generalSpecific;
	generalSpecific.shaders = { "shaders/General/vert.spv", "shaders/General/frag.spv" };
	generalSpecific.renderPass = info->renderPass;
	generalSpecific.viewportSize = { info->surface.actualExtent.width, info->surface.actualExtent.height };
	generalSpecific.descriptorSetLayouts = { Shadows::getDescriptorSetLayout(info->device), Mesh::getDescriptorSetLayout(info->device), Model::getDescriptorSetLayout(info->device), getDescriptorSetLayoutLights(info) };
	generalSpecific.vertexInputBindingDescriptions = Vertex::getBindingDescriptionGeneral();
	generalSpecific.vertexInputAttributeDescriptions = Vertex::getAttributeDescriptionGeneral();
	generalSpecific.cull = vk::CullModeFlagBits::eBack;
	generalSpecific.face = vk::FrontFace::eCounterClockwise;
	generalSpecific.pushConstantRange = vk::PushConstantRange();
	generalSpecific.specializationInfo = vk::SpecializationInfo();

	return generalSpecific;
}

specificGraphicsPipelineCreateInfo Context::getPipelineSpecificationsShadows()
{
	// Shadows Pipeline
	specificGraphicsPipelineCreateInfo shadowsSpecific;
	shadowsSpecific.shaders = { "shaders/Shadows/vert.spv" };
	shadowsSpecific.renderPass = Shadows::getRenderPass(info->device, info->depth);
	shadowsSpecific.viewportSize = { Shadows::imageSize, Shadows::imageSize };
	shadowsSpecific.useBlendState = false;
	shadowsSpecific.sampleCount = vk::SampleCountFlagBits::e1;
	shadowsSpecific.descriptorSetLayouts = { Shadows::getDescriptorSetLayout(info->device) };
	shadowsSpecific.vertexInputBindingDescriptions = Vertex::getBindingDescriptionGeneral();
	shadowsSpecific.vertexInputAttributeDescriptions = Vertex::getAttributeDescriptionGeneral();
	shadowsSpecific.cull = vk::CullModeFlagBits::eBack;
	shadowsSpecific.face = vk::FrontFace::eCounterClockwise;
	shadowsSpecific.pushConstantRange = vk::PushConstantRange();

	return shadowsSpecific;
}

specificGraphicsPipelineCreateInfo Context::getPipelineSpecificationsSkyBox()
{
	// SkyBox Pipeline
	specificGraphicsPipelineCreateInfo skyBoxSpecific;
	skyBoxSpecific.shaders = { "shaders/SkyBox/vert.spv", "shaders/SkyBox/frag.spv" };
	skyBoxSpecific.renderPass = info->renderPass;
	skyBoxSpecific.viewportSize = { info->surface.actualExtent.width, info->surface.actualExtent.height };
	skyBoxSpecific.descriptorSetLayouts = { SkyBox::getDescriptorSetLayout(info->device) };
	skyBoxSpecific.vertexInputBindingDescriptions = Vertex::getBindingDescriptionSkyBox();
	skyBoxSpecific.vertexInputAttributeDescriptions = Vertex::getAttributeDescriptionSkyBox();
	skyBoxSpecific.cull = vk::CullModeFlagBits::eBack;
	skyBoxSpecific.face = vk::FrontFace::eCounterClockwise;
	skyBoxSpecific.sampleCount = vk::SampleCountFlagBits::e4;
	skyBoxSpecific.pushConstantRange = vk::PushConstantRange();

	return skyBoxSpecific;
}

specificGraphicsPipelineCreateInfo Context::getPipelineSpecificationsTerrain()
{
	// Terrain Pipeline
	specificGraphicsPipelineCreateInfo terrainSpecific;
	terrainSpecific.shaders = { "shaders/Terrain/vert.spv", "shaders/Terrain/frag.spv" };
	terrainSpecific.renderPass = info->renderPass;
	terrainSpecific.viewportSize = { info->surface.actualExtent.width, info->surface.actualExtent.height };
	terrainSpecific.descriptorSetLayouts = { Terrain::getDescriptorSetLayout(info->device) };
	terrainSpecific.vertexInputBindingDescriptions = Vertex::getBindingDescriptionGeneral();
	terrainSpecific.vertexInputAttributeDescriptions = Vertex::getAttributeDescriptionGeneral();
	terrainSpecific.cull = vk::CullModeFlagBits::eBack;
	terrainSpecific.face = vk::FrontFace::eCounterClockwise;
	terrainSpecific.pushConstantRange = vk::PushConstantRange();

	return terrainSpecific;
}

specificGraphicsPipelineCreateInfo Context::getPipelineSpecificationsGUI()
{
	// GUI Pipeline
	specificGraphicsPipelineCreateInfo GUISpecific;
	GUISpecific.shaders = { "shaders/GUI/vert.spv", "shaders/GUI/frag.spv" };
	GUISpecific.renderPass = info->guiRenderPass;
	GUISpecific.viewportSize = { info->surface.actualExtent.width, info->surface.actualExtent.height };
	GUISpecific.descriptorSetLayouts = { GUI::getDescriptorSetLayout(info->device) };
	GUISpecific.vertexInputBindingDescriptions = Vertex::getBindingDescriptionGUI();
	GUISpecific.vertexInputAttributeDescriptions = Vertex::getAttributeDescriptionGUI();
	GUISpecific.cull = vk::CullModeFlagBits::eFront;
	GUISpecific.face = vk::FrontFace::eCounterClockwise;
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

void Context::initVulkanContext()
{
	instance = createInstance();
	surface = createSurface();
	gpu = findGpu();
	device = createDevice();
	semaphores = createSemaphores(3);
	fences = createFences(1);
	swapchain = createSwapchain();
	commandPool = createCommandPool();
	depth = createDepthResources();
	renderPass = createRenderPass();
	dRenderPass = createDeferredRenderPass();
	rRenderPass = createReflectionRenderPass();
	guiRenderPass = createGUIRenderPass();
	frameBuffers = createFrameBuffers();
	dFrameBuffers = createDeferredFrameBuffers();
	rFrameBuffers = createReflectionFrameBuffers();
	guiFrameBuffers = createGUIFrameBuffers();
	dynamicCmdBuffer = createCmdBuffer();
	shadowCmdBuffer = createCmdBuffer();
	descriptorPool = createDescriptorPool(1000); // max number of all descriptor sets to allocate	

	shadows.createFrameBuffers(device, gpu, depth, static_cast<uint32_t>(swapchain.images.size()));

	// create pipelines
	// FORWARD PIPELINES
	pipelineTerrain = createPipeline(getPipelineSpecificationsTerrain());
	pipelineShadows = createPipeline(getPipelineSpecificationsShadows());
	pipelineSkyBox = createPipeline(getPipelineSpecificationsSkyBox());
	pipelineGUI = createPipeline(getPipelineSpecificationsGUI());

	specificGraphicsPipelineCreateInfo sgpci = getPipelineSpecificationsModel();
	sgpci.specializationInfo = vk::SpecializationInfo{ 1, &vk::SpecializationMapEntry{ 0, 0, sizeof(MAX_LIGHTS) }, sizeof(MAX_LIGHTS), &MAX_LIGHTS };
	pipeline = createPipeline(sgpci);

	// DEFERRED PIPELINES
	sgpci.shaders = { "shaders/Deferred/vert.spv", "shaders/Deferred/frag.spv" };
	sgpci.renderPass = info->dRenderPass;
	sgpci.sampleCount = vk::SampleCountFlagBits::e1;
	sgpci.descriptorSetLayouts = { Model::getDescriptorSetLayout(device), Mesh::getDescriptorSetLayout(device) };
	sgpci.specializationInfo = vk::SpecializationInfo();
	sgpci.blendAttachmentStates[0].blendEnable = VK_FALSE;
	sgpci.blendAttachmentStates = {
		sgpci.blendAttachmentStates[0],
		sgpci.blendAttachmentStates[0],
		sgpci.blendAttachmentStates[0],
		sgpci.blendAttachmentStates[0],
		sgpci.blendAttachmentStates[0] };
	pipelineDeferred = createPipeline(sgpci);
	pipelineComposition = createCompositionPipeline();

	// SSR PIPELINE
	pipelineReflection = createReflectionPipeline();

	// COMPUTE PIPELINE
	//pipelineCompute = createComputePipeline();
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

vk::PhysicalDevice Context::findGpu()
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
				graphicsFamilyId = i;
			//find compute queue family index
			if (properties[i].queueFlags & vk::QueueFlagBits::eCompute)
				computeFamilyId = i;
			// find present queue family index
			vk::Bool32 presentSupport = false;
			gpu.getSurfaceSupportKHR(i, surface.surface, &presentSupport);
			if (properties[i].queueCount > 0 && presentSupport)
				presentFamilyId = i;

			//TODO: Check for different family IDs support in different queue families
			if (graphicsFamilyId >= 0 && presentFamilyId >=0 && computeFamilyId >= 0) {
				gpu.getProperties(&gpuProperties);
				gpu.getFeatures(&gpuFeatures);

				// 1. surface capabilities
				gpu.getSurfaceCapabilitiesKHR(surface.surface, &surface.capabilities);

				// 2. surface format
				uint32_t formatCount = 0;
				std::vector<vk::SurfaceFormatKHR> formats{};
				gpu.getSurfaceFormatsKHR(surface.surface, &formatCount, nullptr);
				if (formatCount == 0) {
					std::cout << "Surface formats missing\n";
					exit(-5);
				}
				formats.resize(formatCount);
				gpu.getSurfaceFormatsKHR(surface.surface, &formatCount, formats.data());

				// 3. presentation mode
				uint32_t presentCount = 0;
				std::vector<vk::PresentModeKHR> presentModes{};
				gpu.getSurfacePresentModesKHR(surface.surface, &presentCount, nullptr);
				if (presentCount == 0) {
					std::cout << "Surface formats missing\n";
					exit(-5);
				}
				presentModes.resize(presentCount);
				gpu.getSurfacePresentModesKHR(surface.surface, &presentCount, presentModes.data());

				for (const auto& i : presentModes) {
					if (i == vk::PresentModeKHR::eImmediate || i == vk::PresentModeKHR::eMailbox) {
						surface.presentModeKHR = i;
						break;
					}
					else surface.presentModeKHR = vk::PresentModeKHR::eFifo;
				}
				for (const auto& i : formats) {
					if (i.format == vk::Format::eB8G8R8A8Unorm && i.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) {
						surface.formatKHR = i;
						break;
					}
					else surface.formatKHR = formats[0];
				}
				return gpu;
			}
		}
	}

	return nullptr;
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

	//get the graphics queue handler
	graphicsQueue = _device.getQueue(graphicsFamilyId, 0);
	presentQueue = _device.getQueue(presentFamilyId, 0);
	computeQueue = _device.getQueue(computeFamilyId, 0);
	if (!graphicsQueue || !presentQueue || !computeQueue)
		return nullptr;

	return _device;
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
	
	position.format = vk::Format::eR16G16B16A16Sfloat;
	position.initialLayout = vk::ImageLayout::eUndefined;
	position.createImage(device, gpu, surface.actualExtent.width, surface.actualExtent.height, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eInputAttachment | vk::ImageUsageFlagBits::eSampled, vk::MemoryPropertyFlagBits::eDeviceLocal);
	position.createImageView(device, vk::ImageAspectFlagBits::eColor);
	position.createSampler(device);

	normal.format = vk::Format::eR16G16B16A16Sfloat;
	normal.initialLayout = vk::ImageLayout::eUndefined;
	normal.createImage(device, gpu, surface.actualExtent.width, surface.actualExtent.height, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eInputAttachment, vk::MemoryPropertyFlagBits::eDeviceLocal);
	normal.createImageView(device, vk::ImageAspectFlagBits::eColor);

	albedo.format = vk::Format::eR8G8B8A8Unorm;
	albedo.initialLayout = vk::ImageLayout::eUndefined;
	albedo.createImage(device, gpu, surface.actualExtent.width, surface.actualExtent.height, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eInputAttachment | vk::ImageUsageFlagBits::eSampled, vk::MemoryPropertyFlagBits::eDeviceLocal);
	albedo.createImageView(device, vk::ImageAspectFlagBits::eColor);

	specular.format = vk::Format::eR16G16B16A16Sfloat;
	specular.initialLayout = vk::ImageLayout::eUndefined;
	specular.createImage(device, gpu, surface.actualExtent.width, surface.actualExtent.height, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eInputAttachment, vk::MemoryPropertyFlagBits::eDeviceLocal);
	specular.createImageView(device, vk::ImageAspectFlagBits::eColor);

	finalColor.format = vk::Format::eR8G8B8A8Unorm;
	finalColor.initialLayout = vk::ImageLayout::eUndefined;
	finalColor.createImage(device, gpu, surface.actualExtent.width, surface.actualExtent.height, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled, vk::MemoryPropertyFlagBits::eDeviceLocal);
	finalColor.createImageView(device, vk::ImageAspectFlagBits::eColor);
	finalColor.createSampler(device);

	finalNormal.format = vk::Format::eR16G16B16A16Sfloat;
	finalNormal.initialLayout = vk::ImageLayout::eUndefined;
	finalNormal.createImage(device, gpu, surface.actualExtent.width, surface.actualExtent.height, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled, vk::MemoryPropertyFlagBits::eDeviceLocal);
	finalNormal.createImageView(device, vk::ImageAspectFlagBits::eColor);
	finalNormal.createSampler(device);

	finalDepth.format = vk::Format::eR16Sfloat;
	finalDepth.initialLayout = vk::ImageLayout::eUndefined;
	finalDepth.createImage(device, gpu, surface.actualExtent.width, surface.actualExtent.height, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled, vk::MemoryPropertyFlagBits::eDeviceLocal);
	finalDepth.createImageView(device, vk::ImageAspectFlagBits::eColor);
	finalDepth.createSampler(device);

	std::array<vk::AttachmentDescription, 9> attachments{};
	// Color attachment
	attachments[0].format = info->surface.formatKHR.format;
	attachments[0].samples = vk::SampleCountFlagBits::e1;
	attachments[0].loadOp = vk::AttachmentLoadOp::eClear;
	attachments[0].storeOp = vk::AttachmentStoreOp::eStore;
	attachments[0].stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
	attachments[0].stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
	attachments[0].initialLayout = vk::ImageLayout::eUndefined;
	attachments[0].finalLayout = vk::ImageLayout::ePresentSrcKHR;

	// Deferred attachments
	// Position
	attachments[1].format = position.format;
	attachments[1].samples = vk::SampleCountFlagBits::e1;
	attachments[1].loadOp = vk::AttachmentLoadOp::eClear;
	attachments[1].storeOp = vk::AttachmentStoreOp::eStore;
	attachments[1].stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
	attachments[1].stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
	attachments[1].initialLayout = vk::ImageLayout::eUndefined;
	attachments[1].finalLayout = vk::ImageLayout::eColorAttachmentOptimal;
	// Normals
	attachments[2].format = normal.format;
	attachments[2].samples = vk::SampleCountFlagBits::e1;
	attachments[2].loadOp = vk::AttachmentLoadOp::eClear;
	attachments[2].storeOp = vk::AttachmentStoreOp::eDontCare;
	attachments[2].stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
	attachments[2].stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
	attachments[2].initialLayout = vk::ImageLayout::eUndefined;
	attachments[2].finalLayout = vk::ImageLayout::eColorAttachmentOptimal;
	// Albedo
	attachments[3].format = albedo.format;
	attachments[3].samples = vk::SampleCountFlagBits::e1;
	attachments[3].loadOp = vk::AttachmentLoadOp::eClear;
	attachments[3].storeOp = vk::AttachmentStoreOp::eDontCare;
	attachments[3].stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
	attachments[3].stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
	attachments[3].initialLayout = vk::ImageLayout::eUndefined;
	attachments[3].finalLayout = vk::ImageLayout::eColorAttachmentOptimal;
	// Specular
	attachments[4].format = specular.format;
	attachments[4].samples = vk::SampleCountFlagBits::e1;
	attachments[4].loadOp = vk::AttachmentLoadOp::eClear;
	attachments[4].storeOp = vk::AttachmentStoreOp::eDontCare;
	attachments[4].stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
	attachments[4].stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
	attachments[4].initialLayout = vk::ImageLayout::eUndefined;
	attachments[4].finalLayout = vk::ImageLayout::eColorAttachmentOptimal;
	// Depth attachment
	attachments[5].format = info->depth.format;
	attachments[5].samples = vk::SampleCountFlagBits::e1;
	attachments[5].loadOp = vk::AttachmentLoadOp::eClear;
	attachments[5].storeOp = vk::AttachmentStoreOp::eStore;
	attachments[5].stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
	attachments[5].stencilStoreOp = vk::AttachmentStoreOp::eStore;
	attachments[5].initialLayout = vk::ImageLayout::eUndefined;
	attachments[5].finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
	// Final color store
	attachments[6].format = info->finalColor.format;
	attachments[6].samples = vk::SampleCountFlagBits::e1;
	attachments[6].loadOp = vk::AttachmentLoadOp::eClear;
	attachments[6].storeOp = vk::AttachmentStoreOp::eStore;
	attachments[6].stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
	attachments[6].stencilStoreOp = vk::AttachmentStoreOp::eStore;
	attachments[6].initialLayout = vk::ImageLayout::eUndefined;
	attachments[6].finalLayout = vk::ImageLayout::eColorAttachmentOptimal;
	// Final normal store
	attachments[7].format = info->finalNormal.format;
	attachments[7].samples = vk::SampleCountFlagBits::e1;
	attachments[7].loadOp = vk::AttachmentLoadOp::eClear;
	attachments[7].storeOp = vk::AttachmentStoreOp::eStore;
	attachments[7].stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
	attachments[7].stencilStoreOp = vk::AttachmentStoreOp::eStore;
	attachments[7].initialLayout = vk::ImageLayout::eUndefined;
	attachments[7].finalLayout = vk::ImageLayout::eColorAttachmentOptimal;
	// Final depth store
	attachments[8].format = info->finalDepth.format;
	attachments[8].samples = vk::SampleCountFlagBits::e1;
	attachments[8].loadOp = vk::AttachmentLoadOp::eClear;
	attachments[8].storeOp = vk::AttachmentStoreOp::eStore;
	attachments[8].stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
	attachments[8].stencilStoreOp = vk::AttachmentStoreOp::eStore;
	attachments[8].initialLayout = vk::ImageLayout::eUndefined;
	attachments[8].finalLayout = vk::ImageLayout::eColorAttachmentOptimal;

	// Two subpasses
	std::array<vk::SubpassDescription, 2> subpassDescriptions{};

	// First subpass: Fill G-Buffer components
	// ----------------------------------------------------------------------------------------

	std::vector<vk::AttachmentReference> colorReferences{
		{ 1, vk::ImageLayout::eColorAttachmentOptimal },
		{ 2, vk::ImageLayout::eColorAttachmentOptimal },
		{ 3, vk::ImageLayout::eColorAttachmentOptimal },
		{ 4, vk::ImageLayout::eColorAttachmentOptimal },
		{ 8, vk::ImageLayout::eColorAttachmentOptimal }
	};
	vk::AttachmentReference depthReference = { 5, vk::ImageLayout::eDepthStencilAttachmentOptimal };

	subpassDescriptions[0].pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
	subpassDescriptions[0].colorAttachmentCount = static_cast<uint32_t>(colorReferences.size());
	subpassDescriptions[0].pColorAttachments = colorReferences.data();
	subpassDescriptions[0].pDepthStencilAttachment = &depthReference;

	// Second subpass: Final composition (using G-Buffer components)
	// ----------------------------------------------------------------------------------------

	std::vector<vk::AttachmentReference> colorReferences1 = {
		{ 0, vk::ImageLayout::eColorAttachmentOptimal },
		{ 6, vk::ImageLayout::eColorAttachmentOptimal },
		{ 7, vk::ImageLayout::eColorAttachmentOptimal }
	};

	std::vector<vk::AttachmentReference> inputReferences{
		{ 1, vk::ImageLayout::eShaderReadOnlyOptimal },
		{ 2, vk::ImageLayout::eShaderReadOnlyOptimal },
		{ 3, vk::ImageLayout::eShaderReadOnlyOptimal },
		{ 4, vk::ImageLayout::eShaderReadOnlyOptimal }
	};

	subpassDescriptions[1].pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
	subpassDescriptions[1].colorAttachmentCount = static_cast<uint32_t>(colorReferences1.size());
	subpassDescriptions[1].pColorAttachments = colorReferences1.data();
	subpassDescriptions[1].pDepthStencilAttachment = nullptr;//&depthReference;
	// Use the color attachments filled in the first pass as input attachments
	subpassDescriptions[1].inputAttachmentCount = static_cast<uint32_t>(inputReferences.size());;
	subpassDescriptions[1].pInputAttachments = inputReferences.data();

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
			1,														// uint32_t dstSubpass;
			vk::PipelineStageFlagBits::eColorAttachmentOutput,		// PipelineStageFlags srcStageMask;
			vk::PipelineStageFlagBits::eFragmentShader,				// PipelineStageFlags dstStageMask;
			vk::AccessFlagBits::eColorAttachmentWrite,				// AccessFlags srcAccessMask;
			vk::AccessFlagBits::eShaderRead,						// AccessFlags dstAccessMask;
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

vk::RenderPass Context::createReflectionRenderPass()
{
	vk::RenderPass _renderPass;

	std::array<vk::AttachmentDescription, 1> attachments{};
	// Color attachment
	attachments[0].format = info->surface.formatKHR.format;
	attachments[0].samples = vk::SampleCountFlagBits::e1;
	attachments[0].loadOp = vk::AttachmentLoadOp::eClear;
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
		vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled, vk::MemoryPropertyFlagBits::eDeviceLocal);
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

	MSColorImage.format = surface.formatKHR.format;
	MSColorImage.createImage(device, gpu, surface.actualExtent.width, surface.actualExtent.height, vk::ImageTiling::eOptimal,
		vk::ImageUsageFlagBits::eTransientAttachment | vk::ImageUsageFlagBits::eColorAttachment, vk::MemoryPropertyFlagBits::eLazilyAllocated, sampleCount);
	MSColorImage.createImageView(device, vk::ImageAspectFlagBits::eColor);

	MSDepthImage.format = depth.format;
	MSDepthImage.createImage(device, gpu, surface.actualExtent.width, surface.actualExtent.height, vk::ImageTiling::eOptimal,
		vk::ImageUsageFlagBits::eTransientAttachment | vk::ImageUsageFlagBits::eDepthStencilAttachment, vk::MemoryPropertyFlagBits::eLazilyAllocated, sampleCount);
	MSDepthImage.createImageView(device, vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil);

	std::vector<vk::Framebuffer> _frameBuffers(swapchain.images.size());

	for (size_t i = 0; i < _frameBuffers.size(); ++i) {
		std::array<vk::ImageView, 4> attachments = {
			MSColorImage.view,
			swapchain.images[i].view,
			MSDepthImage.view,
			depth.view
		};

		auto const fbci = vk::FramebufferCreateInfo()
			.setRenderPass(renderPass)
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
			swapchain.images[i].view,
			position.view,
			normal.view,
			albedo.view,
			specular.view,
			depth.view,
			finalColor.view,
			finalNormal.view,
			finalDepth.view
		};

		auto const fbci = vk::FramebufferCreateInfo()
			.setRenderPass(dRenderPass)
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
			.setRenderPass(rRenderPass)
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
			.setRenderPass(guiRenderPass)
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

Pipeline Context::createPipeline(const specificGraphicsPipelineCreateInfo& specificInfo)
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
		vk::CullModeFlagBits::eFront,					// CullModeFlags cullMode;
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
	std::vector<vk::PipelineColorBlendAttachmentState> colorBlendAttachments = { colorBlendAttachment , colorBlendAttachment, colorBlendAttachment };
	_pipeline.pipeinfo.pColorBlendState = &vk::PipelineColorBlendStateCreateInfo{
		vk::PipelineColorBlendStateCreateFlags{},				// PipelineColorBlendStateCreateFlags flags;
		VK_FALSE,												// Bool32 logicOpEnable; // this must be false is we want alpha blend
		vk::LogicOp::eCopy,										// LogicOp logicOp;
		3,														// uint32_t attachmentCount;
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
				vk::DescriptorType::eInputAttachment,	//DescriptorType descriptorType;
				1,										//uint32_t descriptorCount;
				vk::ShaderStageFlagBits::eFragment,		//ShaderStageFlags stageFlags;
				nullptr									//const Sampler* pImmutableSamplers;
			},
			// Binding 1: Normal input attachment 
			vk::DescriptorSetLayoutBinding{
				1,										//uint32_t binding;
				vk::DescriptorType::eInputAttachment,	//DescriptorType descriptorType;
				1,										//uint32_t descriptorCount;
				vk::ShaderStageFlagBits::eFragment,		//ShaderStageFlags stageFlags;
				nullptr									//const Sampler* pImmutableSamplers;
			},
			// Binding 2: Albedo input attachment 
			vk::DescriptorSetLayoutBinding{
				2,										//uint32_t binding;
				vk::DescriptorType::eInputAttachment,	//DescriptorType descriptorType;
				1,										//uint32_t descriptorCount;
				vk::ShaderStageFlagBits::eFragment,		//ShaderStageFlags stageFlags;
				nullptr									//const Sampler* pImmutableSamplers;
			},
			// Binding 3: Specular input attachment 
			vk::DescriptorSetLayoutBinding{
				3,										//uint32_t binding;
				vk::DescriptorType::eInputAttachment,	//DescriptorType descriptorType;
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
			}
		};

		vk::DescriptorSetLayoutCreateInfo descriptorLayout = vk::DescriptorSetLayoutCreateInfo{
			vk::DescriptorSetLayoutCreateFlags(),		//DescriptorSetLayoutCreateFlags flags;
			(uint32_t)setLayoutBindings.size(),			//uint32_t bindingCount;
			setLayoutBindings.data()					//const DescriptorSetLayoutBinding* pBindings;
		};

		VkCheck(device.createDescriptorSetLayout(&descriptorLayout, nullptr, &DSLayoutComposition));

	}

	std::vector<vk::DescriptorSetLayout> descriptorSetLayouts = { DSLayoutComposition, Shadows::getDescriptorSetLayout(device) };
	VkCheck(device.createPipelineLayout(
		&vk::PipelineLayoutCreateInfo{ vk::PipelineLayoutCreateFlags(), (uint32_t)descriptorSetLayouts.size(), descriptorSetLayouts.data(), 0, &vk::PushConstantRange() },
		nullptr,
		&_pipeline.pipeinfo.layout));
	std::cout << "Pipeline Layout created\n";

	// Render Pass
	_pipeline.pipeinfo.renderPass = dRenderPass;

	// Subpass (Index of subpass this pipeline will be used in)
	_pipeline.pipeinfo.subpass = 1;

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
		vk::CullModeFlagBits::eFront,					// CullModeFlags cullMode;
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

		VkCheck(device.createDescriptorSetLayout(&descriptorLayout, nullptr, &DSLayoutReflection));

	}

	std::vector<vk::DescriptorSetLayout> descriptorSetLayouts = { DSLayoutReflection, Shadows::getDescriptorSetLayout(device) };
	VkCheck(device.createPipelineLayout(
		&vk::PipelineLayoutCreateInfo{ vk::PipelineLayoutCreateFlags(), (uint32_t)descriptorSetLayouts.size(), descriptorSetLayouts.data(), 0, &vk::PushConstantRange() },
		nullptr,
		&_pipeline.pipeinfo.layout));
	std::cout << "Pipeline Layout created\n";

	// Render Pass
	_pipeline.pipeinfo.renderPass = rRenderPass;

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
			},
			// Binding 1 (out)
			vk::DescriptorSetLayoutBinding{
				1,										//uint32_t binding;
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
		VkCheck(device.createDescriptorSetLayout(&descriptorLayout, nullptr, &DSLayoutCompute));
	}
	std::vector<vk::DescriptorSetLayout> descriptorSetLayouts = { DSLayoutCompute };
	VkCheck(device.createPipelineLayout(
		&vk::PipelineLayoutCreateInfo{ vk::PipelineLayoutCreateFlags(), (uint32_t)descriptorSetLayouts.size(), descriptorSetLayouts.data(), 0, &vk::PushConstantRange() },
		nullptr,
		&_pipeline.compinfo.layout));
	std::cout << "Compute Pipeline Layout created\n";

	VkCheck(device.createComputePipelines(nullptr, 1, &_pipeline.compinfo, nullptr, &_pipeline.pipeline));

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
	MSColorImage.destroy(device);
	MSDepthImage.destroy(device);
	for (auto& fb : frameBuffers)
		device.destroyFramebuffer(fb);
	pipeline.destroy(device);
	pipelineGUI.destroy(device);
	pipelineSkyBox.destroy(device);
	pipelineTerrain.destroy(device);
	device.destroyRenderPass(renderPass);
	swapchain.destroy(device);

	// Recreate resources
	surface.actualExtent = { width, height };
	swapchain = createSwapchain();
	depth = createDepthResources();
	renderPass = createRenderPass(); // ?
	frameBuffers = createFrameBuffers();
	pipelineTerrain = createPipeline(getPipelineSpecificationsTerrain());
	pipelineSkyBox = createPipeline(getPipelineSpecificationsSkyBox());
	pipelineGUI = createPipeline(getPipelineSpecificationsGUI());
	pipeline = createPipeline(getPipelineSpecificationsModel());
}