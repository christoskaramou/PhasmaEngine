#include "Skybox.h"
#include "../GUI/GUI.h"

using namespace vm;

vk::DescriptorSetLayout SkyBox::descriptorSetLayout = nullptr;

vk::DescriptorSetLayout SkyBox::getDescriptorSetLayout()
{
	if (!descriptorSetLayout) {
		auto layoutBinding = [](uint32_t binding, vk::DescriptorType descriptorType, vk::ShaderStageFlags stageFlags) {
			return vk::DescriptorSetLayoutBinding{ binding, descriptorType, 1, stageFlags, nullptr };
		};
		std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings{
			layoutBinding(0, vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eVertex),
			layoutBinding(1, vk::DescriptorType::eCombinedImageSampler,vk::ShaderStageFlagBits::eFragment),
		};
		vk::DescriptorSetLayoutCreateInfo descriptorLayout;
		descriptorLayout.bindingCount = static_cast<uint32_t>(setLayoutBindings.size());
		descriptorLayout.pBindings = setLayoutBindings.data();
		descriptorSetLayout = VulkanContext::get().device.createDescriptorSetLayout(descriptorLayout);
	}
	return descriptorSetLayout;
}

void SkyBox::loadSkyBox(const std::array<std::string, 6>& textureNames, uint32_t imageSideSize, bool show)
{
	loadTextures(textureNames, imageSideSize);
	render = show;
#ifdef RENDER_SKYBOX
	float SIZE = 1.f;
	vertices = {
		-SIZE,  SIZE, -SIZE, 0.0f,
		-SIZE, -SIZE, -SIZE, 0.0f,
		 SIZE, -SIZE, -SIZE, 0.0f,
		 SIZE, -SIZE, -SIZE, 0.0f,
		 SIZE,  SIZE, -SIZE, 0.0f,
		-SIZE,  SIZE, -SIZE, 0.0f,

		-SIZE, -SIZE,  SIZE, 0.0f,
		-SIZE, -SIZE, -SIZE, 0.0f,
		-SIZE,  SIZE, -SIZE, 0.0f,
		-SIZE,  SIZE, -SIZE, 0.0f,
		-SIZE,  SIZE,  SIZE, 0.0f,
		-SIZE, -SIZE,  SIZE, 0.0f,

		 SIZE, -SIZE, -SIZE, 0.0f,
		 SIZE, -SIZE,  SIZE, 0.0f,
		 SIZE,  SIZE,  SIZE, 0.0f,
		 SIZE,  SIZE,  SIZE, 0.0f,
		 SIZE,  SIZE, -SIZE, 0.0f,
		 SIZE, -SIZE, -SIZE, 0.0f,

		-SIZE, -SIZE,  SIZE, 0.0f,
		-SIZE,  SIZE,  SIZE, 0.0f,
		 SIZE,  SIZE,  SIZE, 0.0f,
		 SIZE,  SIZE,  SIZE, 0.0f,
		 SIZE, -SIZE,  SIZE, 0.0f,
		-SIZE, -SIZE,  SIZE, 0.0f,

		-SIZE,  SIZE, -SIZE, 0.0f,
		 SIZE,  SIZE, -SIZE, 0.0f,
		 SIZE,  SIZE,  SIZE, 0.0f,
		 SIZE,  SIZE,  SIZE, 0.0f,
		-SIZE,  SIZE,  SIZE, 0.0f,
		-SIZE,  SIZE, -SIZE, 0.0f,

		-SIZE, -SIZE, -SIZE, 0.0f,
		-SIZE, -SIZE,  SIZE, 0.0f,
		 SIZE, -SIZE, -SIZE, 0.0f,
		 SIZE, -SIZE, -SIZE, 0.0f,
		-SIZE, -SIZE,  SIZE, 0.0f,
		 SIZE, -SIZE,  SIZE, 0.0f
	};
	createVertexBuffer();
#endif
}

