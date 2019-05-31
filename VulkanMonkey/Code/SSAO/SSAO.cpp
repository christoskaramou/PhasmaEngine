#include "SSAO.h"
#include <deque>

using namespace vm;

void SSAO::createUniforms(std::map<std::string, Image>& renderTargets)
{
	// kernel buffer
	std::vector<vec4> kernel{};
	for (unsigned i = 0; i < 32; i++) {
		vec3 sample(rand(-1.f, 1.f), rand(-1.f, 1.f), rand(0.f, 1.f));
		sample = normalize(sample);
		sample *= rand(0.f, 1.f);
		float scale = float(i) / 32.f;
		scale = lerp(.1f, 1.f, scale * scale);
		kernel.push_back(vec4(sample * scale, 0.f));
	}
	UB_Kernel.createBuffer(sizeof(vec4) * 32, vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostCoherent);
	UB_Kernel.data = vulkan->device.mapMemory(UB_Kernel.memory, 0, UB_Kernel.size);
	memcpy(UB_Kernel.data, kernel.data(), UB_Kernel.size);
	// noise image
	std::vector<vec4> noise{};
	for (unsigned int i = 0; i < 16; i++)
		noise.push_back(vec4(rand(-1.f, 1.f), rand(-1.f, 1.f), 0.f, 1.f));

	Buffer staging;
	uint64_t bufSize = sizeof(vec4) * 16;
	staging.createBuffer(bufSize, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
	void* data = vulkan->device.mapMemory(staging.memory, vk::DeviceSize(), staging.size);
	memcpy(data, noise.data(), staging.size);
	vulkan->device.unmapMemory(staging.memory);

	noiseTex.filter = vk::Filter::eNearest;
	noiseTex.minLod = 0.0f;
	noiseTex.maxLod = 0.0f;
	noiseTex.maxAnisotropy = 1.0f;
	noiseTex.format = vk::Format::eR16G16B16A16Sfloat;
	noiseTex.createImage(4, 4, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled, vk::MemoryPropertyFlagBits::eDeviceLocal);
	noiseTex.transitionImageLayout(vk::ImageLayout::ePreinitialized, vk::ImageLayout::eTransferDstOptimal);
	noiseTex.copyBufferToImage(staging.buffer, 0, 0, 4, 4);
	noiseTex.transitionImageLayout(vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal);
	noiseTex.createImageView(vk::ImageAspectFlagBits::eColor);
	noiseTex.createSampler();
	staging.destroy();
	// pvm uniform
	UB_PVM.createBuffer(3 * sizeof(mat4), vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostCoherent);
	UB_PVM.data = vulkan->device.mapMemory(UB_PVM.memory, 0, UB_PVM.size);
	memset(UB_PVM.data, 0, UB_PVM.size);

	// DESCRIPTOR SET FOR SSAO
	vk::DescriptorSetAllocateInfo allocInfo = vk::DescriptorSetAllocateInfo{
		vulkan->descriptorPool,					//DescriptorPool descriptorPool;
		1,										//uint32_t descriptorSetCount;
		&DSLayout								//const DescriptorSetLayout* pSetLayouts;
	};
	DSet = vulkan->device.allocateDescriptorSets(allocInfo).at(0);

	// DESCRIPTOR SET FOR SSAO BLUR
	vk::DescriptorSetAllocateInfo allocInfoBlur = vk::DescriptorSetAllocateInfo{
		vulkan->descriptorPool,					//DescriptorPool descriptorPool;
		1,										//uint32_t descriptorSetCount;
		&DSLayoutBlur							//const DescriptorSetLayout* pSetLayouts;
	};
	DSBlur = vulkan->device.allocateDescriptorSets(allocInfoBlur).at(0);

	updateDescriptorSets(renderTargets);
}

void SSAO::updateDescriptorSets(std::map<std::string, Image>& renderTargets)
{
	std::deque<vk::DescriptorImageInfo> dsii{};
	auto wSetImage = [&dsii](vk::DescriptorSet& dstSet, uint32_t dstBinding, Image& image, vk::ImageLayout imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal) {
		dsii.push_back({ image.sampler, image.view, imageLayout });
		return vk::WriteDescriptorSet{ dstSet, dstBinding, 0, 1, vk::DescriptorType::eCombinedImageSampler, &dsii.back(), nullptr, nullptr };
	};
	std::deque<vk::DescriptorBufferInfo> dsbi{};
	auto wSetBuffer = [&dsbi](vk::DescriptorSet& dstSet, uint32_t dstBinding, Buffer& buffer) {
		dsbi.push_back({ buffer.buffer, 0, buffer.size });
		return vk::WriteDescriptorSet{ dstSet, dstBinding, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &dsbi.back(), nullptr };
	};

	std::vector<vk::WriteDescriptorSet> writeDescriptorSets{
		wSetImage(DSet, 0, renderTargets["depth"]),
		wSetImage(DSet, 1, renderTargets["normal"]),
		wSetImage(DSet, 2, noiseTex),
		wSetBuffer(DSet, 3, UB_Kernel),
		wSetBuffer(DSet, 4, UB_PVM),
		wSetImage(DSBlur, 0, renderTargets["ssao"])
	};
	vulkan->device.updateDescriptorSets(writeDescriptorSets, nullptr);
}

void SSAO::draw(vk::CommandBuffer cmd, uint32_t imageIndex, std::function<void(vk::CommandBuffer, Image&, LayoutState)>&& changeLayout, Image& image)
{
	// SSAO image
	vk::ClearValue clearColor;
	memcpy(clearColor.color.float32, GUI::clearColor.data(), 4 * sizeof(float));


	std::vector<vk::ClearValue> clearValues = { clearColor };

	vk::RenderPassBeginInfo rpi;
	rpi.renderPass = renderPass;
	rpi.framebuffer = frameBuffers[imageIndex];
	rpi.renderArea.offset = { 0, 0 };
	rpi.renderArea.extent = image.extent;
	rpi.clearValueCount = 1;
	rpi.pClearValues = clearValues.data();

	changeLayout(cmd, image, LayoutState::Write);
	cmd.beginRenderPass(rpi, vk::SubpassContents::eInline);
	cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline.pipeline);
	const vk::DescriptorSet descriptorSets = { DSet };
	cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline.pipeinfo.layout, 0, descriptorSets, nullptr);
	cmd.draw(3, 1, 0, 0);
	cmd.endRenderPass();
	changeLayout(cmd, image, LayoutState::Read);

	// new blurry SSAO image
	rpi.renderPass = blurRenderPass;
	rpi.framebuffer = blurFrameBuffers[imageIndex];

	cmd.beginRenderPass(rpi, vk::SubpassContents::eInline);
	cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelineBlur.pipeline);
	const vk::DescriptorSet descriptorSetsBlur = { DSBlur };
	cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineBlur.pipeinfo.layout, 0, descriptorSetsBlur, nullptr);
	cmd.draw(3, 1, 0, 0);
	cmd.endRenderPass();
}

