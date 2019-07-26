#include "TAA.h"
#include "../Timer/Timer.h"
#include <deque>

using namespace vm;

void TAA::Init()
{
	previous.format = vulkan->surface->formatKHR.format;
	previous.initialLayout = vk::ImageLayout::eUndefined;
	previous.createImage(
		static_cast<uint32_t>(WIDTH_f * GUI::renderTargetsScale),
		static_cast<uint32_t>(HEIGHT_f * GUI::renderTargetsScale),
		vk::ImageTiling::eOptimal,
		vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled, vk::MemoryPropertyFlagBits::eDeviceLocal);
	previous.transitionImageLayout(vk::ImageLayout::eUndefined, vk::ImageLayout::eShaderReadOnlyOptimal);
	previous.createImageView(vk::ImageAspectFlagBits::eColor);
	previous.createSampler();

	frameImage.format = vulkan->surface->formatKHR.format;
	frameImage.initialLayout = vk::ImageLayout::eUndefined;
	frameImage.createImage(
		static_cast<uint32_t>(WIDTH_f * GUI::renderTargetsScale),
		static_cast<uint32_t>(HEIGHT_f * GUI::renderTargetsScale),
		vk::ImageTiling::eOptimal,
		vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
		vk::MemoryPropertyFlagBits::eDeviceLocal);
	frameImage.transitionImageLayout(vk::ImageLayout::eUndefined, vk::ImageLayout::eShaderReadOnlyOptimal);
	frameImage.createImageView(vk::ImageAspectFlagBits::eColor);
	frameImage.createSampler();
}

void TAA::update(const Camera& camera)
{
	if (GUI::use_TAA) {
		ubo.values = { camera.projOffset.x, camera.projOffset.y, GUI::TAA_feedback_min, GUI::TAA_feedback_max };
		ubo.sharpenValues = { GUI::TAA_sharp_strength, GUI::TAA_sharp_clamp, GUI::TAA_sharp_offset_bias , sin(Timer::getTotalTime() * 0.125f) };
		ubo.invProj = camera.invProjection;
		memcpy(uniform.data, &ubo, sizeof(ubo));
	}
}

void TAA::createUniforms(std::map<std::string, Image>& renderTargets)
{
	uniform.createBuffer(sizeof(UBO), vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostCoherent);
	uniform.data = vulkan->device.mapMemory(uniform.memory, 0, uniform.size);
	memset(uniform.data, 0, uniform.size);

	vk::DescriptorSetAllocateInfo allocateInfo2;
	allocateInfo2.descriptorPool = vulkan->descriptorPool;
	allocateInfo2.descriptorSetCount = 1;
	allocateInfo2.pSetLayouts = &DSLayout;
	DSet = vulkan->device.allocateDescriptorSets(allocateInfo2).at(0);
	
	allocateInfo2.pSetLayouts = &DSLayoutSharpen;
	DSetSharpen = vulkan->device.allocateDescriptorSets(allocateInfo2).at(0);

	updateDescriptorSets(renderTargets);
}

void TAA::updateDescriptorSets(std::map<std::string, Image>& renderTargets)
{
	std::deque<vk::DescriptorImageInfo> dsii{};
	const auto wSetImage = [&dsii](vk::DescriptorSet & dstSet, uint32_t dstBinding, Image & image) {
		dsii.emplace_back(image.sampler, image.view, vk::ImageLayout::eShaderReadOnlyOptimal);
		return vk::WriteDescriptorSet{ dstSet, dstBinding, 0, 1, vk::DescriptorType::eCombinedImageSampler, &dsii.back(), nullptr, nullptr };
	};
	std::deque<vk::DescriptorBufferInfo> dsbi{};
	const auto wSetBuffer = [&dsbi](vk::DescriptorSet & dstSet, uint32_t dstBinding, Buffer & buffer) {
		dsbi.emplace_back(buffer.buffer, 0, buffer.size);
		return vk::WriteDescriptorSet{ dstSet, dstBinding, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &dsbi.back(), nullptr };
	};

	std::vector<vk::WriteDescriptorSet> writeDescriptorSets = {
		wSetImage(DSet, 0, previous),
		wSetImage(DSet, 1, frameImage),
		wSetImage(DSet, 2, renderTargets["depth"]),
		wSetImage(DSet, 3, renderTargets["velocity"]),
		wSetBuffer(DSet, 4, uniform),
		wSetImage(DSetSharpen, 0, renderTargets["taa"]),
		wSetBuffer(DSetSharpen, 1, uniform)
	};

	vulkan->device.updateDescriptorSets(writeDescriptorSets, nullptr);
}

