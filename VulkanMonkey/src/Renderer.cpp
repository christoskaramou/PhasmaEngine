#include "../include/Renderer.h"
#include "../include/Errors.h"
#include <iostream>
#include <fstream>
#include <memory>
#include <random>
#include <chrono>

Renderer::Renderer(SDL_Window* window)
{
	info.window = window;

	initVulkanContext();

	info.skyBox = SkyBox::loadSkyBox({ "objects/sky/right.png", "objects/sky/left.png", "objects/sky/top.png", "objects/sky/bottom.png", "objects/sky/back.png", "objects/sky/front.png" }, 1024, &info, true);
	info.gui = GUI::loadGUI("objects/gui/gui.png", &info, false);
	info.terrain = Terrain::generateTerrain("", &info);
	info.models.push_back(Model::loadModel("objects/sponza/", "sponza.obj", &info));

	// some random light colors and positions
	info.light.resize(info.gpuProperties.limits.maxPushConstantsSize / sizeof(Light));
	for (auto& light : info.light) {
		light = { glm::vec4(rand(0.f,1.f), rand(0.0f,1.f), rand(0.f,1.f), rand(0.f,1.f)),
			glm::vec4(rand(-1.f,1.f), rand(.3f,1.f),rand(-.5f,.5f), 1.f),
			glm::vec4(2.f, 1.f, 1.f, 1.f) };
	}
	info.light[0] = { glm::vec4(1.f, 1.f, 1.f, .5f),
		glm::vec4(.5f, 1.2f, 0.f, 1.f),
		glm::vec4(0.f, 0.f, 1.f, 1.f) };
}

Renderer::~Renderer()
{
    info.device.waitIdle();

    for (auto &semaphore : info.semaphores){
        if (semaphore){
            info.device.destroySemaphore(semaphore);
            std::cout << "Semaphore destroyed\n";
        }
    }

    if (info.descriptorPool){
        info.device.destroyDescriptorPool(info.descriptorPool);
        std::cout << "DescriptorPool destroyed\n";
    }

	info.skyBox.destroy(&info);
	info.terrain.destroy(&info);
	info.gui.destroy(&info);
	for(auto& shadows : info.shadows)
		shadows.destroy(&info);

	for (auto &model : info.models)
		model.destroy(&info);

	info.pipelineTerrain.destroy(&info);
	info.pipelineSkyBox.destroy(&info);
	info.pipelineGUI.destroy(&info);
	info.pipelineShadows.destroy(&info);
	info.pipeline.destroy(&info);

	info.multiSampleColorImage.destroy(&info);
	info.multiSampleDepthImage.destroy(&info);

	if (Shadows::descriptorSetLayout) {
		info.device.destroyDescriptorSetLayout(Shadows::descriptorSetLayout);
		std::cout << "Descriptor Set Layout destroyed\n";
	}

	if (SkyBox::descriptorSetLayout) {
		info.device.destroyDescriptorSetLayout(SkyBox::descriptorSetLayout);
		std::cout << "Descriptor Set Layout destroyed\n";
	}

	if (GUI::descriptorSetLayout) {
		info.device.destroyDescriptorSetLayout(GUI::descriptorSetLayout);
		std::cout << "Descriptor Set Layout destroyed\n";
	}

	if (Terrain::descriptorSetLayout) {
		info.device.destroyDescriptorSetLayout(Terrain::descriptorSetLayout);
		std::cout << "Descriptor Set Layout destroyed\n";
	}

	if (Mesh::descriptorSetLayout) {
		info.device.destroyDescriptorSetLayout(Mesh::descriptorSetLayout);
		std::cout << "Descriptor Set Layout destroyed\n";
	}

    if (Model::descriptorSetLayout){
        info.device.destroyDescriptorSetLayout(Model::descriptorSetLayout);
        std::cout << "Descriptor Set Layout destroyed\n";
    }

    for (auto &frameBuffer : info.frameBuffers){
        if (frameBuffer){
            info.device.destroyFramebuffer(frameBuffer);
            std::cout << "Frame Buffer destroyed\n";
        }
    }

    info.depth.destroy(&info);

	if (Shadows::renderPass) {
		info.device.destroyRenderPass(Shadows::renderPass);
		std::cout << "RenderPass destroyed\n";
	}

    if (info.renderPass){
        info.device.destroyRenderPass(info.renderPass);
        std::cout << "RenderPass destroyed\n";
    }

    if (info.commandPool){
        info.device.destroyCommandPool(info.commandPool);
        std::cout << "CommandPool destroyed\n";
    }
	
	info.swapchain.destroy(info.device);

    if (info.device){
        info.device.destroy();
        std::cout << "Device destroyed\n";
    }

    if (info.surface.surface){
        info.instance.destroySurfaceKHR(info.surface.surface);
        std::cout << "Surface destroyed\n";
    }

    if (info.instance){
		info.instance.destroy();
        std::cout << "Instance destroyed\n";
    }
}

