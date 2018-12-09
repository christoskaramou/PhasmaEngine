#include "Context.h"
#include <iostream>
#include <fstream>

using namespace vm;

std::vector<Model> Context::models = {};

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
	vulkan.commandPoolCompute = createComputeCommadPool();
	vulkan.descriptorPool = createDescriptorPool(2000); // max number of all descriptor sets to allocate
	vulkan.dynamicCmdBuffer = createCmdBuffers(1)[0];
	vulkan.shadowCmdBuffer = createCmdBuffers(1)[0];
	vulkan.computeCmdBuffer = createComputeCmdBuffer();
	vulkan.depth = new Image(createDepthResources());
}

void Context::initRendering()
{
	addRenderTarget("position", vk::Format::eR32G32B32A32Sfloat);
	addRenderTarget("normal", vk::Format::eR16G16B16A16Sfloat);
	addRenderTarget("albedo", vk::Format::eR8G8B8A8Unorm);
	addRenderTarget("srm", vk::Format::eR16G16B16A16Sfloat); // Specular Roughness Metallic
	addRenderTarget("ssao", vk::Format::eR16Sfloat);
	addRenderTarget("ssaoBlur", vk::Format::eR16Sfloat);
	addRenderTarget("ssr", vk::Format::eR16G16B16A16Sfloat);
	addRenderTarget("composition", vk::Format::eR8G8B8A8Unorm);

	// render passes
	forward.renderPass = createRenderPass();
	deferred.renderPass = createDeferredRenderPass();
	deferred.compositionRenderPass = createCompositionRenderPass();
	ssao.renderPass = createSSAORenderPass();
	ssao.blurRenderPass = createSSAOBlurRenderPass();
	ssr.renderPass = createSSRRenderPass();
	motionBlur.renderPass = createMotionBlurRenderPass();
	gui.renderPass = createGUIRenderPass();

	// frame buffers
	forward.frameBuffers = createFrameBuffers();
	deferred.frameBuffers = createDeferredFrameBuffers();
	deferred.compositionFrameBuffers = createCompositionFrameBuffers();
	ssao.frameBuffers = createSSAOFrameBuffers();
	ssao.blurFrameBuffers = createSSAOBlurFrameBuffers();
	ssr.frameBuffers = createSSRFrameBuffers();
	motionBlur.frameBuffers = createMotionBlurFrameBuffers();
	gui.frameBuffers = createGUIFrameBuffers();
	shadows.createFrameBuffers(static_cast<uint32_t>(vulkan.swapchain->images.size()));

	// pipelines
	PipelineInfo gpi = getPipelineSpecificationsModel();
	gpi.specializationInfo = vk::SpecializationInfo{ 1, &vk::SpecializationMapEntry{ 0, 0, sizeof(MAX_LIGHTS) }, sizeof(MAX_LIGHTS), &MAX_LIGHTS };

	forward.pipeline = createPipeline(gpi);
	//terrain.pipeline = createPipeline(getPipelineSpecificationsTerrain());
	shadows.pipeline = createPipeline(getPipelineSpecificationsShadows());
	//skyBox.pipeline = createPipeline(getPipelineSpecificationsSkyBox());
	gui.pipeline = createPipeline(getPipelineSpecificationsGUI());
	deferred.pipeline = createPipeline(getPipelineSpecificationsDeferred());
	deferred.pipelineComposition = createCompositionPipeline();
	ssao.pipeline = createSSAOPipeline();
	ssao.pipelineBlur = createSSAOBlurPipeline();
	ssr.pipeline = createSSRPipeline();
	motionBlur.pipeline = createMotionBlurPipeline();
	compute.pipeline = createComputePipeline();

	//camera.push_back(Camera());
}

void Context::loadResources()
{
	// SKYBOX LOAD
	//std::array<std::string, 6> skyTextures = { "objects/sky/right.png", "objects/sky/left.png", "objects/sky/top.png", "objects/sky/bottom.png", "objects/sky/back.png", "objects/sky/front.png" };
	//skyBox.loadSkyBox(skyTextures, 1024);
	// GUI LOAD
	gui.loadGUI();
	// TERRAIN LOAD
	//terrain.generateTerrain("");
	// MODELS LOAD
	//models.push_back(Model(&vulkan));
	//models.back().loadModel("objects/sponza/", "sponza.obj");
}

void Context::createUniforms()
{
	// DESCRIPTOR SETS FOR GUI
	gui.createDescriptorSet(GUI::getDescriptorSetLayout(vulkan.device));
	// DESCRIPTOR SETS FOR TERRAIN
	//terrain.createDescriptorSet(Terrain::getDescriptorSetLayout(vulkan.device));
	// DESCRIPTOR SETS FOR SKYBOX
	//skyBox.createDescriptorSet(SkyBox::getDescriptorSetLayout(vulkan.device));
	// DESCRIPTOR SETS FOR SHADOWS
	shadows.createDynamicUniformBuffer(10); // TODO: Fix to dynamic add or remove memory
	shadows.createDescriptorSet();
	// DESCRIPTOR SETS FOR LIGHTS
	lightUniforms.createLightUniforms();
	// DESCRIPTOR SETS FOR SSAO
	ssao.createSSAOUniforms(renderTargets);
	// DESCRIPTOR SETS FOR COMPOSITION PIPELINE
	deferred.createDeferredUniforms(renderTargets, lightUniforms);
	// DESCRIPTOR SET FOR REFLECTION PIPELINE
	ssr.createSSRUniforms(renderTargets);
	// DESCRIPTOR SET FOR MOTIONBLUR PIPELINE
	motionBlur.createMotionBlurUniforms(renderTargets);
	// DESCRIPTOR SET FOR COMPUTE PIPELINE
	compute.createComputeUniforms();
}