void TAA::draw(vk::CommandBuffer cmd, uint32_t imageIndex, std::function<void(vk::CommandBuffer, Image&, LayoutState)>&& changeLayout, std::map<std::string, Image>& renderTargets)
{
	vk::ClearValue clearColor;
	memcpy(clearColor.color.float32, GUI::clearColor.data(), 4 * sizeof(float));

	std::vector<vk::ClearValue> clearValues = { clearColor };

	// Main TAA pass
	vk::RenderPassBeginInfo rpi;
	rpi.renderPass = renderPass;
	rpi.framebuffer = frameBuffers[imageIndex];
	rpi.renderArea.offset = vk::Offset2D{ 0, 0 };
	rpi.renderArea.extent = renderTargets["taa"].extent;
	rpi.clearValueCount = 1;
	rpi.pClearValues = clearValues.data();

	changeLayout(cmd, renderTargets["taa"], LayoutState::ColorWrite);
	cmd.beginRenderPass(rpi, vk::SubpassContents::eInline);
	cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline.pipeline);
	cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline.pipeinfo.layout, 0, DSet, nullptr);
	cmd.draw(3, 1, 0, 0);
	cmd.endRenderPass();
	changeLayout(cmd, renderTargets["taa"], LayoutState::ColorRead);

	saveImage(cmd, renderTargets["taa"]);

	// TAA Sharpen pass
	vk::RenderPassBeginInfo rpi2;
	rpi2.renderPass = renderPassSharpen;
	rpi2.framebuffer = frameBuffersSharpen[imageIndex];
	rpi2.renderArea.offset = vk::Offset2D{ 0, 0 };
	rpi2.renderArea.extent = renderTargets["viewport"].extent;
	rpi2.clearValueCount = 1;
	rpi2.pClearValues = clearValues.data();

	cmd.beginRenderPass(rpi2, vk::SubpassContents::eInline);
	cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelineSharpen.pipeline);
	cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineSharpen.pipeinfo.layout, 0, DSetSharpen, nullptr);
	cmd.draw(3, 1, 0, 0);
	cmd.endRenderPass();
}

void TAA::createRenderPasses(std::map<std::string, Image>& renderTargets)
{
	createRenderPass(renderTargets);
	createRenderPassSharpen(renderTargets);
}

void TAA::createRenderPass(std::map<std::string, Image>& renderTargets)
{
	std::array<vk::AttachmentDescription, 1> attachments{};
	// taa render target
	attachments[0].format = renderTargets["taa"].format;
	attachments[0].samples = vk::SampleCountFlagBits::e1;
	attachments[0].loadOp = vk::AttachmentLoadOp::eClear;
	attachments[0].storeOp = vk::AttachmentStoreOp::eStore;
	attachments[0].stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
	attachments[0].stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
	attachments[0].initialLayout = vk::ImageLayout::eUndefined;
	attachments[0].finalLayout = vk::ImageLayout::eColorAttachmentOptimal;

	std::vector<vk::AttachmentReference> colorReferences{
		{ 0, vk::ImageLayout::eColorAttachmentOptimal }
	};

	vk::SubpassDescription subpassDescription;
	subpassDescription.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
	subpassDescription.colorAttachmentCount = static_cast<uint32_t>(colorReferences.size());
	subpassDescription.pColorAttachments = colorReferences.data();
	subpassDescription.pDepthStencilAttachment = nullptr;

	vk::RenderPassCreateInfo renderPassInfo = {};
	renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
	renderPassInfo.pAttachments = attachments.data();
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpassDescription;

	renderPass = vulkan->device.createRenderPass(renderPassInfo);
}