// images must be squared and the image size must be the real else the assertion will fail
void SkyBox::loadTextures(const std::array<std::string, 6>& paths, int imageSideSize)
{
	assert(paths.size() == 6);

	texture.arrayLayers = 6;
	texture.format = vk::Format::eR8G8B8A8Unorm;
	texture.imageCreateFlags = vk::ImageCreateFlagBits::eCubeCompatible;
	texture.createImage(imageSideSize, imageSideSize, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eDeviceLocal);

	texture.transitionImageLayout(vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal);
	for (uint32_t i = 0; i < texture.arrayLayers; ++i) {
		// Texture Load
		int texWidth, texHeight, texChannels;
		//stbi_set_flip_vertically_on_load(true);
		stbi_uc* pixels = stbi_load(paths[i].c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
		assert(imageSideSize == texWidth && imageSideSize == texHeight);

		vk::DeviceSize imageSize = (size_t)texWidth * (size_t)texHeight * 4;
		if (!pixels)
			throw std::runtime_error("No pixel data loaded");

		Buffer staging;
		staging.createBuffer(imageSize, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
		staging.data = vulkan->device.mapMemory(staging.memory, vk::DeviceSize(), imageSize);
		memcpy(staging.data, pixels, static_cast<size_t>(imageSize));
		vulkan->device.unmapMemory(staging.memory);
		stbi_image_free(pixels);

		texture.copyBufferToImage(staging.buffer, i);
		staging.destroy();
	}
	texture.transitionImageLayout(vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal);

	texture.viewType = vk::ImageViewType::eCube;
	texture.createImageView(vk::ImageAspectFlagBits::eColor);

	texture.addressMode = vk::SamplerAddressMode::eClampToEdge;
	texture.createSampler();
}

void SkyBox::destroy()
{
	Object::destroy();
	pipeline.destroy();
	if (SkyBox::descriptorSetLayout) {
		vulkan->device.destroyDescriptorSetLayout(SkyBox::descriptorSetLayout);
		SkyBox::descriptorSetLayout = nullptr;
	}
	if (renderPass) {
		vulkan->device.destroyRenderPass(renderPass);
		renderPass = nullptr;
	}
	for (auto& frameBuffer : frameBuffers) {
		if (frameBuffer) {
			vulkan->device.destroyFramebuffer(frameBuffer);
		}
	}
}


#ifdef RENDER_SKYBOX
void SkyBox::update(Camera& camera)
{
	if (!render) return;

	// projection matrix correction here, because the given has reversed near and far
	mat4 projection = camera.projection;
	projection[2][2] = camera.nearPlane / (camera.nearPlane - camera.farPlane) * camera.worldOrientation.z;
	projection[3][2] = -(camera.nearPlane * camera.farPlane) / (camera.nearPlane - camera.farPlane);
	const mat4 pvm[2]{ projection, camera.view };
	memcpy(uniformBuffer.data, &pvm, sizeof(pvm));
}

void SkyBox::draw(uint32_t imageIndex)
{
	if (!render) return;

	vk::ClearValue clearColor;
	memcpy(clearColor.color.float32, GUI::clearColor.data(), 4 * sizeof(float));

	std::vector<vk::ClearValue> clearValues = { clearColor };

	vk::RenderPassBeginInfo renderPassInfo;
	renderPassInfo.renderPass = renderPass;
	renderPassInfo.framebuffer = frameBuffers[imageIndex];
	renderPassInfo.renderArea = { { 0, 0 }, vulkan->surface->actualExtent };
	renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
	renderPassInfo.pClearValues = clearValues.data();

	auto& cmd = vulkan->dynamicCmdBuffer;
	cmd.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);
	vk::DeviceSize offset{ 0 };
	cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline.pipeline);
	cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline.pipeinfo.layout, 0, descriptorSet, nullptr);
	cmd.bindVertexBuffers(0, vertexBuffer.buffer, offset);
	cmd.draw(static_cast<uint32_t>(vertices.size() * 0.25f), 1, 0, 0);
	cmd.endRenderPass();
}