vk::Instance Context::createInstance()
{
	unsigned extCount;
	if (!SDL_Vulkan_GetInstanceExtensions(vulkan.window, &extCount, nullptr))
	{
		std::cout << SDL_GetError();
		exit(-1);
	}
	std::vector<const char*> instanceExtensions(extCount);
	if (!SDL_Vulkan_GetInstanceExtensions(vulkan.window, &extCount, instanceExtensions.data()))
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

	return vk::createInstance(instInfo);
}

Surface Context::createSurface()
{
	VkSurfaceKHR _vkSurface;
	if (!SDL_Vulkan_CreateSurface(vulkan.window, VkInstance(vulkan.instance), &_vkSurface))
	{
		std::cout << SDL_GetError();
		exit(-2);
	}
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
		if (i.format == vk::Format::eR8G8B8A8Unorm && i.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear)
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
	auto const queueCreateInfo = vk::DeviceQueueCreateInfo()
		.setQueueFamilyIndex(vulkan.graphicsFamilyId)
		.setQueueCount(1)
		.setPQueuePriorities(priorities);
	auto const deviceCreateInfo = vk::DeviceCreateInfo()
		.setQueueCreateInfoCount(1)
		.setPQueueCreateInfos(&queueCreateInfo)
		.setEnabledLayerCount(0)
		.setPpEnabledLayerNames(nullptr)
		.setEnabledExtensionCount((uint32_t)deviceExtensions.size())
		.setPpEnabledExtensionNames(deviceExtensions.data())
		.setPEnabledFeatures(&vulkan.gpuFeatures);

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
	return vulkan.device.getQueue(vulkan.computeFamilyId, 0);;
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
	auto const swapchainCreateInfo = vk::SwapchainCreateInfoKHR()
		.setSurface(vulkan.surface->surface)
		.setMinImageCount(swapchainImageCount)
		.setImageFormat(vulkan.surface->formatKHR.format)
		.setImageColorSpace(vulkan.surface->formatKHR.colorSpace)
		.setImageExtent(extent)
		.setImageArrayLayers(1)
		.setImageUsage(vk::ImageUsageFlagBits::eColorAttachment)
		.setPreTransform(vulkan.surface->capabilities.currentTransform)
		.setCompositeAlpha(vk::CompositeAlphaFlagBitsKHR::eOpaque)
		.setPresentMode(vulkan.surface->presentModeKHR)
		.setClipped(VK_TRUE)
		.setOldSwapchain(oldSwapchain);

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
		auto const imageViewCreateInfo = vk::ImageViewCreateInfo()
			.setImage(image.image)
			.setViewType(vk::ImageViewType::e2D)
			.setFormat(vulkan.surface->formatKHR.format)
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
		image.view = vulkan.device.createImageView(imageViewCreateInfo);
	}

	return _swapchain;
}

vk::CommandPool Context::createCommandPool()
{
	auto const cpci = vk::CommandPoolCreateInfo()
		.setQueueFamilyIndex(vulkan.graphicsFamilyId)
		.setFlags(vk::CommandPoolCreateFlagBits::eTransient | vk::CommandPoolCreateFlagBits::eResetCommandBuffer);

	return vulkan.device.createCommandPool(cpci);
}

vk::CommandPool Context::createComputeCommadPool()
{
	auto const cpci = vk::CommandPoolCreateInfo()
		.setQueueFamilyIndex(vulkan.computeFamilyId)
		.setFlags(vk::CommandPoolCreateFlagBits::eTransient | vk::CommandPoolCreateFlagBits::eResetCommandBuffer);

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
	renderTargets[name].createImage(vulkan.surface->actualExtent.width, vulkan.surface->actualExtent.height, vk::ImageTiling::eOptimal,
		vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled, vk::MemoryPropertyFlagBits::eDeviceLocal);
	renderTargets[name].createImageView(vk::ImageAspectFlagBits::eColor);
	renderTargets[name].createSampler();
}

vk::RenderPass Context::createRenderPass()
{
	std::array<vk::AttachmentDescription, 4> attachments = {};

	// for Multisampling
	attachments[0] = vk::AttachmentDescription() // color attachment disc
		.setFormat(vulkan.surface->formatKHR.format)
		.setSamples(vulkan.sampleCount)
		.setLoadOp(vk::AttachmentLoadOp::eClear)
		.setStoreOp(vk::AttachmentStoreOp::eStore)
		.setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
		.setStencilStoreOp(vk::AttachmentStoreOp::eDontCare)
		.setInitialLayout(vk::ImageLayout::eUndefined)
		.setFinalLayout(vk::ImageLayout::eColorAttachmentOptimal);

	// the multisampled image will be resolved to this image and presented to swapchain
	attachments[1] = vk::AttachmentDescription() // color attachment disc
		.setFormat(vulkan.surface->formatKHR.format)
		.setSamples(vk::SampleCountFlagBits::e1)
		.setLoadOp(vk::AttachmentLoadOp::eDontCare)
		.setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
		.setStencilStoreOp(vk::AttachmentStoreOp::eDontCare)
		.setStoreOp(vk::AttachmentStoreOp::eStore)
		.setInitialLayout(vk::ImageLayout::eUndefined)
		.setFinalLayout(vk::ImageLayout::ePresentSrcKHR);

	// multisampled depth
	attachments[2] = vk::AttachmentDescription()
		.setFormat(vulkan.depth->format)
		.setSamples(vulkan.sampleCount)
		.setLoadOp(vk::AttachmentLoadOp::eClear)
		.setStoreOp(vk::AttachmentStoreOp::eStore)
		.setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
		.setStencilStoreOp(vk::AttachmentStoreOp::eStore)
		.setInitialLayout(vk::ImageLayout::eUndefined)
		.setFinalLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal);

	// Depth resolve
	attachments[3] = vk::AttachmentDescription()
		.setFormat(vulkan.depth->format)
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

	return vulkan.device.createRenderPass(rpci);
}