void SSAO::destroy()
{
	UB_Kernel.destroy();
	UB_PVM.destroy();
	noiseTex.destroy();
	if (renderPass) {
		vulkan->device.destroyRenderPass(renderPass);
		renderPass = nullptr;
	}
	if (blurRenderPass) {
		vulkan->device.destroyRenderPass(blurRenderPass);
		blurRenderPass = nullptr;
	}
	for (auto &frameBuffer : frameBuffers) {
		if (frameBuffer) {
			vulkan->device.destroyFramebuffer(frameBuffer);
		}
	}
	for (auto &frameBuffer : blurFrameBuffers) {
		if (frameBuffer) {
			vulkan->device.destroyFramebuffer(frameBuffer);
		}
	}
	pipeline.destroy();
	pipelineBlur.destroy();
	if (DSLayout) {
		vulkan->device.destroyDescriptorSetLayout(DSLayout);
		DSLayout = nullptr;
	}
	if (DSLayoutBlur) {
		vulkan->device.destroyDescriptorSetLayout(DSLayoutBlur);
		DSLayoutBlur = nullptr;
	}
}

void SSAO::update(Camera& camera)
{
	if (GUI::show_ssao) {
		mat4 pvm[3]{ camera.projection, camera.view, camera.invProjection };
		memcpy(UB_PVM.data, pvm, sizeof(pvm));
	}
}

void vm::SSAO::createRenderPasses(std::map<std::string, Image>& renderTargets)
{
	createSSAORenderPass(renderTargets);
	createSSAOBlurRenderPass(renderTargets);
}

void vm::SSAO::createSSAORenderPass(std::map<std::string, Image>& renderTargets)
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

	vk::RenderPassCreateInfo renderPassInfo = {};
	renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
	renderPassInfo.pAttachments = attachments.data();
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpassDescription;

	renderPass = vulkan->device.createRenderPass(renderPassInfo);
}