void Renderer::initVulkanContext()
{
	info.instance = createInstance();
	info.surface = createSurface();
	info.gpu = findGpu();
	info.device = createDevice();
	info.semaphores = createSemaphores(3);
	info.swapchain = createSwapchain();
	info.commandPool = createCommandPool();
	info.depth = createDepthResources();
	info.renderPass = createRenderPass();
	info.frameBuffers = createFrameBuffers();
	info.dynamicCmdBuffer = createCmdBuffer();
	info.shadowsPassCmdBuffers = createCmdBuffers(1);
	info.descriptorPool = createDescriptorPool(500); // max number of all descriptor sets to allocate	
	info.shadows.resize(1);
	for (auto& shadows : info.shadows) {
		shadows.createFrameBuffer(&info);
		shadows.createUniformBuffer(&info);
		shadows.createDescriptorSet(Shadows::getDescriptorSetLayout(&info), &info);
	}

	// create pipelines
	info.pipelineShadows = createPipeline(Shadows::getPipelineSpecifications(&info));
	info.pipelineSkyBox = createPipeline(SkyBox::getPipelineSpecifications(&info));
	info.pipelineGUI = createPipeline(GUI::getPipelineSpecifications(&info));
	info.pipelineTerrain = createPipeline(Terrain::getPipelineSpecifications(&info));
	info.pipeline = createPipeline(Model::getPipelineSpecifications(&info));
}