vk::RenderPass Context::createDeferredRenderPass()
{
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
	// Specular Roughness Metallic
	attachments[3].format = renderTargets["srm"].format;
	attachments[3].samples = vk::SampleCountFlagBits::e1;
	attachments[3].loadOp = vk::AttachmentLoadOp::eClear;
	attachments[3].storeOp = vk::AttachmentStoreOp::eStore;
	attachments[3].stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
	attachments[3].stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
	attachments[3].initialLayout = vk::ImageLayout::eUndefined;
	attachments[3].finalLayout = vk::ImageLayout::eColorAttachmentOptimal;

	// Depth
	attachments[4].format = vulkan.depth->format;
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

	return vulkan.device.createRenderPass(renderPassInfo);
}

vk::RenderPass Context::createCompositionRenderPass()
{
	std::array<vk::AttachmentDescription, 2> attachments{};
	// Color target
	attachments[0].format = vulkan.surface->formatKHR.format;
	attachments[0].samples = vk::SampleCountFlagBits::e1;
	attachments[0].loadOp = vk::AttachmentLoadOp::eClear;
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

	return vulkan.device.createRenderPass(renderPassInfo);
}

vk::RenderPass Context::createMotionBlurRenderPass()
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
	{
		exit(-9);
	}
	_image.createImage(vulkan.surface->actualExtent.width, vulkan.surface->actualExtent.height,
		vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eDepthStencilAttachment, vk::MemoryPropertyFlagBits::eDeviceLocal);
	_image.createImageView(vk::ImageAspectFlagBits::eDepth);

	_image.addressMode = vk::SamplerAddressMode::eClampToEdge;
	_image.maxAnisotropy = 1.f;
	_image.borderColor = vk::BorderColor::eFloatOpaqueWhite;
	_image.samplerCompareEnable = VK_TRUE;
	_image.createSampler();

	_image.transitionImageLayout(vk::ImageLayout::eUndefined, vk::ImageLayout::eDepthStencilAttachmentOptimal);

	return _image;
}

std::vector<vk::Framebuffer> Context::createFrameBuffers()
{
	assert((VkSampleCountFlags(vulkan.gpuProperties.limits.framebufferColorSampleCounts) >= VkSampleCountFlags(vulkan.sampleCount))
		&& (VkSampleCountFlags(vulkan.gpuProperties.limits.framebufferDepthSampleCounts) >= VkSampleCountFlags(vulkan.sampleCount)));

	forward.MSColorImage.format = vulkan.surface->formatKHR.format;
	forward.MSColorImage.createImage(vulkan.surface->actualExtent.width, vulkan.surface->actualExtent.height, vk::ImageTiling::eOptimal,
		vk::ImageUsageFlagBits::eTransientAttachment | vk::ImageUsageFlagBits::eColorAttachment, vk::MemoryPropertyFlagBits::eLazilyAllocated, vulkan.sampleCount);
	forward.MSColorImage.createImageView(vk::ImageAspectFlagBits::eColor);

	forward.MSDepthImage.format = vulkan.depth->format;
	forward.MSDepthImage.createImage(vulkan.surface->actualExtent.width, vulkan.surface->actualExtent.height, vk::ImageTiling::eOptimal,
		vk::ImageUsageFlagBits::eTransientAttachment | vk::ImageUsageFlagBits::eDepthStencilAttachment, vk::MemoryPropertyFlagBits::eLazilyAllocated, vulkan.sampleCount);
	forward.MSDepthImage.createImageView(vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil);

	std::vector<vk::Framebuffer> _frameBuffers(vulkan.swapchain->images.size());

	for (size_t i = 0; i < _frameBuffers.size(); ++i) {
		std::array<vk::ImageView, 4> attachments = {
			forward.MSColorImage.view,
			vulkan.swapchain->images[i].view,
			forward.MSDepthImage.view,
			vulkan.depth->view
		};

		auto const fbci = vk::FramebufferCreateInfo()
			.setRenderPass(forward.renderPass)
			.setAttachmentCount(static_cast<uint32_t>(attachments.size()))
			.setPAttachments(attachments.data())
			.setWidth(vulkan.surface->actualExtent.width)
			.setHeight(vulkan.surface->actualExtent.height)
			.setLayers(1);
		_frameBuffers[i] = vulkan.device.createFramebuffer(fbci);
	}

	return _frameBuffers;
}