void vm::TAA::createRenderPassSharpen(std::map<std::string, Image>& renderTargets)
{
	std::array<vk::AttachmentDescription, 1> attachments{};
	// swapchain image
	attachments[0].format = renderTargets["viewport"].format;
	attachments[0].samples = vk::SampleCountFlagBits::e1;
	attachments[0].loadOp = vk::AttachmentLoadOp::eClear;
	attachments[0].storeOp = vk::AttachmentStoreOp::eStore;
	attachments[0].stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
	attachments[0].stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
	attachments[0].initialLayout = vk::ImageLayout::eUndefined;
	attachments[0].finalLayout = vk::ImageLayout::eColorAttachmentOptimal;

	std::vector<vk::AttachmentReference> colorReferences{
		{ 0, vk::ImageLayout::eColorAttachmentOptimal }
	};

	vk::SubpassDescription subpassDescription;
	subpassDescription.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
	subpassDescription.colorAttachmentCount = static_cast<uint32_t>(colorReferences.size());
	subpassDescription.pColorAttachments = colorReferences.data();
	subpassDescription.pDepthStencilAttachment = nullptr;

	vk::RenderPassCreateInfo renderPassInfo;
	renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
	renderPassInfo.pAttachments = attachments.data();
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpassDescription;

	renderPassSharpen = vulkan->device.createRenderPass(renderPassInfo);
}

void TAA::createFrameBuffers(std::map<std::string, Image>& renderTargets)
{
	frameBuffers.resize(vulkan->swapchain->images.size());

	for (size_t i = 0; i < frameBuffers.size(); ++i) {
		std::vector<vk::ImageView> attachments = {
			renderTargets["taa"].view
		};
		vk::FramebufferCreateInfo fbci;
		fbci.renderPass = renderPass;
		fbci.attachmentCount = static_cast<uint32_t>(attachments.size());
		fbci.pAttachments = attachments.data();
		fbci.width = renderTargets["taa"].width;
		fbci.height = renderTargets["taa"].height;
		fbci.layers = 1;
		frameBuffers[i] = vulkan->device.createFramebuffer(fbci);
	}

	frameBuffersSharpen.resize(vulkan->swapchain->images.size());

	for (size_t i = 0; i < frameBuffersSharpen.size(); ++i) {
		std::vector<vk::ImageView> attachments = {
			renderTargets["viewport"].view
		};
		vk::FramebufferCreateInfo fbci;
		fbci.renderPass = renderPassSharpen;
		fbci.attachmentCount = static_cast<uint32_t>(attachments.size());
		fbci.pAttachments = attachments.data();
		fbci.width = renderTargets["viewport"].width;
		fbci.height = renderTargets["viewport"].height;
		fbci.layers = 1;
		frameBuffersSharpen[i] = vulkan->device.createFramebuffer(fbci);
	}
}

void TAA::createPipelines(std::map<std::string, Image>& renderTargets)
{
	createPipeline(renderTargets);
	createPipelineSharpen(renderTargets);
}