vk::Instance Renderer::createInstance()
{
    vk::Instance _instance;
    unsigned extCount;
    if (!SDL_Vulkan_GetInstanceExtensions(info.window, &extCount, nullptr))
    {
        std::cout << SDL_GetError();
        exit(-1);
    }
    std::vector<const char*> instanceExtensions(extCount);
    if(!SDL_Vulkan_GetInstanceExtensions(info.window, &extCount, instanceExtensions.data()))
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

Surface Renderer::createSurface()
{
    VkSurfaceKHR _vkSurface;
    if(!SDL_Vulkan_CreateSurface(info.window, VkInstance(info.instance), &_vkSurface))
    {
        std::cout << SDL_GetError();
        exit(-2);
    }
	Surface _surface;
	_surface.surface = vk::SurfaceKHR(_vkSurface);

	return _surface;
}

vk::PhysicalDevice Renderer::findGpu()
{
    uint32_t gpuCount = 0;
	info.instance.enumeratePhysicalDevices(&gpuCount, nullptr);
    std::vector<vk::PhysicalDevice> gpuList(gpuCount);
	info.instance.enumeratePhysicalDevices(&gpuCount, gpuList.data());

    for (const auto& gpu : gpuList) {
        uint32_t familyCount = 0;
		gpu.getQueueFamilyProperties(&familyCount, nullptr);
		std::vector<vk::QueueFamilyProperties> properties(familyCount);
		gpu.getQueueFamilyProperties(&familyCount, properties.data());

		for (uint32_t i = 0; i < familyCount; ++i) {
			//find graphics queue family index
			if (properties[i].queueFlags & vk::QueueFlagBits::eGraphics)
				info.graphicsFamilyId = i;
			// find present queue family index
			vk::Bool32 presentSupport = false;
			gpu.getSurfaceSupportKHR(i, info.surface.surface, &presentSupport);
			if (properties[i].queueCount > 0 && presentSupport)
				info.presentFamilyId = i;

			if (info.graphicsFamilyId >= 0 && info.presentFamilyId >= 0){
                gpu.getProperties(&info.gpuProperties);
                gpu.getFeatures(&info.gpuFeatures);

				// 1. surface capabilities
				gpu.getSurfaceCapabilitiesKHR(info.surface.surface, &info.surface.capabilities);

				// 2. surface format
				uint32_t formatCount = 0;
				std::vector<vk::SurfaceFormatKHR> formats{};
				gpu.getSurfaceFormatsKHR(info.surface.surface, &formatCount, nullptr);
				if (formatCount == 0) {
					std::cout << "Surface formats missing\n";
					exit(-5);
				}
				formats.resize(formatCount);
				gpu.getSurfaceFormatsKHR(info.surface.surface, &formatCount, formats.data());

				// 3. presentation mode
				uint32_t presentCount = 0;
				std::vector<vk::PresentModeKHR> presentModes{};
				gpu.getSurfacePresentModesKHR(info.surface.surface, &presentCount, nullptr);
				if (presentCount == 0) {
					std::cout << "Surface formats missing\n";
					exit(-5);
				}
				presentModes.resize(presentCount);
				gpu.getSurfacePresentModesKHR(info.surface.surface, &presentCount, presentModes.data());

				for (const auto& i : presentModes) {
					if (i == vk::PresentModeKHR::eImmediate || i == vk::PresentModeKHR::eMailbox) {
						info.surface.presentModeKHR = i;
						break;
					}
					else info.surface.presentModeKHR = vk::PresentModeKHR::eFifo;
				}
				for (const auto& i : formats) {
					if (i.format == vk::Format::eB8G8R8A8Unorm && i.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) {
						info.surface.formatKHR = i;
						break;
					}
					else info.surface.formatKHR = formats[0];
				}
                return gpu;
			}
		}
    }

    return nullptr;
}

vk::Device Renderer::createDevice()
{
    vk::Device _device;

	uint32_t count;
	info.gpu.enumerateDeviceExtensionProperties(nullptr, &count, nullptr);
	std::vector<vk::ExtensionProperties> extensionProperties(count);
	info.gpu.enumerateDeviceExtensionProperties(nullptr, &count, extensionProperties.data());

	std::vector<const char*> deviceExtensions{};
	for (auto& i : extensionProperties) {
		if (std::string(i.extensionName) == VK_KHR_SWAPCHAIN_EXTENSION_NAME && i.specVersion < 110)
			deviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
	}

    float priorities[]{ 1.0f }; // range : [0.0, 1.0]
	auto const queueCreateInfo = vk::DeviceQueueCreateInfo()
		.setQueueFamilyIndex(info.graphicsFamilyId)
		.setQueueCount(1)
		.setPQueuePriorities(priorities);
	auto const deviceCreateInfo = vk::DeviceCreateInfo()
		.setQueueCreateInfoCount(1)
		.setPQueueCreateInfos(&queueCreateInfo)
		.setEnabledLayerCount(0)
		.setPpEnabledLayerNames(nullptr)
		.setEnabledExtensionCount((uint32_t)deviceExtensions.size())
		.setPpEnabledExtensionNames(deviceExtensions.data())
		.setPEnabledFeatures(&info.gpuFeatures);

    VkCheck(info.gpu.createDevice(&deviceCreateInfo, nullptr, &_device));
    std::cout << "Device created\n";

    //get the graphics queue handler
	info.graphicsQueue = _device.getQueue(info.graphicsFamilyId, 0);
	info.presentQueue = _device.getQueue(info.presentFamilyId, 0);
	if (!info.graphicsQueue || !info.presentQueue)
		return nullptr;

    return _device;
}

Swapchain Renderer::createSwapchain()
{
    Swapchain _swapchain;
   
    uint32_t swapchainImageCount = info.surface.capabilities.minImageCount + 1;
    if (info.surface.capabilities.maxImageCount > 0 &&
        swapchainImageCount > info.surface.capabilities.maxImageCount) {
        swapchainImageCount = info.surface.capabilities.maxImageCount;
    }

    vk::SwapchainKHR oldSwapchain = _swapchain.swapchain;
	auto const swapchainCreateInfo = vk::SwapchainCreateInfoKHR()
		.setSurface(info.surface.surface)
		.setMinImageCount(swapchainImageCount)
		.setImageFormat(info.surface.formatKHR.format)
		.setImageColorSpace(info.surface.formatKHR.colorSpace)
		.setImageExtent(info.surface.capabilities.currentExtent)
		.setImageArrayLayers(1)
		.setImageUsage(vk::ImageUsageFlagBits::eColorAttachment)
		.setPreTransform(info.surface.capabilities.currentTransform)
		.setCompositeAlpha(vk::CompositeAlphaFlagBitsKHR::eOpaque)
		.setPresentMode(info.surface.presentModeKHR)
		.setClipped(VK_TRUE)
		.setOldSwapchain(oldSwapchain);

	// new swapchain with old create info
    vk::SwapchainKHR newSwapchain;
    VkCheck(info.device.createSwapchainKHR(&swapchainCreateInfo, nullptr, &newSwapchain));
    std::cout << "Swapchain created\n";

    if (_swapchain.swapchain)
		info.device.destroySwapchainKHR(_swapchain.swapchain);
    _swapchain.swapchain = newSwapchain;

    // get the swapchain image handlers
	std::vector<vk::Image> images{};
	info.device.getSwapchainImagesKHR(_swapchain.swapchain, &swapchainImageCount, nullptr);
    images.resize(swapchainImageCount);
	info.device.getSwapchainImagesKHR(_swapchain.swapchain, &swapchainImageCount, images.data());

	_swapchain.images.resize(images.size());
	for (unsigned i = 0; i < images.size(); i++)
		_swapchain.images[i].image = images[i]; // hold the image handlers

	// create image views for each swapchain image
	for (auto &image : _swapchain.images) {
		auto const imageViewCreateInfo = vk::ImageViewCreateInfo()
			.setImage(image.image)
			.setViewType(vk::ImageViewType::e2D)
			.setFormat(info.surface.formatKHR.format)
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
		VkCheck(info.device.createImageView(&imageViewCreateInfo, nullptr, &image.view));
		std::cout << "Swapchain ImageView created\n";
	}

    return _swapchain;
}

vk::CommandPool Renderer::createCommandPool()
{
    vk::CommandPool _commandPool;
	auto const cpci = vk::CommandPoolCreateInfo()
		.setQueueFamilyIndex(info.graphicsFamilyId)
		.setFlags(vk::CommandPoolCreateFlagBits::eTransient | vk::CommandPoolCreateFlagBits::eResetCommandBuffer);
    VkCheck(info.device.createCommandPool(&cpci, nullptr, &_commandPool));
    std::cout << "CommandPool created\n";
    return _commandPool;
}

vk::RenderPass Renderer::createRenderPass()
{
    vk::RenderPass _renderPass;
	std::array<vk::AttachmentDescription, 4> attachments = {};

	// for Multisampling
	attachments[0] = vk::AttachmentDescription() // color attachment disc
		.setFormat(info.surface.formatKHR.format)
		.setSamples(info.sampleCount)
		.setLoadOp(vk::AttachmentLoadOp::eClear)
		.setStoreOp(vk::AttachmentStoreOp::eStore)
		.setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
		.setStencilStoreOp(vk::AttachmentStoreOp::eDontCare)
		.setInitialLayout(vk::ImageLayout::eUndefined)
		.setFinalLayout(vk::ImageLayout::eColorAttachmentOptimal);

	// the multisampled image will be resolved to this image and presented to swapchain
	attachments[1] = vk::AttachmentDescription() // color attachment disc
		.setFormat(info.surface.formatKHR.format)
		.setSamples(vk::SampleCountFlagBits::e1)
		.setLoadOp(vk::AttachmentLoadOp::eDontCare)
		.setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
		.setStencilStoreOp(vk::AttachmentStoreOp::eDontCare)
		.setStoreOp(vk::AttachmentStoreOp::eStore)
		.setInitialLayout(vk::ImageLayout::eUndefined)
		.setFinalLayout(vk::ImageLayout::ePresentSrcKHR);

	// multisampled depth
	attachments[2] = vk::AttachmentDescription()
		.setFormat(info.depth.format)
		.setSamples(info.sampleCount)
		.setLoadOp(vk::AttachmentLoadOp::eClear)
		.setStoreOp(vk::AttachmentStoreOp::eDontCare)
		.setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
		.setStencilStoreOp(vk::AttachmentStoreOp::eDontCare)
		.setInitialLayout(vk::ImageLayout::eUndefined)
		.setFinalLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal);

	// Depth resolve
	attachments[3] = vk::AttachmentDescription()
		.setFormat(info.depth.format)
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

    VkCheck(info.device.createRenderPass(&rpci, nullptr, &_renderPass));
    std::cout << "RenderPass created\n";

    return _renderPass;
}