std::vector<vk::Framebuffer> Context::createDeferredFrameBuffers()
{
	std::vector<vk::Framebuffer> _frameBuffers(vulkan.swapchain->images.size());

	for (size_t i = 0; i < _frameBuffers.size(); ++i) {
		std::vector<vk::ImageView> attachments = {
			renderTargets["position"].view,
			renderTargets["normal"].view,
			renderTargets["albedo"].view,
			renderTargets["srm"].view,
			vulkan.depth->view
		};

		auto const fbci = vk::FramebufferCreateInfo()
			.setRenderPass(deferred.renderPass)
			.setAttachmentCount(static_cast<uint32_t>(attachments.size()))
			.setPAttachments(attachments.data())
			.setWidth(vulkan.surface->actualExtent.width)
			.setHeight(vulkan.surface->actualExtent.height)
			.setLayers(1);
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

		auto const fbci = vk::FramebufferCreateInfo()
			.setRenderPass(deferred.compositionRenderPass)
			.setAttachmentCount(static_cast<uint32_t>(attachments.size()))
			.setPAttachments(attachments.data())
			.setWidth(vulkan.surface->actualExtent.width)
			.setHeight(vulkan.surface->actualExtent.height)
			.setLayers(1);
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

		auto const fbci = vk::FramebufferCreateInfo()
			.setRenderPass(ssr.renderPass)
			.setAttachmentCount(static_cast<uint32_t>(attachments.size()))
			.setPAttachments(attachments.data())
			.setWidth(vulkan.surface->actualExtent.width)
			.setHeight(vulkan.surface->actualExtent.height)
			.setLayers(1);
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

		auto const fbci = vk::FramebufferCreateInfo()
			.setRenderPass(ssao.renderPass)
			.setAttachmentCount(static_cast<uint32_t>(attachments.size()))
			.setPAttachments(attachments.data())
			.setWidth(vulkan.surface->actualExtent.width)
			.setHeight(vulkan.surface->actualExtent.height)
			.setLayers(1);
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

		auto const fbci = vk::FramebufferCreateInfo()
			.setRenderPass(ssao.renderPass)
			.setAttachmentCount(static_cast<uint32_t>(attachments.size()))
			.setPAttachments(attachments.data())
			.setWidth(vulkan.surface->actualExtent.width)
			.setHeight(vulkan.surface->actualExtent.height)
			.setLayers(1);
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

		auto const fbci = vk::FramebufferCreateInfo()
			.setRenderPass(motionBlur.renderPass)
			.setAttachmentCount(static_cast<uint32_t>(attachments.size()))
			.setPAttachments(attachments.data())
			.setWidth(vulkan.surface->actualExtent.width)
			.setHeight(vulkan.surface->actualExtent.height)
			.setLayers(1);
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

		auto const fbci = vk::FramebufferCreateInfo()
			.setRenderPass(gui.renderPass)
			.setAttachmentCount(static_cast<uint32_t>(attachments.size()))
			.setPAttachments(attachments.data())
			.setWidth(vulkan.surface->actualExtent.width)
			.setHeight(vulkan.surface->actualExtent.height)
			.setLayers(1);
		_frameBuffers[i] = vulkan.device.createFramebuffer(fbci);
	}

	return _frameBuffers;
}

std::vector<vk::CommandBuffer> Context::createCmdBuffers(const uint32_t bufferCount)
{
	auto const cbai = vk::CommandBufferAllocateInfo()
		.setCommandPool(vulkan.commandPool)
		.setLevel(vk::CommandBufferLevel::ePrimary)
		.setCommandBufferCount(bufferCount);

	return vulkan.device.allocateCommandBuffers(cbai);
}

vk::CommandBuffer Context::createComputeCmdBuffer()
{
	vk::CommandBuffer _cmdBuffer;
	auto const cbai = vk::CommandBufferAllocateInfo()
		.setCommandPool(vulkan.commandPoolCompute)
		.setLevel(vk::CommandBufferLevel::ePrimary)
		.setCommandBufferCount(1);

	return vulkan.device.allocateCommandBuffers(cbai)[0];
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
		vertModule = vulkan.device.createShaderModule(vsmci);
		if (specificInfo.shaders.size() > 1) {
			fragModule = vulkan.device.createShaderModule(fsmci);
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
			specificInfo.depth.x,									// float minDepth;
			specificInfo.depth.y },									// float maxDepth;
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

	_pipeline.pipeline = vulkan.device.createGraphicsPipelines(nullptr, _pipeline.pipeinfo)[0];

	// destroy Shader Modules
	vulkan.device.destroyShaderModule(vertModule);
	vulkan.device.destroyShaderModule(fragModule);

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
		vertModule = vulkan.device.createShaderModule(vsmci);
		fragModule = vulkan.device.createShaderModule(fsmci);
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
			static_cast<float>(vulkan.surface->actualExtent.width), 	// float width;
			static_cast<float>(vulkan.surface->actualExtent.height),	// float height;
			0.0f,												// float minDepth;
			1.0f },												// float maxDepth;
		1,													// uint32_t scissorCount;
		&vk::Rect2D{										// const Rect2D* pScissors;
			vk::Offset2D(),										// Offset2D offset;
			vulkan.surface->actualExtent }								// Extent2D extent;
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
		vk::CompareOp::eGreater,								// CompareOp depthCompareOp;
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
	std::vector<vk::PipelineColorBlendAttachmentState> colorBlendAttachments = { colorBlendAttachment, colorBlendAttachment };
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
	_pipeline.pipeinfo.pDynamicState = nullptr;

	// Pipeline Layout
	if (!deferred.DSLayoutComposition)
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
			},
			// Binding 6: SSR image
			vk::DescriptorSetLayoutBinding{
				6,										//uint32_t binding;
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

		deferred.DSLayoutComposition  = vulkan.device.createDescriptorSetLayout(descriptorLayout);
	}

	std::vector<vk::DescriptorSetLayout> descriptorSetLayouts = { deferred.DSLayoutComposition, Shadows::getDescriptorSetLayout(vulkan.device) };

	vk::PushConstantRange pConstants = vk::PushConstantRange{ vk::ShaderStageFlagBits::eFragment, 0, sizeof(vec4) };

	_pipeline.pipeinfo.layout = vulkan.device.createPipelineLayout(
		vk::PipelineLayoutCreateInfo{ vk::PipelineLayoutCreateFlags(), (uint32_t)descriptorSetLayouts.size(), descriptorSetLayouts.data(), 1, &pConstants });

	// Render Pass
	_pipeline.pipeinfo.renderPass = deferred.compositionRenderPass;

	// Subpass (Index of subpass this pipeline will be used in)
	_pipeline.pipeinfo.subpass = 0;

	// Base Pipeline Handle
	_pipeline.pipeinfo.basePipelineHandle = nullptr;

	// Base Pipeline Index
	_pipeline.pipeinfo.basePipelineIndex = -1;

	_pipeline.pipeline = vulkan.device.createGraphicsPipelines(nullptr, _pipeline.pipeinfo)[0];

	// destroy Shader Modules
	vulkan.device.destroyShaderModule(vertModule);
	vulkan.device.destroyShaderModule(fragModule);

	return _pipeline;
}