void TAA::createPipeline(std::map<std::string, Image>& renderTargets)
{
	// Shader stages
	std::vector<char> vertCode = readFile("shaders/Common/vert.spv");
	vk::ShaderModuleCreateInfo vsmci;
	vsmci.codeSize = vertCode.size();
	vsmci.pCode = reinterpret_cast<const uint32_t*>(vertCode.data());
	vk::ShaderModule vertModule = vulkan->device.createShaderModule(vsmci);

	std::vector<char> fragCode = readFile("shaders/TAA/frag.spv");
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
	vp.width = renderTargets["taa"].width_f;
	vp.height = renderTargets["taa"].height_f;
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
	std::vector<vk::PipelineColorBlendAttachmentState> colorBlendAttachments = {
		renderTargets["taa"].blentAttachment
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
			layoutBinding(1, vk::DescriptorType::eCombinedImageSampler),
			layoutBinding(2, vk::DescriptorType::eCombinedImageSampler),
			layoutBinding(3, vk::DescriptorType::eCombinedImageSampler),
			layoutBinding(4, vk::DescriptorType::eUniformBuffer)
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

void vm::TAA::createPipelineSharpen(std::map<std::string, Image>& renderTargets)
{
	// Shader stages
	std::vector<char> vertCode = readFile("shaders/Common/vert.spv");
	vk::ShaderModuleCreateInfo vsmci;
	vsmci.codeSize = vertCode.size();
	vsmci.pCode = reinterpret_cast<const uint32_t*>(vertCode.data());
	vk::ShaderModule vertModule = vulkan->device.createShaderModule(vsmci);

	std::vector<char> fragCode = readFile("shaders/TAA/fragSharpen.spv");
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
	pipelineSharpen.pipeinfo.stageCount = static_cast<uint32_t>(stages.size());
	pipelineSharpen.pipeinfo.pStages = stages.data();

	// Vertex Input state
	vk::PipelineVertexInputStateCreateInfo pvisci;
	pipelineSharpen.pipeinfo.pVertexInputState = &pvisci;

	// Input Assembly stage
	vk::PipelineInputAssemblyStateCreateInfo piasci;
	piasci.topology = vk::PrimitiveTopology::eTriangleList;
	piasci.primitiveRestartEnable = VK_FALSE;
	pipelineSharpen.pipeinfo.pInputAssemblyState = &piasci;

	// Viewports and Scissors
	vk::Viewport vp;
	vp.x = 0.0f;
	vp.y = 0.0f;
	vp.width = renderTargets["viewport"].width_f;
	vp.height = renderTargets["viewport"].height_f;
	vp.minDepth = 0.0f;
	vp.maxDepth = 1.0f;

	vk::Rect2D r2d;
	r2d.extent = vulkan->surface->actualExtent;

	vk::PipelineViewportStateCreateInfo pvsci;
	pvsci.viewportCount = 1;
	pvsci.pViewports = &vp;
	pvsci.scissorCount = 1;
	pvsci.pScissors = &r2d;
	pipelineSharpen.pipeinfo.pViewportState = &pvsci;

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
	pipelineSharpen.pipeinfo.pRasterizationState = &prsci;

	// Multisample state
	vk::PipelineMultisampleStateCreateInfo pmsci;
	pmsci.rasterizationSamples = vk::SampleCountFlagBits::e1;
	pmsci.sampleShadingEnable = VK_FALSE;
	pmsci.minSampleShading = 1.0f;
	pmsci.pSampleMask = nullptr;
	pmsci.alphaToCoverageEnable = VK_FALSE;
	pmsci.alphaToOneEnable = VK_FALSE;
	pipelineSharpen.pipeinfo.pMultisampleState = &pmsci;

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
	pipelineSharpen.pipeinfo.pDepthStencilState = &pdssci;

	// Color Blending state
	std::vector<vk::PipelineColorBlendAttachmentState> colorBlendAttachments = {
		renderTargets["viewport"].blentAttachment
	};
	vk::PipelineColorBlendStateCreateInfo pcbsci;
	pcbsci.logicOpEnable = VK_FALSE;
	pcbsci.logicOp = vk::LogicOp::eCopy;
	pcbsci.attachmentCount = static_cast<uint32_t>(colorBlendAttachments.size());
	pcbsci.pAttachments = colorBlendAttachments.data();
	float blendConstants[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	memcpy(pcbsci.blendConstants, blendConstants, 4 * sizeof(float));
	pipelineSharpen.pipeinfo.pColorBlendState = &pcbsci;

	// Dynamic state
	pipelineSharpen.pipeinfo.pDynamicState = nullptr;

	// Pipeline Layout
	if (!DSLayoutSharpen)
	{
		auto layoutBinding = [](uint32_t binding, vk::DescriptorType descriptorType) {
			return vk::DescriptorSetLayoutBinding{ binding, descriptorType, 1, vk::ShaderStageFlagBits::eFragment, nullptr };
		};
		std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings{
			layoutBinding(0, vk::DescriptorType::eCombinedImageSampler),
			layoutBinding(1, vk::DescriptorType::eUniformBuffer)
		};
		vk::DescriptorSetLayoutCreateInfo descriptorLayout;
		descriptorLayout.bindingCount = static_cast<uint32_t>(setLayoutBindings.size());
		descriptorLayout.pBindings = setLayoutBindings.data();
		DSLayoutSharpen = vulkan->device.createDescriptorSetLayout(descriptorLayout);
	}

	std::vector<vk::DescriptorSetLayout> descriptorSetLayouts = { DSLayoutSharpen };

	vk::PipelineLayoutCreateInfo plci;
	plci.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
	plci.pSetLayouts = descriptorSetLayouts.data();
	pipelineSharpen.pipeinfo.layout = vulkan->device.createPipelineLayout(plci);

	// Render Pass
	pipelineSharpen.pipeinfo.renderPass = renderPassSharpen;

	// Subpass (Index of subpass this pipeline will be used in)
	pipelineSharpen.pipeinfo.subpass = 0;

	// Base Pipeline Handle
	pipelineSharpen.pipeinfo.basePipelineHandle = nullptr;

	// Base Pipeline Index
	pipelineSharpen.pipeinfo.basePipelineIndex = -1;

	pipelineSharpen.pipeline = vulkan->device.createGraphicsPipelines(nullptr, pipelineSharpen.pipeinfo).at(0);

	// destroy Shader Modules
	vulkan->device.destroyShaderModule(vertModule);
	vulkan->device.destroyShaderModule(fragModule);
}

void TAA::copyFrameImage(const vk::CommandBuffer& cmd, Image& renderedImage)
{
	frameImage.transitionImageLayout(
		cmd,
		vk::ImageLayout::eShaderReadOnlyOptimal,
		vk::ImageLayout::eTransferDstOptimal,
		vk::PipelineStageFlagBits::eFragmentShader,
		vk::PipelineStageFlagBits::eTransfer,
		vk::AccessFlagBits::eShaderRead,
		vk::AccessFlagBits::eTransferWrite,
		vk::ImageAspectFlagBits::eColor);
	renderedImage.transitionImageLayout(
		cmd,
		vk::ImageLayout::eColorAttachmentOptimal,
		vk::ImageLayout::eTransferSrcOptimal,
		vk::PipelineStageFlagBits::eColorAttachmentOutput,
		vk::PipelineStageFlagBits::eTransfer,
		vk::AccessFlagBits::eColorAttachmentWrite,
		vk::AccessFlagBits::eTransferRead,
		vk::ImageAspectFlagBits::eColor);

	// copy the image
	vk::ImageCopy region;
	region.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
	region.srcSubresource.layerCount = 1;
	region.dstSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
	region.dstSubresource.layerCount = 1;
	region.extent.width = renderedImage.width;
	region.extent.height = renderedImage.height;
	region.extent.depth = 1;

	cmd.copyImage(
		renderedImage.image,
		vk::ImageLayout::eTransferSrcOptimal,
		frameImage.image,
		vk::ImageLayout::eTransferDstOptimal,
		region);

	frameImage.transitionImageLayout(
		cmd,
		vk::ImageLayout::eTransferDstOptimal,
		vk::ImageLayout::eShaderReadOnlyOptimal,
		vk::PipelineStageFlagBits::eTransfer,
		vk::PipelineStageFlagBits::eFragmentShader,
		vk::AccessFlagBits::eTransferWrite,
		vk::AccessFlagBits::eShaderRead,
		vk::ImageAspectFlagBits::eColor);
	renderedImage.transitionImageLayout(
		cmd,
		vk::ImageLayout::eTransferSrcOptimal,
		vk::ImageLayout::eColorAttachmentOptimal,
		vk::PipelineStageFlagBits::eTransfer,
		vk::PipelineStageFlagBits::eColorAttachmentOutput,
		vk::AccessFlagBits::eTransferRead,
		vk::AccessFlagBits::eColorAttachmentWrite,
		vk::ImageAspectFlagBits::eColor);
}

void TAA::saveImage(const vk::CommandBuffer& cmd, Image& source)
{
	previous.transitionImageLayout(
		cmd,
		vk::ImageLayout::eShaderReadOnlyOptimal,
		vk::ImageLayout::eTransferDstOptimal,
		vk::PipelineStageFlagBits::eFragmentShader,
		vk::PipelineStageFlagBits::eTransfer,
		vk::AccessFlagBits::eShaderRead,
		vk::AccessFlagBits::eTransferWrite,
		vk::ImageAspectFlagBits::eColor);
	source.transitionImageLayout(
		cmd,
		source.layoutState == LayoutState::ColorRead ? vk::ImageLayout::eShaderReadOnlyOptimal : vk::ImageLayout::eColorAttachmentOptimal,
		vk::ImageLayout::eTransferSrcOptimal,
		source.layoutState == LayoutState::ColorRead ? vk::PipelineStageFlagBits::eFragmentShader : vk::PipelineStageFlagBits::eColorAttachmentOutput,
		vk::PipelineStageFlagBits::eTransfer,
		source.layoutState == LayoutState::ColorRead ? vk::AccessFlagBits::eShaderRead : vk::AccessFlagBits::eColorAttachmentWrite,
		vk::AccessFlagBits::eTransferRead,
		vk::ImageAspectFlagBits::eColor);

	// copy the image
	vk::ImageCopy region;
	region.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
	region.srcSubresource.layerCount = 1;
	region.dstSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
	region.dstSubresource.layerCount = 1;
	region.extent.width = source.width;
	region.extent.height = source.height;
	region.extent.depth = 1;

	cmd.copyImage(
		source.image,
		vk::ImageLayout::eTransferSrcOptimal,
		previous.image,
		vk::ImageLayout::eTransferDstOptimal,
		region
	);

	previous.transitionImageLayout(
		cmd,
		vk::ImageLayout::eTransferDstOptimal,
		vk::ImageLayout::eShaderReadOnlyOptimal,
		vk::PipelineStageFlagBits::eTransfer,
		vk::PipelineStageFlagBits::eFragmentShader,
		vk::AccessFlagBits::eTransferWrite,
		vk::AccessFlagBits::eShaderRead,
		vk::ImageAspectFlagBits::eColor);
	source.transitionImageLayout(
		cmd,
		vk::ImageLayout::eTransferSrcOptimal,
		vk::ImageLayout::eShaderReadOnlyOptimal,
		vk::PipelineStageFlagBits::eTransfer,
		vk::PipelineStageFlagBits::eFragmentShader,
		vk::AccessFlagBits::eTransferRead,
		vk::AccessFlagBits::eShaderRead,
		vk::ImageAspectFlagBits::eColor);
	source.layoutState = LayoutState::ColorRead;
}

void TAA::destroy()
{
	uniform.destroy();
	previous.destroy();
	frameImage.destroy();
	for (auto& frameBuffer : frameBuffers) {
		if (frameBuffer) {
			vulkan->device.destroyFramebuffer(frameBuffer);
		}
	}
	for (auto& frameBuffer : frameBuffersSharpen) {
		if (frameBuffer) {
			vulkan->device.destroyFramebuffer(frameBuffer);
		}
	}
	if (renderPass) {
		vulkan->device.destroyRenderPass(renderPass);
		renderPass = nullptr;
	}
	if (renderPassSharpen) {
		vulkan->device.destroyRenderPass(renderPassSharpen);
		renderPassSharpen = nullptr;
	}
	if (DSLayout) {
		vulkan->device.destroyDescriptorSetLayout(DSLayout);
		DSLayout = nullptr;
	}
	if (DSLayoutSharpen) {
		vulkan->device.destroyDescriptorSetLayout(DSLayoutSharpen);
		DSLayoutSharpen = nullptr;
	}
	pipeline.destroy();
	pipelineSharpen.destroy();
}
