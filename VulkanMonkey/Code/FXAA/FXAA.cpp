#include "FXAA.h"

using namespace vm;

void FXAA::createUniforms(std::map<std::string, Image>& renderTargets)
{
	vk::DescriptorSetAllocateInfo allocateInfo2;
	allocateInfo2.descriptorPool = vulkan->descriptorPool;
	allocateInfo2.descriptorSetCount = 1;
	allocateInfo2.pSetLayouts = &DSLayout;
	DSet = vulkan->device.allocateDescriptorSets(allocateInfo2).at(0);

	updateDescriptorSets(renderTargets);
}

void FXAA::updateDescriptorSets(std::map<std::string, Image>& renderTargets)
{
	// Composition sampler
	vk::DescriptorImageInfo dii;
	dii.sampler = renderTargets["composition"].sampler;
	dii.imageView = renderTargets["composition"].view;
	dii.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

	vk::WriteDescriptorSet textureWriteSet;
	textureWriteSet.dstSet = DSet;
	textureWriteSet.dstBinding = 0;
	textureWriteSet.dstArrayElement = 0;
	textureWriteSet.descriptorCount = 1;
	textureWriteSet.descriptorType = vk::DescriptorType::eCombinedImageSampler;
	textureWriteSet.pImageInfo = &dii;

	vulkan->device.updateDescriptorSets(textureWriteSet, nullptr);
}

void FXAA::draw(uint32_t imageIndex)
{
	vk::ClearValue clearColor;
	memcpy(clearColor.color.float32, GUI::clearColor.data(), 4 * sizeof(float));

	std::vector<vk::ClearValue> clearValues = { clearColor, clearColor };

	vk::RenderPassBeginInfo rpi;
	rpi.renderPass = renderPass;
	rpi.framebuffer = frameBuffers[imageIndex];
	rpi.renderArea = { { 0, 0 }, vulkan->surface->actualExtent };
	rpi.clearValueCount = 2;
	rpi.pClearValues = clearValues.data();

	vulkan->dynamicCmdBuffer.beginRenderPass(rpi, vk::SubpassContents::eInline);
	vulkan->dynamicCmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline.pipeline);
	vulkan->dynamicCmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline.pipeinfo.layout, 0, DSet, nullptr);
	vulkan->dynamicCmdBuffer.draw(3, 1, 0, 0);
	vulkan->dynamicCmdBuffer.endRenderPass();
}

void vm::FXAA::createRenderPass(std::map<std::string, Image>& renderTargets)
{
	std::array<vk::AttachmentDescription, 2> attachments{};
	// Swapchain attachment
	attachments[0].format = vulkan->surface->formatKHR.format;
	attachments[0].samples = vk::SampleCountFlagBits::e1;
	attachments[0].loadOp = vk::AttachmentLoadOp::eDontCare;
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
	//renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
	//renderPassInfo.pDependencies = dependencies.data();

	renderPass = vulkan->device.createRenderPass(renderPassInfo);
}

void vm::FXAA::createFrameBuffers(std::map<std::string, Image>& renderTargets)
{
	frameBuffers.resize(vulkan->swapchain->images.size());

	for (size_t i = 0; i < frameBuffers.size(); ++i) {
		std::vector<vk::ImageView> attachments = {
			vulkan->swapchain->images[i].view,
			renderTargets["composition2"].view
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

void FXAA::createPipeline(std::map<std::string, Image>& renderTargets)
{
	// Shader stages
	std::vector<char> vertCode = readFile("shaders/Common/vert.spv");
	vk::ShaderModuleCreateInfo vsmci;
	vsmci.codeSize = vertCode.size();
	vsmci.pCode = reinterpret_cast<const uint32_t*>(vertCode.data());
	vk::ShaderModule vertModule = vulkan->device.createShaderModule(vsmci);

	std::vector<char> fragCode = readFile("shaders/FXAA/frag.spv");
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
	vk::PipelineVertexInputStateCreateInfo pvisci;
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
	vp.minDepth = 0.0f;
	vp.maxDepth = 1.0f;

	vk::Rect2D r2d;
	r2d.extent = vulkan->surface->actualExtent;

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
	prsci.cullMode = vk::CullModeFlagBits::eBack;
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
		vulkan->swapchain->images[0].blentAttachment,
		renderTargets["composition2"].blentAttachment
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
	pipeline.pipeinfo.pDynamicState = nullptr;

	// Pipeline Layout
	if (!DSLayout)
	{
		auto layoutBinding = [](uint32_t binding, vk::DescriptorType descriptorType) {
			return vk::DescriptorSetLayoutBinding{ binding, descriptorType, 1, vk::ShaderStageFlagBits::eFragment, nullptr };
		};
		std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings{
			layoutBinding(0, vk::DescriptorType::eCombinedImageSampler),
		};
		vk::DescriptorSetLayoutCreateInfo descriptorLayout;
		descriptorLayout.bindingCount = static_cast<uint32_t>(setLayoutBindings.size());
		descriptorLayout.pBindings = setLayoutBindings.data();
		DSLayout = vulkan->device.createDescriptorSetLayout(descriptorLayout);
	}

	std::vector<vk::DescriptorSetLayout> descriptorSetLayouts = { DSLayout };

	vk::PipelineLayoutCreateInfo plci;
	plci.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
	plci.pSetLayouts = descriptorSetLayouts.data();
	pipeline.pipeinfo.layout = vulkan->device.createPipelineLayout(plci);

	// Render Pass
	pipeline.pipeinfo.renderPass = renderPass;

	// Subpass (Index of subpass this pipeline will be used in)
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

void FXAA::destroy()
{
	for (auto &frameBuffer : frameBuffers) {
		if (frameBuffer) {
			vulkan->device.destroyFramebuffer(frameBuffer);
		}
	}
	if (renderPass) {
		vulkan->device.destroyRenderPass(renderPass);
		renderPass = nullptr;
	}
	if (DSLayout) {
		vulkan->device.destroyDescriptorSetLayout(DSLayout);
		DSLayout = nullptr;
	}
	pipeline.destroy();
}