Pipeline Context::createSSRPipeline()
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
		vertCode = readFile("shaders/SSR/vert.spv");
		fragCode = readFile("shaders/SSR/frag.spv");

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
		vertModule = vulkan.device.createShaderModule(vsmci);
		fragModule = vulkan.device.createShaderModule(fsmci);
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
			static_cast<float>(vulkan.surface->actualExtent.width), 	// float width;
			static_cast<float>(vulkan.surface->actualExtent.height),	// float height;
			0.0f,												// float minDepth;
			1.0f },												// float maxDepth;
		1,													// uint32_t scissorCount;
		&vk::Rect2D{										// const Rect2D* pScissors;
			vk::Offset2D(),										// Offset2D offset;
			vulkan.surface->actualExtent }								// Extent2D extent;
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
		vk::CompareOp::eGreater,								// CompareOp depthCompareOp;
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
	_pipeline.pipeinfo.pDynamicState = nullptr;

	// Pipeline Layout
	if (!ssr.DSLayoutReflection)
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

		ssr.DSLayoutReflection = vulkan.device.createDescriptorSetLayout(descriptorLayout);
	}

	std::vector<vk::DescriptorSetLayout> descriptorSetLayouts = { ssr.DSLayoutReflection };
	vk::PushConstantRange pConstants = vk::PushConstantRange{ vk::ShaderStageFlagBits::eFragment, 0, 4 * sizeof(float) };
	_pipeline.pipeinfo.layout = vulkan.device.createPipelineLayout(
		vk::PipelineLayoutCreateInfo{ vk::PipelineLayoutCreateFlags(), (uint32_t)descriptorSetLayouts.size(), descriptorSetLayouts.data(), 1, &pConstants } );

	// Render Pass
	_pipeline.pipeinfo.renderPass = ssr.renderPass;

	// Subpass (Index of subpass this pipeline will be used in)
	_pipeline.pipeinfo.subpass = 0;

	// Base Pipeline Handle
	_pipeline.pipeinfo.basePipelineHandle = nullptr;

	// Base Pipeline Index
	_pipeline.pipeinfo.basePipelineIndex = -1;

	_pipeline.pipeline = vulkan.device.createGraphicsPipelines(nullptr, _pipeline.pipeinfo)[0];

	// destroy Shader Modules
	vulkan.device.destroyShaderModule(vertModule);
	vulkan.device.destroyShaderModule(fragModule);

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
		vertModule = vulkan.device.createShaderModule(vsmci);
		fragModule = vulkan.device.createShaderModule(fsmci);
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
			static_cast<float>(vulkan.surface->actualExtent.width), 	// float width;
			static_cast<float>(vulkan.surface->actualExtent.height),	// float height;
			0.0f,												// float minDepth;
			1.0f },												// float maxDepth;
		1,													// uint32_t scissorCount;
		&vk::Rect2D{										// const Rect2D* pScissors;
			vk::Offset2D(),										// Offset2D offset;
			vulkan.surface->actualExtent }								// Extent2D extent;
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
		vk::CompareOp::eGreater,								// CompareOp depthCompareOp;
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
	_pipeline.pipeinfo.pDynamicState = nullptr;
	//&vk::PipelineDynamicStateCreateInfo{
	//		vk::PipelineDynamicStateCreateFlags(),			//PipelineDynamicStateCreateFlags flags;
	//		static_cast<uint32_t>(dynamicStates.size()),	//uint32_t dynamicStateCount;
	//		dynamicStates.data()							//const DynamicState* pDynamicStates;
	//};

	// Pipeline Layout
	if (!ssao.DSLayoutSSAO)
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

		ssao.DSLayoutSSAO = vulkan.device.createDescriptorSetLayout(descriptorLayout);
	}

	std::vector<vk::DescriptorSetLayout> descriptorSetLayouts = { ssao.DSLayoutSSAO };
	vk::PushConstantRange pConstants = vk::PushConstantRange{ vk::ShaderStageFlagBits::eFragment, 0, 4 * sizeof(float) };
	_pipeline.pipeinfo.layout = vulkan.device.createPipelineLayout(
		vk::PipelineLayoutCreateInfo{ vk::PipelineLayoutCreateFlags(), (uint32_t)descriptorSetLayouts.size(), descriptorSetLayouts.data(), 1, &pConstants } );

	// Render Pass
	_pipeline.pipeinfo.renderPass = ssao.renderPass;

	// Subpass (Index of subpass this pipeline will be used in)
	_pipeline.pipeinfo.subpass = 0;

	// Base Pipeline Handle
	_pipeline.pipeinfo.basePipelineHandle = nullptr;

	// Base Pipeline Index
	_pipeline.pipeinfo.basePipelineIndex = -1;

	_pipeline.pipeline = vulkan.device.createGraphicsPipelines(nullptr, _pipeline.pipeinfo)[0];

	// destroy Shader Modules
	vulkan.device.destroyShaderModule(vertModule);
	vulkan.device.destroyShaderModule(fragModule);

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
		vertModule = vulkan.device.createShaderModule(vsmci);
		fragModule = vulkan.device.createShaderModule(fsmci);
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
			static_cast<float>(vulkan.surface->actualExtent.width), 	// float width;
			static_cast<float>(vulkan.surface->actualExtent.height),	// float height;
			0.0f,												// float minDepth;
			1.0f },												// float maxDepth;
		1,													// uint32_t scissorCount;
		&vk::Rect2D{										// const Rect2D* pScissors;
			vk::Offset2D(),										// Offset2D offset;
			vulkan.surface->actualExtent }								// Extent2D extent;
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
		vk::CompareOp::eGreater,								// CompareOp depthCompareOp;
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
	_pipeline.pipeinfo.pDynamicState = nullptr;

	// Pipeline Layout
	if (!ssao.DSLayoutSSAOBlur)
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

		ssao.DSLayoutSSAOBlur = vulkan.device.createDescriptorSetLayout(descriptorLayout);

	}

	std::vector<vk::DescriptorSetLayout> descriptorSetLayouts = { ssao.DSLayoutSSAOBlur };
	vk::PushConstantRange pConstants = vk::PushConstantRange();//{ vk::ShaderStageFlagBits::eFragment, 0, 4 * sizeof(float) };
	_pipeline.pipeinfo.layout = vulkan.device.createPipelineLayout(
		vk::PipelineLayoutCreateInfo{ vk::PipelineLayoutCreateFlags(), (uint32_t)descriptorSetLayouts.size(), descriptorSetLayouts.data(), 0, &pConstants } );

	// Render Pass
	_pipeline.pipeinfo.renderPass = ssao.blurRenderPass;

	// Subpass (Index of subpass this pipeline will be used in)
	_pipeline.pipeinfo.subpass = 0;

	// Base Pipeline Handle
	_pipeline.pipeinfo.basePipelineHandle = nullptr;

	// Base Pipeline Index
	_pipeline.pipeinfo.basePipelineIndex = -1;

	_pipeline.pipeline = vulkan.device.createGraphicsPipelines(nullptr, _pipeline.pipeinfo)[0];

	// destroy Shader Modules
	vulkan.device.destroyShaderModule(vertModule);
	vulkan.device.destroyShaderModule(fragModule);

	return _pipeline;
}