Image Renderer::createDepthResources()
{
	Image _image;
	_image.format = vk::Format::eUndefined;
	std::vector<vk::Format> candidates = { vk::Format::eD32SfloatS8Uint, vk::Format::eD32Sfloat, vk::Format::eD24UnormS8Uint };
	for (auto &format : candidates) {
		vk::FormatProperties props = info.gpu.getFormatProperties(format);
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

	_image.createImage(&info, info.surface.capabilities.currentExtent.width, info.surface.capabilities.currentExtent.height,
        vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eDepthStencilAttachment, vk::MemoryPropertyFlagBits::eDeviceLocal);
	_image.createImageView(&info, vk::ImageAspectFlagBits::eDepth);
	_image.transitionImageLayout(&info, vk::ImageLayout::eUndefined, vk::ImageLayout::eDepthStencilAttachmentOptimal);

    return _image;
}

std::vector<vk::Framebuffer> Renderer::createFrameBuffers()
{
	assert((VkSampleCountFlags(info.gpuProperties.limits.framebufferColorSampleCounts) >= VkSampleCountFlags(info.sampleCount))
		&& (VkSampleCountFlags(info.gpuProperties.limits.framebufferDepthSampleCounts) >= VkSampleCountFlags(info.sampleCount)));

	info.multiSampleColorImage.format = info.surface.formatKHR.format;
	info.multiSampleColorImage.createImage(&info, info.surface.capabilities.currentExtent.width, info.surface.capabilities.currentExtent.height, vk::ImageTiling::eOptimal,
		vk::ImageUsageFlagBits::eTransientAttachment | vk::ImageUsageFlagBits::eColorAttachment, vk::MemoryPropertyFlagBits::eLazilyAllocated, info.sampleCount);
	info.multiSampleColorImage.createImageView(&info, vk::ImageAspectFlagBits::eColor);

	info.multiSampleDepthImage.format = info.depth.format;
	info.multiSampleDepthImage.createImage(&info, info.surface.capabilities.currentExtent.width, info.surface.capabilities.currentExtent.height, vk::ImageTiling::eOptimal,
		vk::ImageUsageFlagBits::eTransientAttachment | vk::ImageUsageFlagBits::eDepthStencilAttachment, vk::MemoryPropertyFlagBits::eLazilyAllocated, info.sampleCount);
	info.multiSampleDepthImage.createImageView(&info, vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil);

    std::vector<vk::Framebuffer> _frameBuffers(info.swapchain.images.size());

    for (size_t i = 0; i < _frameBuffers.size(); ++i) {
        std::array<vk::ImageView, 4> attachments = { info.multiSampleColorImage.view, info.swapchain.images[i].view, info.multiSampleDepthImage.view, info.depth.view };

		auto const fbci = vk::FramebufferCreateInfo()
			.setRenderPass(info.renderPass)
			.setAttachmentCount(static_cast<uint32_t>(attachments.size()))
			.setPAttachments(attachments.data())
			.setWidth(info.surface.capabilities.currentExtent.width)
			.setHeight(info.surface.capabilities.currentExtent.height)
			.setLayers(1);
        VkCheck(info.device.createFramebuffer(&fbci, nullptr, &_frameBuffers[i]));
        std::cout << "Framebuffer created\n";
    }

    return _frameBuffers;
}

std::vector<vk::CommandBuffer> Renderer::createCmdBuffers(uint32_t bufferCount)
{
    std::vector<vk::CommandBuffer> _cmdBuffers(bufferCount);
	auto const cbai = vk::CommandBufferAllocateInfo()
		.setCommandPool(info.commandPool)
		.setLevel(vk::CommandBufferLevel::ePrimary)
		.setCommandBufferCount(bufferCount);
    VkCheck(info.device.allocateCommandBuffers(&cbai, _cmdBuffers.data()));
    std::cout << "Command Buffers allocated\n";

    return _cmdBuffers;
}

vk::CommandBuffer Renderer::createCmdBuffer()
{
	vk::CommandBuffer _cmdBuffer;
	auto const cbai = vk::CommandBufferAllocateInfo()
		.setCommandPool(info.commandPool)
		.setLevel(vk::CommandBufferLevel::ePrimary)
		.setCommandBufferCount(1);
	VkCheck(info.device.allocateCommandBuffers(&cbai, &_cmdBuffer));
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

Pipeline Renderer::createPipeline(const specificGraphicsPipelineCreateInfo& specificInfo)
{
	Pipeline _pipeline;

	// Shader stages
	vk::ShaderModule vertModule;
	vk::ShaderModule fragModule;

	auto vertCode = readFile(specificInfo.shaders[0]);
	std::vector<char> fragCode{};
	if (specificInfo.shaders.size()>1)
		fragCode = readFile(specificInfo.shaders[1]);


	auto vsmci = vk::ShaderModuleCreateInfo{
		vk::ShaderModuleCreateFlags(),						// ShaderModuleCreateFlags flags;
		vertCode.size(),									// size_t codeSize;
		reinterpret_cast<const uint32_t*>(vertCode.data())	// const uint32_t* pCode;
	};
	vk::ShaderModuleCreateInfo fsmci;
	if (specificInfo.shaders.size() > 1) {
		fsmci = vk::ShaderModuleCreateInfo{
			vk::ShaderModuleCreateFlags(),						// ShaderModuleCreateFlags flags;
			fragCode.size(),									// size_t codeSize;
			reinterpret_cast<const uint32_t*>(fragCode.data())	// const uint32_t* pCode;
		};
	}

	VkCheck(info.device.createShaderModule(&vsmci, nullptr, &vertModule));
	std::cout << "Vertex Shader Module created\n";
	if (specificInfo.shaders.size() > 1) {
		VkCheck(info.device.createShaderModule(&fsmci, nullptr, &fragModule));
		std::cout << "Fragment Shader Module created\n";
	}

	_pipeline.info.stageCount = (uint32_t)specificInfo.shaders.size();

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
		stages[1].pSpecializationInfo = nullptr;
	}
	_pipeline.info.pStages = stages.data();

    // Vertex Input state
	_pipeline.info.pVertexInputState = &vk::PipelineVertexInputStateCreateInfo{ 
		vk::PipelineVertexInputStateCreateFlags(),
		(uint32_t)specificInfo.vertexInputBindingDescriptions.size(),
		specificInfo.vertexInputBindingDescriptions.data(),
		(uint32_t)specificInfo.vertexInputAttributeDescriptions.size(),
		specificInfo.vertexInputAttributeDescriptions.data()
	};

    // Input Assembly stage
	_pipeline.info.pInputAssemblyState = &vk::PipelineInputAssemblyStateCreateInfo{
        vk::PipelineInputAssemblyStateCreateFlags(),		// PipelineInputAssemblyStateCreateFlags flags;
		vk::PrimitiveTopology::eTriangleList,				// PrimitiveTopology topology;
		VK_FALSE											// Bool32 primitiveRestartEnable;
    };

    // Viewports and Scissors
	_pipeline.info.pViewportState = &vk::PipelineViewportStateCreateInfo{
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
	_pipeline.info.pRasterizationState = &vk::PipelineRasterizationStateCreateInfo{
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
	_pipeline.info.pMultisampleState = &vk::PipelineMultisampleStateCreateInfo{
        vk::PipelineMultisampleStateCreateFlags(),	// PipelineMultisampleStateCreateFlags flags;
		specificInfo.sampleCount,					// SampleCountFlagBits rasterizationSamples;
		VK_FALSE,									// Bool32 sampleShadingEnable;
		1.0f,										// float minSampleShading;
		nullptr,									// const SampleMask* pSampleMask;
		VK_FALSE,									// Bool32 alphaToCoverageEnable;
		VK_FALSE									// Bool32 alphaToOneEnable;
    };

    // Depth ans stencil state
	_pipeline.info.pDepthStencilState = &vk::PipelineDepthStencilStateCreateInfo{
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
	if (specificInfo.useBlendState) {
		_pipeline.info.pColorBlendState = &vk::PipelineColorBlendStateCreateInfo{
			vk::PipelineColorBlendStateCreateFlags{},				// PipelineColorBlendStateCreateFlags flags;
			VK_FALSE,												// Bool32 logicOpEnable; // this muse be false is we want alpha blend
			vk::LogicOp::eCopy,										// LogicOp logicOp;
			1,														// uint32_t attachmentCount;
			&vk::PipelineColorBlendAttachmentState{		// const PipelineColorBlendAttachmentState* pAttachments;
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
	}
	else
		_pipeline.info.pColorBlendState = nullptr;

    // Dynamic state
	_pipeline.info.pDynamicState = nullptr;

	// Push Constant Range
	auto pushConstantRange = specificInfo.pushConstantRange;
	uint32_t numOfConsts = pushConstantRange == vk::PushConstantRange() ? 0 : 1;

    // Pipeline Layout
	VkCheck(info.device.createPipelineLayout(
		&vk::PipelineLayoutCreateInfo{ vk::PipelineLayoutCreateFlags(), (uint32_t)specificInfo.descriptorSetLayouts.size(), specificInfo.descriptorSetLayouts.data(), numOfConsts, &pushConstantRange },
		nullptr,
		&_pipeline.info.layout));
    std::cout << "Pipeline Layout created\n";

    // Render Pass
	_pipeline.info.renderPass = specificInfo.renderPass;

    // Subpass
	_pipeline.info.subpass = 0;

    // Base Pipeline Handle
	_pipeline.info.basePipelineHandle = nullptr;

    // Base Pipeline Index
	_pipeline.info.basePipelineIndex = -1;

    VkCheck(info.device.createGraphicsPipelines(nullptr, 1, &_pipeline.info, nullptr, &_pipeline.pipeline));
    std::cout << "Pipeline created\n";

    // destroy Shader Modules
	info.device.destroyShaderModule(vertModule);
	info.device.destroyShaderModule(fragModule);

    return _pipeline;
}

void Renderer::update(float delta)
{
	ProjView proj_view;
	proj_view.projection = info.mainCamera.getPerspective();
	proj_view.view = info.mainCamera.getView();

	//TERRAIN
	UBO mvpTerrain;
	mvpTerrain.projection = proj_view.projection;
	mvpTerrain.view = proj_view.view;
	mvpTerrain.model = glm::mat4(1.0f);
	memcpy(info.terrain.uniformBuffer.data, &mvpTerrain, sizeof(UBO));

	// MODELS
	UBO mvp;
	mvp.projection = proj_view.projection;
	mvp.view = proj_view.view;
	for (auto &model : info.models) {
		mvp.model = model.matrix;
		memcpy(model.uniformBuffer.data, &mvp, sizeof(UBO));
	}

	// SKYBOX
	memcpy(info.skyBox.uniformBuffer.data, &proj_view, 2 * sizeof(glm::mat4));

	// SHADOWS
	float FOV = 90.0f;
	for (auto& shadows : info.shadows) {
		ShadowsUBO shadows_UBO;
		shadows_UBO.projection = glm::perspective(glm::radians(FOV), 1.f, 0.01f, 50.0f);
		glm::vec3 lightPos = glm::vec3(info.light[0].position);
		glm::vec3 center = -lightPos;
		shadows_UBO.view = glm::lookAt(lightPos, center, info.mainCamera.worldUp);
		shadows_UBO.model = mvp.model;
		shadows_UBO.castShadows = Shadows::shadowCast ? 1.0f : 0.0f;
		memcpy(shadows.uniformBuffer.data, &shadows_UBO, sizeof(ShadowsUBO));
	}
}

vk::DescriptorPool Renderer::createDescriptorPool(uint32_t maxDescriptorSets)
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

	auto const createInfo = vk::DescriptorPoolCreateInfo()
		.setPoolSizeCount((uint32_t)descPoolsize.size())
		.setPPoolSizes(descPoolsize.data())
		.setMaxSets(maxDescriptorSets);

	VkCheck(info.device.createDescriptorPool(&createInfo, nullptr, &_descriptorPool));
	std::cout << "DescriptorPool created\n";

	return _descriptorPool;
}

std::vector<vk::Semaphore> Renderer::createSemaphores(uint32_t semaphoresCount)
{
    std::vector<vk::Semaphore> _semaphores(semaphoresCount);
    auto const si = vk::SemaphoreCreateInfo();

	for (uint32_t i = 0; i < semaphoresCount; i++) {
		VkCheck(info.device.createSemaphore(&si, nullptr, &_semaphores[i]));
		std::cout << "Semaphore created\n";
	}

    return _semaphores;
}

float Renderer::rand(float x1, float x2)
{
	static auto seed = std::chrono::system_clock::now().time_since_epoch().count();
	static std::default_random_engine gen(seed);
	std::uniform_real_distribution<float> x(x1, x2);
	return x(gen);
}

void Renderer::recordDynamicCmdBuffer(const uint32_t imageIndex)
{
	// Render Pass (color)
	std::array<vk::ClearValue, 3> clearValues = {};
	clearValues[0].setColor(vk::ClearColorValue().setFloat32({ 0.0f, 0.749f, 1.0f }));
	clearValues[1].setColor(vk::ClearColorValue().setFloat32({ 0.0f, 0.749f, 1.0f }));
	clearValues[2].setDepthStencil({ 1.0f, 0 });

	auto beginInfo = vk::CommandBufferBeginInfo()
		.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit)
		.setPInheritanceInfo(nullptr);
	auto renderPassInfo = vk::RenderPassBeginInfo()
		.setRenderPass(info.renderPass)
		.setFramebuffer(info.frameBuffers[imageIndex])
		.setRenderArea({ { 0, 0 }, info.surface.capabilities.currentExtent })
		.setClearValueCount((uint32_t)clearValues.size())
		.setPClearValues(clearValues.data());

	VkCheck(info.dynamicCmdBuffer.begin(&beginInfo));
	info.dynamicCmdBuffer.beginRenderPass(&renderPassInfo, vk::SubpassContents::eInline);

	vk::DeviceSize offset = vk::DeviceSize();

	// TERRAIN
	if (info.terrain.enabled)
	{
		const vk::DescriptorSet descriptorSets[] = { info.terrain.descriptorSet };
		info.dynamicCmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, info.pipelineTerrain.pipeline);
		info.dynamicCmdBuffer.bindVertexBuffers(0, 1, &info.terrain.vertexBuffer.buffer, &offset);
		info.dynamicCmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, info.pipelineTerrain.info.layout, 0, 1, descriptorSets, 0, nullptr);
		info.dynamicCmdBuffer.draw((uint32_t)info.terrain.vertices.size()/12, 1, 0, 0);
	}

	// MODELS
	if (info.models.size() > 0)
	{
		info.dynamicCmdBuffer.pushConstants(info.pipeline.info.layout, vk::ShaderStageFlagBits::eFragment, 0, static_cast<uint32_t>(info.light.size() * sizeof(Light)), info.light.data());
		info.dynamicCmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, info.pipeline.pipeline);
		for (auto& model : info.models) {
			if (model.enabled) {
				info.dynamicCmdBuffer.bindVertexBuffers(0, 1, &model.vertexBuffer.buffer, &offset);
				info.dynamicCmdBuffer.bindIndexBuffer(model.indexBuffer.buffer, 0, vk::IndexType::eUint32);
				uint32_t index = 0;
				uint32_t vertex = 0;
				for (auto& mesh : model.meshes) {
					if (mesh.colorEffects.diffuse.a >= 1.f) {
						const vk::DescriptorSet descriptorSets[] = { model.descriptorSet, mesh.descriptorSet, info.shadows[0].descriptorSet};
						info.dynamicCmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, info.pipeline.info.layout, 0, 3, descriptorSets, 0, nullptr);
						info.dynamicCmdBuffer.drawIndexed((uint32_t)mesh.indices.size(), 1, index, vertex, 0);
					}
					index += (uint32_t)mesh.indices.size();
					vertex += (uint32_t)mesh.vertices.size();
				}
				index = 0;
				vertex = 0;
				for (auto& mesh : model.meshes) {
					if (mesh.colorEffects.diffuse.a < 1.f) {
						const vk::DescriptorSet descriptorSets[] = { model.descriptorSet, mesh.descriptorSet, info.shadows[0].descriptorSet};
						info.dynamicCmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, info.pipeline.info.layout, 0, 3, descriptorSets, 0, nullptr);
						info.dynamicCmdBuffer.drawIndexed((uint32_t)mesh.indices.size(), 1, index, vertex, 0);
					}
					index += (uint32_t)mesh.indices.size();
					vertex += (uint32_t)mesh.vertices.size();
				}
			}
		}
	}

	// SKYBOX
	if (info.skyBox.enabled)
	{
		info.dynamicCmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, info.pipelineSkyBox.pipeline);
		info.dynamicCmdBuffer.bindVertexBuffers(0, 1, &info.skyBox.vertexBuffer.buffer, &offset);
		info.dynamicCmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, info.pipelineSkyBox.info.layout, 0, 1, &info.skyBox.descriptorSet, 0, nullptr);
		info.dynamicCmdBuffer.draw((uint32_t)info.skyBox.vertices.size()/4, 1, 0, 0);
	}

	// GUI
	if (info.gui.enabled)
	{
		info.dynamicCmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, info.pipelineGUI.pipeline);
		info.dynamicCmdBuffer.bindVertexBuffers(0, 1, &info.gui.vertexBuffer.buffer, &offset);
		info.dynamicCmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, info.pipelineGUI.info.layout, 0, 1, &info.gui.descriptorSet, 0, nullptr);
		info.dynamicCmdBuffer.draw((uint32_t)info.gui.vertices.size()/5, 1, 0, 0);
	}

	info.dynamicCmdBuffer.endRenderPass();
	info.dynamicCmdBuffer.end();
}