void vm::SSAO::createSSAOBlurRenderPass(std::map<std::string, Image>& renderTargets)
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
	//renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
	//renderPassInfo.pDependencies = dependencies.data();

	blurRenderPass = vulkan->device.createRenderPass(renderPassInfo);
}

void vm::SSAO::createFrameBuffers(std::map<std::string, Image>& renderTargets)
{
	createSSAOFrameBuffers(renderTargets);
	createSSAOBlurFrameBuffers(renderTargets);
}

void vm::SSAO::createSSAOFrameBuffers(std::map<std::string, Image>& renderTargets)
{
	frameBuffers.resize(vulkan->swapchain->images.size());

	for (size_t i = 0; i < frameBuffers.size(); ++i) {
		std::vector<vk::ImageView> attachments = {
			renderTargets["ssao"].view
		};
		vk::FramebufferCreateInfo fbci;
		fbci.renderPass = renderPass;
		fbci.attachmentCount = static_cast<uint32_t>(attachments.size());
		fbci.pAttachments = attachments.data();
		fbci.width = renderTargets["ssao"].width;
		fbci.height = renderTargets["ssao"].height;
		fbci.layers = 1;
		frameBuffers[i] = vulkan->device.createFramebuffer(fbci);
	}
}

void vm::SSAO::createSSAOBlurFrameBuffers(std::map<std::string, Image>& renderTargets)
{
	blurFrameBuffers.resize(vulkan->swapchain->images.size());

	for (size_t i = 0; i < blurFrameBuffers.size(); ++i) {
		std::vector<vk::ImageView> attachments = {
			renderTargets["ssaoBlur"].view
		};
		vk::FramebufferCreateInfo fbci;
		fbci.renderPass = blurRenderPass;
		fbci.attachmentCount = static_cast<uint32_t>(attachments.size());
		fbci.pAttachments = attachments.data();
		fbci.width = renderTargets["ssaoBlur"].width;
		fbci.height = renderTargets["ssaoBlur"].height;
		fbci.layers = 1;
		blurFrameBuffers[i] = vulkan->device.createFramebuffer(fbci);
	}
}

void vm::SSAO::createPipelines(std::map<std::string, Image>& renderTargets)
{
	createPipeline(renderTargets);
	createBlurPipeline(renderTargets);
}

void SSAO::createPipeline(std::map<std::string, Image>& renderTargets)
{
	// Shader stages
	std::vector<char> vertCode = readFile("shaders/Common/vert.spv");
	vk::ShaderModuleCreateInfo vsmci;
	vsmci.codeSize = vertCode.size();
	vsmci.pCode = reinterpret_cast<const uint32_t*>(vertCode.data());
	vk::ShaderModule vertModule = vulkan->device.createShaderModule(vsmci);

	std::vector<char> fragCode = readFile("shaders/SSAO/frag.spv");
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
	vp.width = renderTargets["ssao"].width_f;
	vp.height = renderTargets["ssao"].height_f;
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
		renderTargets["ssao"].blentAttachment
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
			layoutBinding(3, vk::DescriptorType::eUniformBuffer),
			layoutBinding(4, vk::DescriptorType::eUniformBuffer),
		};
		vk::DescriptorSetLayoutCreateInfo descriptorLayout;
		descriptorLayout.bindingCount = static_cast<uint32_t>(setLayoutBindings.size());
		descriptorLayout.pBindings = setLayoutBindings.data();
		DSLayout = vulkan->device.createDescriptorSetLayout(descriptorLayout);
	}

	std::vector<vk::DescriptorSetLayout> descriptorSetLayouts = { DSLayout };

	//vk::PushConstantRange pConstants;
	//pConstants.stageFlags = vk::ShaderStageFlagBits::eFragment;
	//pConstants.offset = 0;
	//pConstants.size = 4 * sizeof(vec4);

	vk::PipelineLayoutCreateInfo plci;
	plci.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
	plci.pSetLayouts = descriptorSetLayouts.data();
	plci.pushConstantRangeCount = 0;
	plci.pPushConstantRanges = nullptr;
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