Pipeline Context::createMotionBlurPipeline()
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
		vertCode = readFile("shaders/MotionBlur/vert.spv");
		fragCode = readFile("shaders/MotionBlur/frag.spv");

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
		vertModule = vulkan.device.createShaderModule(vsmci);
		fragModule = vulkan.device.createShaderModule(fsmci);
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
			static_cast<float>(vulkan.surface->actualExtent.width), 	// float width;
			static_cast<float>(vulkan.surface->actualExtent.height),	// float height;
			0.0f,												// float minDepth;
			1.0f },												// float maxDepth;
		1,													// uint32_t scissorCount;
		&vk::Rect2D{										// const Rect2D* pScissors;
			vk::Offset2D(),										// Offset2D offset;
			vulkan.surface->actualExtent }								// Extent2D extent;
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
		vk::CompareOp::eGreater,								// CompareOp depthCompareOp;
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
	_pipeline.pipeinfo.pDynamicState = nullptr;

	// Pipeline Layout
	if (!motionBlur.DSLayoutMotionBlur)
	{// DescriptorSetLayout
		std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings =
		{
			// Binding 0: Composition image Sampler 
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
			// Binding 4: Matrix Uniforms
			vk::DescriptorSetLayoutBinding{
				2,											//uint32_t binding;
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

		motionBlur.DSLayoutMotionBlur = vulkan.device.createDescriptorSetLayout(descriptorLayout);
	}

	std::vector<vk::DescriptorSetLayout> descriptorSetLayouts = { motionBlur.DSLayoutMotionBlur };
	vk::PushConstantRange pConstants = vk::PushConstantRange{ vk::ShaderStageFlagBits::eFragment, 0, 8 * sizeof(float) };
	_pipeline.pipeinfo.layout = vulkan.device.createPipelineLayout(
		vk::PipelineLayoutCreateInfo{ vk::PipelineLayoutCreateFlags(), (uint32_t)descriptorSetLayouts.size(), descriptorSetLayouts.data(), 1, &pConstants } );

	// Render Pass
	_pipeline.pipeinfo.renderPass = motionBlur.renderPass;

	// Subpass (Index of subpass this pipeline will be used in)
	_pipeline.pipeinfo.subpass = 0;

	// Base Pipeline Handle
	_pipeline.pipeinfo.basePipelineHandle = nullptr;

	// Base Pipeline Index
	_pipeline.pipeinfo.basePipelineIndex = -1;

	_pipeline.pipeline = vulkan.device.createGraphicsPipelines(nullptr, _pipeline.pipeinfo)[0];

	// destroy Shader Modules
	vulkan.device.destroyShaderModule(vertModule);
	vulkan.device.destroyShaderModule(fragModule);

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
		compModule = vulkan.device.createShaderModule(csmci);
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
		compute.DSLayoutCompute = vulkan.device.createDescriptorSetLayout(descriptorLayout);
	}
	std::vector<vk::DescriptorSetLayout> descriptorSetLayouts = { compute.DSLayoutCompute };
	_pipeline.compinfo.layout = vulkan.device.createPipelineLayout(
		vk::PipelineLayoutCreateInfo{ vk::PipelineLayoutCreateFlags(), (uint32_t)descriptorSetLayouts.size(), descriptorSetLayouts.data(), 0, &vk::PushConstantRange() } );

	_pipeline.pipeline = vulkan.device.createComputePipelines(nullptr, _pipeline.compinfo)[0];

	// destroy Shader Modules
	vulkan.device.destroyShaderModule(compModule);

	return _pipeline;
}