void Renderer::recordShadowsCmdBuffers()
{
	// Render Pass (shadows mapping) (outputs the depth image with the light POV)
	for (uint32_t i=0; i<info.shadowsPassCmdBuffers.size();i++){
		std::array<vk::ClearValue, 1> clearValuesShadows = {};
		clearValuesShadows[0].setDepthStencil({ 1.0f, 0 });
		auto beginInfoShadows = vk::CommandBufferBeginInfo()
			.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit)
			.setPInheritanceInfo(nullptr);
		auto renderPassInfoShadows = vk::RenderPassBeginInfo()
			.setRenderPass(Shadows::getRenderPass(&info))
			.setFramebuffer(info.shadows[i].frameBuffer)
			.setRenderArea({ { 0, 0 },{ info.shadows[i].imageSize, info.shadows[i].imageSize } })
			.setClearValueCount((uint32_t)clearValuesShadows.size())
			.setPClearValues(clearValuesShadows.data());

		VkCheck(info.shadowsPassCmdBuffers[i].begin(&beginInfoShadows));
		info.shadowsPassCmdBuffers[i].beginRenderPass(&renderPassInfoShadows, vk::SubpassContents::eInline);

		vk::DeviceSize offset = vk::DeviceSize();

		info.shadowsPassCmdBuffers[i].bindPipeline(vk::PipelineBindPoint::eGraphics, info.pipelineShadows.pipeline);
		info.shadowsPassCmdBuffers[i].bindDescriptorSets(vk::PipelineBindPoint::eGraphics, info.pipelineShadows.info.layout, 0, 1, &info.shadows[i].descriptorSet, 0, nullptr);

		if (info.models.size() > 0)
		{
			for (auto& model : info.models) {
				if (model.enabled) {
					info.shadowsPassCmdBuffers[i].bindVertexBuffers(0, 1, &model.vertexBuffer.buffer, &offset);
					info.shadowsPassCmdBuffers[i].bindIndexBuffer(model.indexBuffer.buffer, 0, vk::IndexType::eUint32);
					uint32_t index = 0;
					uint32_t vertex = 0;

					for (auto& mesh : model.meshes) {
						if (mesh.colorEffects.diffuse.a >= 1.f) {
							info.shadowsPassCmdBuffers[i].drawIndexed((uint32_t)mesh.indices.size(), 1, index, vertex, 0);
						}
						index += (uint32_t)mesh.indices.size();
						vertex += (uint32_t)mesh.vertices.size();
					}
					index = 0;
					vertex = 0;
					for (auto& mesh : model.meshes) {
						if (mesh.colorEffects.diffuse.a < 1.f) {
							info.shadowsPassCmdBuffers[i].drawIndexed((uint32_t)mesh.indices.size(), 1, index, vertex, 0);
						}
						index += (uint32_t)mesh.indices.size();
						vertex += (uint32_t)mesh.vertices.size();
					}
				}
			}
		}
		info.shadowsPassCmdBuffers[i].endRenderPass();
		info.shadowsPassCmdBuffers[i].end();
	}
}