void vm::SkyBox::createRenderPass()
{
	std::array<vk::AttachmentDescription, 1> attachments{};
	// Color attachment
	attachments[0].format = vulkan->surface->formatKHR.format;
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
	//renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
	//renderPassInfo.pDependencies = dependencies.data();

	renderPass = vulkan->device.createRenderPass(renderPassInfo);
}

void vm::SkyBox::createFrameBuffers()
{
	frameBuffers.resize(vulkan->swapchain->images.size());

	for (size_t i = 0; i < frameBuffers.size(); ++i) {
		std::vector<vk::ImageView> attachments = {
			vulkan->swapchain->images[i].view //renderTargets["albedo"].view
		};
		vk::FramebufferCreateInfo fbci;
		fbci.renderPass = renderPass;
		fbci.attachmentCount = static_cast<uint32_t>(attachments.size());
		fbci.pAttachments = attachments.data();
		fbci.width = WIDTH;
		fbci.height = HEIGHT;
		fbci.layers = 1;
		frameBuffers[i] = vulkan->device.createFramebuffer(fbci);
	}
}

void vm::SkyBox::createPipeline()
{
	// Shader stages
	std::vector<char> vertCode = readFile("shaders/SkyBox/vert.spv");
	vk::ShaderModuleCreateInfo vsmci;
	vsmci.codeSize = vertCode.size();
	vsmci.pCode = reinterpret_cast<const uint32_t*>(vertCode.data());
	vk::ShaderModule vertModule = vulkan->device.createShaderModule(vsmci);

	std::vector<char> fragCode = readFile("shaders/SkyBox/frag.spv");
	vk::ShaderModuleCreateInfo fsmci;
	fsmci.codeSize = fragCode.size();
	fsmci.pCode = reinterpret_cast<const uint32_t*>(fragCode.data());
	vk::ShaderModule fragModule = vulkan->device.createShaderModule(fsmci);

	vk::PipelineShaderStageCreateInfo pssci1;
	pssci1.stage = vk::ShaderStageFlagBits::eVertex;
	pssci1.module = vertModule;
	pssci1.pName = "main";

	vk::PipelineShaderStageCreateInfo pssci2;
	pssci2.stage = vk::ShaderStageFlagBits::eFragment;
	pssci2.module = fragModule;
	pssci2.pName = "main";

	std::vector<vk::PipelineShaderStageCreateInfo> stages{ pssci1, pssci2 };
	pipeline.pipeinfo.stageCount = static_cast<uint32_t>(stages.size());
	pipeline.pipeinfo.pStages = stages.data();

	// Vertex Input state
	auto vibd = Vertex::getBindingDescriptionSkyBox();
	auto viad = Vertex::getAttributeDescriptionSkyBox();
	vk::PipelineVertexInputStateCreateInfo pvisci;
	pvisci.vertexBindingDescriptionCount = static_cast<uint32_t>(vibd.size());
	pvisci.pVertexBindingDescriptions = vibd.data();
	pvisci.vertexAttributeDescriptionCount = static_cast<uint32_t>(viad.size());
	pvisci.pVertexAttributeDescriptions = viad.data();
	pipeline.pipeinfo.pVertexInputState = &pvisci;

	// Input Assembly stage
	vk::PipelineInputAssemblyStateCreateInfo piasci;
	piasci.topology = vk::PrimitiveTopology::eTriangleList;
	piasci.primitiveRestartEnable = VK_FALSE;
	pipeline.pipeinfo.pInputAssemblyState = &piasci;

	// Viewports and Scissors
	vk::Viewport vp;
	vp.x = 0.0f;
	vp.y = 0.0f;
	vp.width = WIDTH_f;
	vp.height = HEIGHT_f;
	vp.minDepth = 0.f;
	vp.maxDepth = 1.f;

	vk::Rect2D r2d;
	r2d.extent = { WIDTH, HEIGHT };

	vk::PipelineViewportStateCreateInfo pvsci;
	pvsci.viewportCount = 1;
	pvsci.pViewports = &vp;
	pvsci.scissorCount = 1;
	pvsci.pScissors = &r2d;
	pipeline.pipeinfo.pViewportState = &pvsci;

	// Rasterization state
	vk::PipelineRasterizationStateCreateInfo prsci;
	prsci.depthClampEnable = VK_FALSE;
	prsci.rasterizerDiscardEnable = VK_FALSE;
	prsci.polygonMode = vk::PolygonMode::eFill;
	prsci.cullMode = vk::CullModeFlagBits::eFront;
	prsci.frontFace = vk::FrontFace::eClockwise;
	prsci.depthBiasEnable = VK_FALSE;
	prsci.depthBiasConstantFactor = 0.0f;
	prsci.depthBiasClamp = 0.0f;
	prsci.depthBiasSlopeFactor = 0.0f;
	prsci.lineWidth = 1.0f;
	pipeline.pipeinfo.pRasterizationState = &prsci;

	// Multisample state
	vk::PipelineMultisampleStateCreateInfo pmsci;
	pmsci.rasterizationSamples = vk::SampleCountFlagBits::e1;
	pmsci.sampleShadingEnable = VK_FALSE;
	pmsci.minSampleShading = 1.0f;
	pmsci.pSampleMask = nullptr;
	pmsci.alphaToCoverageEnable = VK_FALSE;
	pmsci.alphaToOneEnable = VK_FALSE;
	pipeline.pipeinfo.pMultisampleState = &pmsci;

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
	pipeline.pipeinfo.pDepthStencilState = &pdssci;

	// Color Blending state
	vulkan->swapchain->images[0].blentAttachment.blendEnable = VK_FALSE;
	std::vector<vk::PipelineColorBlendAttachmentState> colorBlendAttachments = {
		vulkan->swapchain->images[0].blentAttachment
	};
	vk::PipelineColorBlendStateCreateInfo pcbsci;
	pcbsci.logicOpEnable = VK_FALSE;
	pcbsci.logicOp = vk::LogicOp::eCopy;
	pcbsci.attachmentCount = static_cast<uint32_t>(colorBlendAttachments.size());
	pcbsci.pAttachments = colorBlendAttachments.data();
	float blendConstants[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	memcpy(pcbsci.blendConstants, blendConstants, 4 * sizeof(float));
	pipeline.pipeinfo.pColorBlendState = &pcbsci;

	// Dynamic state
	std::vector<vk::DynamicState> dynamicStates{ vk::DynamicState::eViewport, vk::DynamicState::eScissor };
	vk::PipelineDynamicStateCreateInfo dsi;
	dsi.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
	dsi.pDynamicStates = dynamicStates.data();
	pipeline.pipeinfo.pDynamicState = &dsi;

	// Pipeline Layout
	std::vector<vk::DescriptorSetLayout> descriptorSetLayouts{ SkyBox::getDescriptorSetLayout() };
	vk::PipelineLayoutCreateInfo plci;
	plci.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
	plci.pSetLayouts = descriptorSetLayouts.data();
	pipeline.pipeinfo.layout = vulkan->device.createPipelineLayout(plci);

	// Render Pass
	pipeline.pipeinfo.renderPass = renderPass;

	// Subpass
	pipeline.pipeinfo.subpass = 0;

	// Base Pipeline Handle
	pipeline.pipeinfo.basePipelineHandle = nullptr;

	// Base Pipeline Index
	pipeline.pipeinfo.basePipelineIndex = -1;

	pipeline.pipeline = vulkan->device.createGraphicsPipelines(nullptr, pipeline.pipeinfo).at(0);

	// destroy Shader Modules
	vulkan->device.destroyShaderModule(vertModule);
	vulkan->device.destroyShaderModule(fragModule);
}
#endif // RENDER_SKYBOX