vk::DescriptorPool Context::createDescriptorPool(uint32_t maxDescriptorSets)
{
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

	return vulkan.device.createDescriptorPool(createInfo);
}

std::vector<vk::Fence> Context::createFences(const uint32_t fenceCount)
{
	std::vector<vk::Fence> _fences(fenceCount);
	auto const fi = vk::FenceCreateInfo();

	for (uint32_t i = 0; i < fenceCount; i++) {
		_fences[i] = vulkan.device.createFence(fi);
	}

	return _fences;
}

std::vector<vk::Semaphore> Context::createSemaphores(const uint32_t semaphoresCount)
{
	std::vector<vk::Semaphore> _semaphores(semaphoresCount);
	auto const si = vk::SemaphoreCreateInfo();

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
		}
	}
	for (auto &semaphore : vulkan.semaphores) {
		if (semaphore) {
			vulkan.device.destroySemaphore(semaphore);
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
	if (vulkan.commandPoolCompute) {
		vulkan.device.destroyCommandPool(vulkan.commandPoolCompute);
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

void Context::resizeViewport(uint32_t width, uint32_t height)
{
	vulkan.device.waitIdle();

	//- Free resources ----------------------
	// render targets
	for (auto& RT : renderTargets)
		RT.second.destroy();
	renderTargets.clear();

	// forward
	forward.destroy();

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

	// motion blur
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

	// terrain
	terrain.pipeline.destroy();
	
	// skybox
	skyBox.pipeline.destroy();

	vulkan.depth->destroy();
	vulkan.swapchain->destroy();
	//---------------------------------------

	//- Recreate resources ------------------
	vulkan.surface->actualExtent = { width, height };
	*vulkan.swapchain = createSwapchain();
	*vulkan.depth = createDepthResources();

	addRenderTarget("position", vk::Format::eR16G16B16A16Sfloat);
	addRenderTarget("normal", vk::Format::eR16G16B16A16Sfloat);
	addRenderTarget("albedo", vk::Format::eR8G8B8A8Unorm);
	addRenderTarget("srm", vk::Format::eR16G16B16A16Sfloat);
	addRenderTarget("ssao", vk::Format::eR16Sfloat);
	addRenderTarget("ssaoBlur", vk::Format::eR16Sfloat);
	addRenderTarget("ssr", vk::Format::eR16G16B16A16Sfloat);
	addRenderTarget("composition", vk::Format::eR8G8B8A8Unorm);


	forward.renderPass = createRenderPass();
	forward.frameBuffers = createFrameBuffers();
	PipelineInfo gpi = getPipelineSpecificationsModel();
	gpi.specializationInfo = vk::SpecializationInfo{ 1, &vk::SpecializationMapEntry{ 0, 0, sizeof(MAX_LIGHTS) }, sizeof(MAX_LIGHTS), &MAX_LIGHTS };
	forward.pipeline = createPipeline(gpi);

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

	terrain.pipeline = createPipeline(getPipelineSpecificationsTerrain());

	skyBox.pipeline = createPipeline(getPipelineSpecificationsSkyBox());
	//---------------------------------------
}

vk::DescriptorSetLayout Context::getDescriptorSetLayoutLights()
{
	// binding for model mvp matrix
	if (!lightUniforms.descriptorSetLayout) {
		std::vector<vk::DescriptorSetLayoutBinding> descriptorSetLayoutBinding{};
		descriptorSetLayoutBinding.push_back(vk::DescriptorSetLayoutBinding()
			.setBinding(0) // binding number in shader stages
			.setDescriptorCount(1) // number of descriptors contained
			.setDescriptorType(vk::DescriptorType::eUniformBuffer)
			.setStageFlags(vk::ShaderStageFlagBits::eFragment)); // which pipeline shader stages can access
		auto const createInfo = vk::DescriptorSetLayoutCreateInfo()
			.setBindingCount((uint32_t)descriptorSetLayoutBinding.size())
			.setPBindings(descriptorSetLayoutBinding.data());
		lightUniforms.descriptorSetLayout = vulkan.device.createDescriptorSetLayout(createInfo);
	}
	return lightUniforms.descriptorSetLayout;
}
PipelineInfo Context::getPipelineSpecificationsModel()
{
	// General Pipeline
	static PipelineInfo generalSpecific;
	generalSpecific.shaders = { "shaders/General/vert.spv", "shaders/General/frag.spv" };
	generalSpecific.renderPass = forward.renderPass;
	generalSpecific.viewportSize = { vulkan.surface->actualExtent.width, vulkan.surface->actualExtent.height };
	generalSpecific.descriptorSetLayouts = { Shadows::getDescriptorSetLayout(vulkan.device), Mesh::getDescriptorSetLayout(vulkan.device), Model::getDescriptorSetLayout(vulkan.device), getDescriptorSetLayoutLights() };
	generalSpecific.vertexInputBindingDescriptions = Vertex::getBindingDescriptionGeneral();
	generalSpecific.vertexInputAttributeDescriptions = Vertex::getAttributeDescriptionGeneral();
	generalSpecific.pushConstantRange = vk::PushConstantRange();
	generalSpecific.specializationInfo = vk::SpecializationInfo();
	generalSpecific.dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
	generalSpecific.dynamicStateInfo = {
		vk::PipelineDynamicStateCreateFlags(),
		static_cast<uint32_t>(generalSpecific.dynamicStates.size()),
		generalSpecific.dynamicStates.data()
	};

	return generalSpecific;
}

PipelineInfo Context::getPipelineSpecificationsShadows()
{
	// Shadows Pipeline
	static PipelineInfo shadowsSpecific;
	shadowsSpecific.shaders = { "shaders/Shadows/vert.spv" };
	shadowsSpecific.renderPass = shadows.getRenderPass();
	shadowsSpecific.viewportSize = { Shadows::imageSize, Shadows::imageSize };
	shadowsSpecific.useBlendState = false;
	shadowsSpecific.sampleCount = vk::SampleCountFlagBits::e1;
	shadowsSpecific.descriptorSetLayouts = { Shadows::getDescriptorSetLayout(vulkan.device) };
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
	skyBoxSpecific.renderPass = forward.renderPass;
	skyBoxSpecific.viewportSize = { vulkan.surface->actualExtent.width, vulkan.surface->actualExtent.height };
	skyBoxSpecific.descriptorSetLayouts = { SkyBox::getDescriptorSetLayout(vulkan.device) };
	skyBoxSpecific.vertexInputBindingDescriptions = Vertex::getBindingDescriptionSkyBox();
	skyBoxSpecific.vertexInputAttributeDescriptions = Vertex::getAttributeDescriptionSkyBox();
	skyBoxSpecific.sampleCount = vk::SampleCountFlagBits::e4;
	skyBoxSpecific.pushConstantRange = vk::PushConstantRange();
	skyBoxSpecific.dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
	skyBoxSpecific.dynamicStateInfo = {
		vk::PipelineDynamicStateCreateFlags(),
		static_cast<uint32_t>(skyBoxSpecific.dynamicStates.size()),
		skyBoxSpecific.dynamicStates.data()
	};


	return skyBoxSpecific;
}

PipelineInfo Context::getPipelineSpecificationsTerrain()
{
	// Terrain Pipeline
	static PipelineInfo terrainSpecific;
	terrainSpecific.shaders = { "shaders/Terrain/vert.spv", "shaders/Terrain/frag.spv" };
	terrainSpecific.renderPass = forward.renderPass;
	terrainSpecific.viewportSize = { vulkan.surface->actualExtent.width, vulkan.surface->actualExtent.height };
	terrainSpecific.descriptorSetLayouts = { Terrain::getDescriptorSetLayout(vulkan.device) };
	terrainSpecific.vertexInputBindingDescriptions = Vertex::getBindingDescriptionGeneral();
	terrainSpecific.vertexInputAttributeDescriptions = Vertex::getAttributeDescriptionGeneral();
	terrainSpecific.pushConstantRange = vk::PushConstantRange();
	terrainSpecific.dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
	terrainSpecific.dynamicStateInfo = {
		vk::PipelineDynamicStateCreateFlags(),
		static_cast<uint32_t>(terrainSpecific.dynamicStates.size()),
		terrainSpecific.dynamicStates.data()
	};

	return terrainSpecific;
}

PipelineInfo Context::getPipelineSpecificationsGUI()
{
	// GUI Pipeline
	static PipelineInfo GUISpecific;
	GUISpecific.shaders = { "shaders/GUI/vert.spv", "shaders/GUI/frag.spv" };
	GUISpecific.renderPass = gui.renderPass;
	GUISpecific.viewportSize = { vulkan.surface->actualExtent.width, vulkan.surface->actualExtent.height };
	GUISpecific.descriptorSetLayouts = { GUI::getDescriptorSetLayout(vulkan.device) };
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
	deferredSpecific.pushConstantRange = vk::PushConstantRange();//{ vk::ShaderStageFlagBits::eFragment, 0, sizeof(vec4) };
	deferredSpecific.shaders = { "shaders/Deferred/vert.spv", "shaders/Deferred/frag.spv" };
	deferredSpecific.renderPass = deferred.renderPass;
	deferredSpecific.viewportSize = { vulkan.surface->actualExtent.width, vulkan.surface->actualExtent.height };
	deferredSpecific.sampleCount = vk::SampleCountFlagBits::e1;
	deferredSpecific.descriptorSetLayouts = { Model::getDescriptorSetLayout(vulkan.device), Mesh::getDescriptorSetLayout(vulkan.device) };
	deferredSpecific.specializationInfo = vk::SpecializationInfo();
	deferredSpecific.blendAttachmentStates[0].blendEnable = VK_FALSE;
	deferredSpecific.blendAttachmentStates = {
		deferredSpecific.blendAttachmentStates[0],
		deferredSpecific.blendAttachmentStates[0],
		deferredSpecific.blendAttachmentStates[0],
		deferredSpecific.blendAttachmentStates[0]
	};
	deferredSpecific.dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
	deferredSpecific.dynamicStateInfo = {
		vk::PipelineDynamicStateCreateFlags(),
		static_cast<uint32_t>(deferredSpecific.dynamicStates.size()),
		deferredSpecific.dynamicStates.data()
	};

	return deferredSpecific;
}