void Renderer::draw()
{
	// what stage of a pipeline at a command buffer to wait for the semaphores to be done until keep going
	vk::PipelineStageFlags waitStages[] = { vk::PipelineStageFlagBits::eColorAttachmentOutput };

    uint32_t imageIndex;

	// if using shadows use the semaphore[0], record and submit the shadow commands, else use the semaphore[1]
	if (Shadows::shadowCast) {
		VkCheck(info.device.acquireNextImageKHR(info.swapchain.swapchain, UINT64_MAX, info.semaphores[0], vk::Fence(), &imageIndex));
		recordShadowsCmdBuffers();
		auto const siShadows = vk::SubmitInfo()
			.setWaitSemaphoreCount(1)
			.setPWaitSemaphores(&info.semaphores[0])
			.setPWaitDstStageMask(waitStages)
			.setCommandBufferCount((uint32_t)info.shadowsPassCmdBuffers.size())
			.setPCommandBuffers(info.shadowsPassCmdBuffers.data())
			.setSignalSemaphoreCount(1)
			.setPSignalSemaphores(&info.semaphores[1]);
		VkCheck(info.graphicsQueue.submit(1, &siShadows, nullptr));
	}
	else
		VkCheck(info.device.acquireNextImageKHR(info.swapchain.swapchain, UINT64_MAX, info.semaphores[1], vk::Fence(), &imageIndex));

	// use the dynamic command buffer
	recordDynamicCmdBuffer(imageIndex);

	// submit the main command buffer
	auto const si = vk::SubmitInfo()
		.setWaitSemaphoreCount(1)
		.setPWaitSemaphores(&info.semaphores[1])
		.setPWaitDstStageMask(waitStages)
		.setCommandBufferCount(1)
		.setPCommandBuffers(&info.dynamicCmdBuffer)
		.setSignalSemaphoreCount(1)
		.setPSignalSemaphores(&info.semaphores[2]);
	VkCheck(info.graphicsQueue.submit(1, &si, nullptr));

    // Presentation
	auto const pi = vk::PresentInfoKHR()
		.setWaitSemaphoreCount(1)
		.setPWaitSemaphores(&info.semaphores[2])
		.setSwapchainCount(1)
		.setPSwapchains(&info.swapchain.swapchain)
		.setPImageIndices(&imageIndex)
		.setPResults(nullptr); //optional
	VkCheck(info.presentQueue.presentKHR(&pi));
	info.presentQueue.waitIdle();
}