void SSAO::createBlurPipeline(std::map<std::string, Image>& renderTargets)
{
	// Shader stages
	std::vector<char> vertCode = readFile("shaders/Common/vert.spv");
	vk::ShaderModuleCreateInfo vsmci;
	vsmci.codeSize = vertCode.size();
	vsmci.pCode = reinterpret_cast<const uint32_t*>(vertCode.data());
	vk::ShaderModule vertModule = vulkan->device.createShaderModule(vsmci);

	std::vector<char> fragCode = readFile("shaders/SSAO/fragBlur.spv");
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
	pipelineBlur.pipeinfo.stageCount = static_cast<uint32_t>(stages.size());
	pipelineBlur.pipeinfo.pStages = stages.data();

	// Vertex Input state
	vk::PipelineVertexInputStateCreateInfo pvisci;
	pipelineBlur.pipeinfo.pVertexInputState = &pvisci;

	// Input Assembly stage
	vk::PipelineInputAssemblyStateCreateInfo piasci;
	piasci.topology = vk::PrimitiveTopology::eTriangleList;
	piasci.primitiveRestartEnable = VK_FALSE;
	pipelineBlur.pipeinfo.pInputAssemblyState = &piasci;

	// Viewports and Scissors
	vk::Viewport vp;
	vp.x = 0.0f;
	vp.y = 0.0f;
	vp.width = renderTargets["ssaoBlur"].width_f;
	vp.height = renderTargets["ssaoBlur"].height_f;
	vp.minDepth = 0.0f;
	vp.maxDepth = 1.0f;

	vk::Rect2D r2d;
	r2d.extent = vulkan->surface->actualExtent;

	vk::PipelineViewportStateCreateInfo pvsci;
	pvsci.viewportCount = 1;
	pvsci.pViewports = &vp;
	pvsci.scissorCount = 1;
	pvsci.pScissors = &r2d;
	pipelineBlur.pipeinfo.pViewportState = &pvsci;

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
	pipelineBlur.pipeinfo.pRasterizationState = &prsci;

	// Multisample state
	vk::PipelineMultisampleStateCreateInfo pmsci;
	pmsci.rasterizationSamples = vk::SampleCountFlagBits::e1;
	pmsci.sampleShadingEnable = VK_FALSE;
	pmsci.minSampleShading = 1.0f;
	pmsci.pSampleMask = nullptr;
	pmsci.alphaToCoverageEnable = VK_FALSE;
	pmsci.alphaToOneEnable = VK_FALSE;
	pipelineBlur.pipeinfo.pMultisampleState = &pmsci;

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
	pipelineBlur.pipeinfo.pDepthStencilState = &pdssci;

	// Color Blending state
	std::vector<vk::PipelineColorBlendAttachmentState> colorBlendAttachments = {
		renderTargets["ssaoBlur"].blentAttachment
	};
	vk::PipelineColorBlendStateCreateInfo pcbsci;
	pcbsci.logicOpEnable = VK_FALSE;
	pcbsci.logicOp = vk::LogicOp::eCopy;
	pcbsci.attachmentCount = static_cast<uint32_t>(colorBlendAttachments.size());
	pcbsci.pAttachments = colorBlendAttachments.data();
	float blendConstants[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	memcpy(pcbsci.blendConstants, blendConstants, 4 * sizeof(float));
	pipelineBlur.pipeinfo.pColorBlendState = &pcbsci;

	// Dynamic state
	pipelineBlur.pipeinfo.pDynamicState = nullptr;

	// Pipeline Layout
	if (!DSLayoutBlur)
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
		DSLayoutBlur = vulkan->device.createDescriptorSetLayout(descriptorLayout);
	}

	std::vector<vk::DescriptorSetLayout> descriptorSetLayouts = { DSLayoutBlur };

	vk::PipelineLayoutCreateInfo plci;
	plci.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
	plci.pSetLayouts = descriptorSetLayouts.data();

	pipelineBlur.pipeinfo.layout = vulkan->device.createPipelineLayout(plci);

	// Render Pass
	pipelineBlur.pipeinfo.renderPass = blurRenderPass;

	// Subpass (Index of subpass this pipeline will be used in)
	pipelineBlur.pipeinfo.subpass = 0;

	// Base Pipeline Handle
	pipelineBlur.pipeinfo.basePipelineHandle = nullptr;

	// Base Pipeline Index
	pipelineBlur.pipeinfo.basePipelineIndex = -1;

	pipelineBlur.pipeline = vulkan->device.createGraphicsPipelines(nullptr, pipelineBlur.pipeinfo).at(0);

	// destroy Shader Modules
	vulkan->device.destroyShaderModule(vertModule);
	vulkan->device.destroyShaderModule(fragModule);
}
