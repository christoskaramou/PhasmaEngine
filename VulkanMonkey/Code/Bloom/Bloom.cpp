#include "Bloom.h"
#include "../GUI/GUI.h"
#include <deque>

using namespace vm;

void Bloom::Init()
{
	frameImage.format = vulkan->surface->formatKHR.format;
	frameImage.initialLayout = vk::ImageLayout::eUndefined;
	frameImage.createImage(
		static_cast<uint32_t>(WIDTH_f * GUI::renderTargetsScale),
		static_cast<uint32_t>(HEIGHT_f * GUI::renderTargetsScale),
		vk::ImageTiling::eOptimal,
		vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled, vk::MemoryPropertyFlagBits::eDeviceLocal);
	frameImage.transitionImageLayout(vk::ImageLayout::eUndefined, vk::ImageLayout::eShaderReadOnlyOptimal);
	frameImage.createImageView(vk::ImageAspectFlagBits::eColor);
	frameImage.createSampler();
}

void Bloom::copyFrameImage(const vk::CommandBuffer& cmd, Image& renderedImage)
{
	frameImage.transitionImageLayout(
		cmd,
		vk::ImageLayout::eShaderReadOnlyOptimal,
		vk::ImageLayout::eTransferDstOptimal,
		vk::PipelineStageFlagBits::eFragmentShader,
		vk::PipelineStageFlagBits::eTransfer);
	renderedImage.transitionImageLayout(
		cmd,
		vk::ImageLayout::eColorAttachmentOptimal,
		vk::ImageLayout::eTransferSrcOptimal,
		vk::PipelineStageFlagBits::eColorAttachmentOutput,
		vk::PipelineStageFlagBits::eTransfer);

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
		vk::PipelineStageFlagBits::eFragmentShader);
	renderedImage.transitionImageLayout(
		cmd,
		vk::ImageLayout::eTransferSrcOptimal,
		vk::ImageLayout::eColorAttachmentOptimal,
		vk::PipelineStageFlagBits::eTransfer,
		vk::PipelineStageFlagBits::eColorAttachmentOutput);
}

void vm::Bloom::createRenderPasses(std::map<std::string, Image>& renderTargets)
{
	createBrightFilterRenderPass(renderTargets);
	createGaussianBlurRenderPass(renderTargets);
	createCombineRenderPass(renderTargets);
}

void vm::Bloom::createBrightFilterRenderPass(std::map<std::string, Image>& renderTargets)
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

	vk::RenderPassCreateInfo renderPassInfo = {};
	renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
	renderPassInfo.pAttachments = attachments.data();
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpassDescription;

	renderPassBrightFilter = vulkan->device.createRenderPass(renderPassInfo);
}

void vm::Bloom::createGaussianBlurRenderPass(std::map<std::string, Image>& renderTargets)
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

	vk::RenderPassCreateInfo renderPassInfo = {};
	renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
	renderPassInfo.pAttachments = attachments.data();
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpassDescription;

	renderPassGaussianBlur = vulkan->device.createRenderPass(renderPassInfo);
}

void vm::Bloom::createCombineRenderPass(std::map<std::string, Image>& renderTargets)
{
	std::array<vk::AttachmentDescription, 1> attachments{};
	// Swapchain attachment
	attachments[0].format = renderTargets["viewport"].format;
	attachments[0].samples = vk::SampleCountFlagBits::e1;
	attachments[0].loadOp = vk::AttachmentLoadOp::eDontCare;
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

	renderPassCombine = vulkan->device.createRenderPass(renderPassInfo);
}

void vm::Bloom::createFrameBuffers(std::map<std::string, Image>& renderTargets)
{
	frameBuffers.resize(vulkan->swapchain->images.size() * 4);
	for (size_t i = 0; i < vulkan->swapchain->images.size(); ++i) {
		std::vector<vk::ImageView> attachments = {
			renderTargets["brightFilter"].view
		};
		vk::FramebufferCreateInfo fbci;
		fbci.renderPass = renderPassBrightFilter;
		fbci.attachmentCount = static_cast<uint32_t>(attachments.size());
		fbci.pAttachments = attachments.data();
		fbci.width = renderTargets["brightFilter"].width;
		fbci.height = renderTargets["brightFilter"].height;
		fbci.layers = 1;
		frameBuffers[i] = vulkan->device.createFramebuffer(fbci);
	}

	for (size_t i = vulkan->swapchain->images.size(); i < vulkan->swapchain->images.size() * 2; ++i) {
		std::vector<vk::ImageView> attachments = {
			renderTargets["gaussianBlurHorizontal"].view
		};
		vk::FramebufferCreateInfo fbci;
		fbci.renderPass = renderPassGaussianBlur;
		fbci.attachmentCount = static_cast<uint32_t>(attachments.size());
		fbci.pAttachments = attachments.data();
		fbci.width = renderTargets["gaussianBlurHorizontal"].width;
		fbci.height = renderTargets["gaussianBlurHorizontal"].height;
		fbci.layers = 1;
		frameBuffers[i] = vulkan->device.createFramebuffer(fbci);
	}

	for (size_t i = vulkan->swapchain->images.size() * 2; i < vulkan->swapchain->images.size() * 3; ++i) {
		std::vector<vk::ImageView> attachments = {
			renderTargets["gaussianBlurVertical"].view
		};
		vk::FramebufferCreateInfo fbci;
		fbci.renderPass = renderPassGaussianBlur;
		fbci.attachmentCount = static_cast<uint32_t>(attachments.size());
		fbci.pAttachments = attachments.data();
		fbci.width = renderTargets["gaussianBlurVertical"].width;
		fbci.height = renderTargets["gaussianBlurVertical"].height;
		fbci.layers = 1;
		frameBuffers[i] = vulkan->device.createFramebuffer(fbci);
	}

	for (size_t i = vulkan->swapchain->images.size() * 3; i < vulkan->swapchain->images.size() * 4; ++i) {
		std::vector<vk::ImageView> attachments = {
			renderTargets["viewport"].view
		};
		vk::FramebufferCreateInfo fbci;
		fbci.renderPass = renderPassCombine;
		fbci.attachmentCount = static_cast<uint32_t>(attachments.size());
		fbci.pAttachments = attachments.data();
		fbci.width = renderTargets["viewport"].width;
		fbci.height = renderTargets["viewport"].height;
		fbci.layers = 1;
		frameBuffers[i] = vulkan->device.createFramebuffer(fbci);
	}
}

void vm::Bloom::createPipelines(std::map<std::string, Image>& renderTargets)
{
	createBrightFilterPipeline(renderTargets);
	createGaussianBlurHorizontaPipeline(renderTargets);
	createGaussianBlurVerticalPipeline(renderTargets);
	createCombinePipeline(renderTargets);
}

void Bloom::createUniforms(std::map<std::string, Image>& renderTargets)
{
	vk::DescriptorSetAllocateInfo allocateInfo;
	allocateInfo.descriptorPool = vulkan->descriptorPool;
	allocateInfo.descriptorSetCount = 1;

	// Composition image to Bright Filter shader
	allocateInfo.pSetLayouts = &DSLayoutBrightFilter;
	DSBrightFilter = vulkan->device.allocateDescriptorSets(allocateInfo).at(0);

	// Bright Filter image to Gaussian Blur Horizontal shader
	allocateInfo.pSetLayouts = &DSLayoutGaussianBlurHorizontal;
	DSGaussianBlurHorizontal = vulkan->device.allocateDescriptorSets(allocateInfo).at(0);

	// Gaussian Blur Horizontal image to Gaussian Blur Vertical shader
	allocateInfo.pSetLayouts = &DSLayoutGaussianBlurVertical;
	DSGaussianBlurVertical = vulkan->device.allocateDescriptorSets(allocateInfo).at(0);

	// Gaussian Blur Vertical image to Combine shader
	allocateInfo.pSetLayouts = &DSLayoutCombine;
	DSCombine = vulkan->device.allocateDescriptorSets(allocateInfo).at(0);

	updateDescriptorSets(renderTargets);
}

void Bloom::updateDescriptorSets(std::map<std::string, Image>& renderTargets)
{
	std::deque<vk::DescriptorImageInfo> dsii{};
	auto wSetImage = [&dsii](vk::DescriptorSet& dstSet, uint32_t dstBinding, Image& image) {
		dsii.push_back({ image.sampler, image.view, vk::ImageLayout::eShaderReadOnlyOptimal });
		return vk::WriteDescriptorSet{ dstSet, dstBinding, 0, 1, vk::DescriptorType::eCombinedImageSampler, &dsii.back(), nullptr, nullptr };
	};
	std::deque<vk::DescriptorBufferInfo> dsbi{};
	auto wSetBuffer = [&dsbi](vk::DescriptorSet& dstSet, uint32_t dstBinding, Buffer& buffer) {
		dsbi.push_back({ buffer.buffer, 0, buffer.size });
		return vk::WriteDescriptorSet{ dstSet, dstBinding, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &dsbi.back(), nullptr };
	};

	std::vector<vk::WriteDescriptorSet> textureWriteSets{
		wSetImage(DSBrightFilter, 0, frameImage),
		wSetImage(DSGaussianBlurHorizontal, 0, renderTargets["brightFilter"]),
		wSetImage(DSGaussianBlurVertical, 0, renderTargets["gaussianBlurHorizontal"]),
		wSetImage(DSCombine, 0, frameImage),
		wSetImage(DSCombine, 1, renderTargets["gaussianBlurVertical"])
	};
	vulkan->device.updateDescriptorSets(textureWriteSets, nullptr);
}

void Bloom::draw(vk::CommandBuffer cmd, uint32_t imageIndex, uint32_t totalImages, std::function<void(vk::CommandBuffer, Image&, LayoutState)>&& changeLayout, std::map<std::string, Image>& renderTargets)
{
	vk::ClearValue clearColor;
	memcpy(clearColor.color.float32, GUI::clearColor.data(), 4 * sizeof(float));

	std::vector<vk::ClearValue> clearValues = { clearColor };

	std::vector<float> values{ GUI::Bloom_Inv_brightness, GUI::Bloom_intensity, GUI::Bloom_range, GUI::Bloom_exposure, static_cast<float>(GUI::use_tonemap) };

	vk::RenderPassBeginInfo rpi;
	rpi.renderPass = renderPassBrightFilter;
	rpi.framebuffer = frameBuffers[imageIndex];
	rpi.renderArea.offset = { 0, 0 };
	rpi.renderArea.extent = renderTargets["brightFilter"].extent;
	rpi.clearValueCount = 1;
	rpi.pClearValues = clearValues.data();

	changeLayout(cmd, renderTargets["brightFilter"], LayoutState::Write);
	cmd.beginRenderPass(rpi, vk::SubpassContents::eInline);
	cmd.pushConstants<float>(pipelineBrightFilter.pipeinfo.layout, vk::ShaderStageFlagBits::eFragment, 0, values);
	cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelineBrightFilter.pipeline);
	cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineBrightFilter.pipeinfo.layout, 0, DSBrightFilter, nullptr);
	cmd.draw(3, 1, 0, 0);
	cmd.endRenderPass();
	changeLayout(cmd, renderTargets["brightFilter"], LayoutState::Read);

	rpi.renderPass = renderPassGaussianBlur;
	rpi.framebuffer = frameBuffers[(size_t)totalImages + (size_t)imageIndex];

	changeLayout(cmd, renderTargets["gaussianBlurHorizontal"], LayoutState::Write);
	cmd.beginRenderPass(rpi, vk::SubpassContents::eInline);
	cmd.pushConstants<float>(pipelineGaussianBlurHorizontal.pipeinfo.layout, vk::ShaderStageFlagBits::eFragment, 0, values);
	cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelineGaussianBlurHorizontal.pipeline);
	cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineBrightFilter.pipeinfo.layout, 0, DSGaussianBlurHorizontal, nullptr);
	cmd.draw(3, 1, 0, 0);
	cmd.endRenderPass();
	changeLayout(cmd, renderTargets["gaussianBlurHorizontal"], LayoutState::Read);

	rpi.framebuffer = frameBuffers[(size_t)totalImages * 2 + (size_t)imageIndex];

	changeLayout(cmd, renderTargets["gaussianBlurVertical"], LayoutState::Write);
	cmd.beginRenderPass(rpi, vk::SubpassContents::eInline);
	cmd.pushConstants<float>(pipelineGaussianBlurVertical.pipeinfo.layout, vk::ShaderStageFlagBits::eFragment, 0, values);
	cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelineGaussianBlurVertical.pipeline);
	cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineGaussianBlurVertical.pipeinfo.layout, 0, DSGaussianBlurVertical, nullptr);
	cmd.draw(3, 1, 0, 0);
	cmd.endRenderPass();
	changeLayout(cmd, renderTargets["gaussianBlurVertical"], LayoutState::Read);

	rpi.renderPass = renderPassCombine;
	rpi.framebuffer = frameBuffers[(size_t)totalImages * 3 + (size_t)imageIndex];

	cmd.beginRenderPass(rpi, vk::SubpassContents::eInline);
	cmd.pushConstants<float>(pipelineCombine.pipeinfo.layout, vk::ShaderStageFlagBits::eFragment, 0, values);
	cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelineCombine.pipeline);
	cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineCombine.pipeinfo.layout, 0, DSCombine, nullptr);
	cmd.draw(3, 1, 0, 0);
	cmd.endRenderPass();
}

void Bloom::createBrightFilterPipeline(std::map<std::string, Image>& renderTargets)
{
	// Shader stages
	std::vector<char> vertCode = readFile("shaders/Common/vert.spv");
	vk::ShaderModuleCreateInfo vsmci;
	vsmci.codeSize = vertCode.size();
	vsmci.pCode = reinterpret_cast<const uint32_t*>(vertCode.data());
	vk::ShaderModule vertModule = vulkan->device.createShaderModule(vsmci);

	std::vector<char> fragCode = readFile("shaders/Bloom/fragBrightFilter.spv");
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
	pipelineBrightFilter.pipeinfo.stageCount = static_cast<uint32_t>(stages.size());
	pipelineBrightFilter.pipeinfo.pStages = stages.data();

	// Vertex Input state
	vk::PipelineVertexInputStateCreateInfo pvisci;
	pipelineBrightFilter.pipeinfo.pVertexInputState = &pvisci;

	// Input Assembly stage
	vk::PipelineInputAssemblyStateCreateInfo piasci;
	piasci.topology = vk::PrimitiveTopology::eTriangleList;
	piasci.primitiveRestartEnable = VK_FALSE;
	pipelineBrightFilter.pipeinfo.pInputAssemblyState = &piasci;

	// Viewports and Scissors
	vk::Viewport vp;
	vp.x = 0.0f;
	vp.y = 0.0f;
	vp.width = renderTargets["brightFilter"].width_f;
	vp.height = renderTargets["brightFilter"].height_f;
	vp.minDepth = 0.0f;
	vp.maxDepth = 1.0f;

	vk::Rect2D r2d;
	r2d.extent = vulkan->surface->actualExtent;

	vk::PipelineViewportStateCreateInfo pvsci;
	pvsci.viewportCount = 1;
	pvsci.pViewports = &vp;
	pvsci.scissorCount = 1;
	pvsci.pScissors = &r2d;
	pipelineBrightFilter.pipeinfo.pViewportState = &pvsci;

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
	pipelineBrightFilter.pipeinfo.pRasterizationState = &prsci;

	// Multisample state
	vk::PipelineMultisampleStateCreateInfo pmsci;
	pmsci.rasterizationSamples = vk::SampleCountFlagBits::e1;
	pmsci.sampleShadingEnable = VK_FALSE;
	pmsci.minSampleShading = 1.0f;
	pmsci.pSampleMask = nullptr;
	pmsci.alphaToCoverageEnable = VK_FALSE;
	pmsci.alphaToOneEnable = VK_FALSE;
	pipelineBrightFilter.pipeinfo.pMultisampleState = &pmsci;

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
	pipelineBrightFilter.pipeinfo.pDepthStencilState = &pdssci;

	// Color Blending state
	std::vector<vk::PipelineColorBlendAttachmentState> colorBlendAttachments = {
		renderTargets["brightFilter"].blentAttachment
	};
	vk::PipelineColorBlendStateCreateInfo pcbsci;
	pcbsci.logicOpEnable = VK_FALSE;
	pcbsci.logicOp = vk::LogicOp::eCopy;
	pcbsci.attachmentCount = static_cast<uint32_t>(colorBlendAttachments.size());
	pcbsci.pAttachments = colorBlendAttachments.data();
	float blendConstants[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	memcpy(pcbsci.blendConstants, blendConstants, 4 * sizeof(float));
	pipelineBrightFilter.pipeinfo.pColorBlendState = &pcbsci;

	// Dynamic state
	pipelineBrightFilter.pipeinfo.pDynamicState = nullptr;

	// Pipeline Layout
	if (!DSLayoutBrightFilter)
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
		DSLayoutBrightFilter = vulkan->device.createDescriptorSetLayout(descriptorLayout);
	}

	std::vector<vk::DescriptorSetLayout> descriptorSetLayouts = { DSLayoutBrightFilter };

	vk::PushConstantRange pConstants;
	pConstants.stageFlags = vk::ShaderStageFlagBits::eFragment;
	pConstants.offset = 0;
	pConstants.size = 5 * sizeof(vec4);

	vk::PipelineLayoutCreateInfo plci;
	plci.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
	plci.pSetLayouts = descriptorSetLayouts.data();
	plci.pushConstantRangeCount = 1;
	plci.pPushConstantRanges = &pConstants;
	pipelineBrightFilter.pipeinfo.layout = vulkan->device.createPipelineLayout(plci);

	// Render Pass
	pipelineBrightFilter.pipeinfo.renderPass = renderPassBrightFilter;

	// Subpass (Index of subpass this pipeline will be used in)
	pipelineBrightFilter.pipeinfo.subpass = 0;

	// Base Pipeline Handle
	pipelineBrightFilter.pipeinfo.basePipelineHandle = nullptr;

	// Base Pipeline Index
	pipelineBrightFilter.pipeinfo.basePipelineIndex = -1;

	pipelineBrightFilter.pipeline = vulkan->device.createGraphicsPipelines(nullptr, pipelineBrightFilter.pipeinfo).at(0);

	// destroy Shader Modules
	vulkan->device.destroyShaderModule(vertModule);
	vulkan->device.destroyShaderModule(fragModule);
}

void Bloom::createGaussianBlurHorizontaPipeline(std::map<std::string, Image>& renderTargets)
{
	// Shader stages
	std::vector<char> vertCode = readFile("shaders/Common/vert.spv");
	vk::ShaderModuleCreateInfo vsmci;
	vsmci.codeSize = vertCode.size();
	vsmci.pCode = reinterpret_cast<const uint32_t*>(vertCode.data());
	vk::ShaderModule vertModule = vulkan->device.createShaderModule(vsmci);

	std::vector<char> fragCode = readFile("shaders/Bloom/fragGaussianBlurHorizontal.spv");
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
	pipelineGaussianBlurHorizontal.pipeinfo.stageCount = static_cast<uint32_t>(stages.size());
	pipelineGaussianBlurHorizontal.pipeinfo.pStages = stages.data();

	// Vertex Input state
	vk::PipelineVertexInputStateCreateInfo pvisci;
	pipelineGaussianBlurHorizontal.pipeinfo.pVertexInputState = &pvisci;

	// Input Assembly stage
	vk::PipelineInputAssemblyStateCreateInfo piasci;
	piasci.topology = vk::PrimitiveTopology::eTriangleList;
	piasci.primitiveRestartEnable = VK_FALSE;
	pipelineGaussianBlurHorizontal.pipeinfo.pInputAssemblyState = &piasci;

	// Viewports and Scissors
	vk::Viewport vp;
	vp.x = 0.0f;
	vp.y = 0.0f;
	vp.width = renderTargets["gaussianBlurHorizontal"].width_f;
	vp.height = renderTargets["gaussianBlurHorizontal"].height_f;
	vp.minDepth = 0.0f;
	vp.maxDepth = 1.0f;

	vk::Rect2D r2d;
	r2d.extent = vulkan->surface->actualExtent;

	vk::PipelineViewportStateCreateInfo pvsci;
	pvsci.viewportCount = 1;
	pvsci.pViewports = &vp;
	pvsci.scissorCount = 1;
	pvsci.pScissors = &r2d;
	pipelineGaussianBlurHorizontal.pipeinfo.pViewportState = &pvsci;

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
	pipelineGaussianBlurHorizontal.pipeinfo.pRasterizationState = &prsci;

	// Multisample state
	vk::PipelineMultisampleStateCreateInfo pmsci;
	pmsci.rasterizationSamples = vk::SampleCountFlagBits::e1;
	pmsci.sampleShadingEnable = VK_FALSE;
	pmsci.minSampleShading = 1.0f;
	pmsci.pSampleMask = nullptr;
	pmsci.alphaToCoverageEnable = VK_FALSE;
	pmsci.alphaToOneEnable = VK_FALSE;
	pipelineGaussianBlurHorizontal.pipeinfo.pMultisampleState = &pmsci;

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
	pipelineGaussianBlurHorizontal.pipeinfo.pDepthStencilState = &pdssci;

	// Color Blending state
	std::vector<vk::PipelineColorBlendAttachmentState> colorBlendAttachments = {
		renderTargets["gaussianBlurHorizontal"].blentAttachment
	};
	vk::PipelineColorBlendStateCreateInfo pcbsci;
	pcbsci.logicOpEnable = VK_FALSE;
	pcbsci.logicOp = vk::LogicOp::eCopy;
	pcbsci.attachmentCount = static_cast<uint32_t>(colorBlendAttachments.size());
	pcbsci.pAttachments = colorBlendAttachments.data();
	float blendConstants[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	memcpy(pcbsci.blendConstants, blendConstants, 4 * sizeof(float));
	pipelineGaussianBlurHorizontal.pipeinfo.pColorBlendState = &pcbsci;

	// Dynamic state
	pipelineGaussianBlurHorizontal.pipeinfo.pDynamicState = nullptr;

	// Pipeline Layout
	if (!DSLayoutGaussianBlurHorizontal)
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
		DSLayoutGaussianBlurHorizontal = vulkan->device.createDescriptorSetLayout(descriptorLayout);
	}

	std::vector<vk::DescriptorSetLayout> descriptorSetLayouts = { DSLayoutGaussianBlurHorizontal };

	vk::PushConstantRange pConstants;
	pConstants.stageFlags = vk::ShaderStageFlagBits::eFragment;
	pConstants.offset = 0;
	pConstants.size = 5 * sizeof(vec4);

	vk::PipelineLayoutCreateInfo plci;
	plci.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
	plci.pSetLayouts = descriptorSetLayouts.data();
	plci.pushConstantRangeCount = 1;
	plci.pPushConstantRanges = &pConstants;
	pipelineGaussianBlurHorizontal.pipeinfo.layout = vulkan->device.createPipelineLayout(plci);

	// Render Pass
	pipelineGaussianBlurHorizontal.pipeinfo.renderPass = renderPassGaussianBlur;

	// Subpass (Index of subpass this pipeline will be used in)
	pipelineGaussianBlurHorizontal.pipeinfo.subpass = 0;

	// Base Pipeline Handle
	pipelineGaussianBlurHorizontal.pipeinfo.basePipelineHandle = nullptr;

	// Base Pipeline Index
	pipelineGaussianBlurHorizontal.pipeinfo.basePipelineIndex = -1;

	pipelineGaussianBlurHorizontal.pipeline = vulkan->device.createGraphicsPipelines(nullptr, pipelineGaussianBlurHorizontal.pipeinfo).at(0);

	// destroy Shader Modules
	vulkan->device.destroyShaderModule(vertModule);
	vulkan->device.destroyShaderModule(fragModule);
}

void Bloom::createGaussianBlurVerticalPipeline(std::map<std::string, Image>& renderTargets)
{
	// Shader stages
	std::vector<char> vertCode = readFile("shaders/Common/vert.spv");
	vk::ShaderModuleCreateInfo vsmci;
	vsmci.codeSize = vertCode.size();
	vsmci.pCode = reinterpret_cast<const uint32_t*>(vertCode.data());
	vk::ShaderModule vertModule = vulkan->device.createShaderModule(vsmci);

	std::vector<char> fragCode = readFile("shaders/Bloom/fragGaussianBlurVertical.spv");
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
	pipelineGaussianBlurVertical.pipeinfo.stageCount = static_cast<uint32_t>(stages.size());
	pipelineGaussianBlurVertical.pipeinfo.pStages = stages.data();

	// Vertex Input state
	vk::PipelineVertexInputStateCreateInfo pvisci;
	pipelineGaussianBlurVertical.pipeinfo.pVertexInputState = &pvisci;

	// Input Assembly stage
	vk::PipelineInputAssemblyStateCreateInfo piasci;
	piasci.topology = vk::PrimitiveTopology::eTriangleList;
	piasci.primitiveRestartEnable = VK_FALSE;
	pipelineGaussianBlurVertical.pipeinfo.pInputAssemblyState = &piasci;

	// Viewports and Scissors
	vk::Viewport vp;
	vp.x = 0.0f;
	vp.y = 0.0f;
	vp.width = renderTargets["gaussianBlurVertical"].width_f;
	vp.height = renderTargets["gaussianBlurVertical"].height_f;
	vp.minDepth = 0.0f;
	vp.maxDepth = 1.0f;

	vk::Rect2D r2d;
	r2d.extent = vulkan->surface->actualExtent;

	vk::PipelineViewportStateCreateInfo pvsci;
	pvsci.viewportCount = 1;
	pvsci.pViewports = &vp;
	pvsci.scissorCount = 1;
	pvsci.pScissors = &r2d;
	pipelineGaussianBlurVertical.pipeinfo.pViewportState = &pvsci;

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
	pipelineGaussianBlurVertical.pipeinfo.pRasterizationState = &prsci;

	// Multisample state
	vk::PipelineMultisampleStateCreateInfo pmsci;
	pmsci.rasterizationSamples = vk::SampleCountFlagBits::e1;
	pmsci.sampleShadingEnable = VK_FALSE;
	pmsci.minSampleShading = 1.0f;
	pmsci.pSampleMask = nullptr;
	pmsci.alphaToCoverageEnable = VK_FALSE;
	pmsci.alphaToOneEnable = VK_FALSE;
	pipelineGaussianBlurVertical.pipeinfo.pMultisampleState = &pmsci;

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
	pipelineGaussianBlurVertical.pipeinfo.pDepthStencilState = &pdssci;

	// Color Blending state
	std::vector<vk::PipelineColorBlendAttachmentState> colorBlendAttachments = {
		renderTargets["gaussianBlurVertical"].blentAttachment
	};
	vk::PipelineColorBlendStateCreateInfo pcbsci;
	pcbsci.logicOpEnable = VK_FALSE;
	pcbsci.logicOp = vk::LogicOp::eCopy;
	pcbsci.attachmentCount = static_cast<uint32_t>(colorBlendAttachments.size());
	pcbsci.pAttachments = colorBlendAttachments.data();
	float blendConstants[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	memcpy(pcbsci.blendConstants, blendConstants, 4 * sizeof(float));
	pipelineGaussianBlurVertical.pipeinfo.pColorBlendState = &pcbsci;

	// Dynamic state
	pipelineGaussianBlurVertical.pipeinfo.pDynamicState = nullptr;

	// Pipeline Layout
	if (!DSLayoutGaussianBlurVertical)
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
		DSLayoutGaussianBlurVertical = vulkan->device.createDescriptorSetLayout(descriptorLayout);
	}

	std::vector<vk::DescriptorSetLayout> descriptorSetLayouts = { DSLayoutGaussianBlurVertical };
	vk::PushConstantRange pConstants;
	pConstants.stageFlags = vk::ShaderStageFlagBits::eFragment;
	pConstants.offset = 0;
	pConstants.size = 5 * sizeof(vec4);

	vk::PipelineLayoutCreateInfo plci;
	plci.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
	plci.pSetLayouts = descriptorSetLayouts.data();
	plci.pushConstantRangeCount = 1;
	plci.pPushConstantRanges = &pConstants;
	pipelineGaussianBlurVertical.pipeinfo.layout = vulkan->device.createPipelineLayout(plci);

	// Render Pass
	pipelineGaussianBlurVertical.pipeinfo.renderPass = renderPassGaussianBlur;

	// Subpass (Index of subpass this pipeline will be used in)
	pipelineGaussianBlurVertical.pipeinfo.subpass = 0;

	// Base Pipeline Handle
	pipelineGaussianBlurVertical.pipeinfo.basePipelineHandle = nullptr;

	// Base Pipeline Index
	pipelineGaussianBlurVertical.pipeinfo.basePipelineIndex = -1;

	pipelineGaussianBlurVertical.pipeline = vulkan->device.createGraphicsPipelines(nullptr, pipelineGaussianBlurVertical.pipeinfo).at(0);

	// destroy Shader Modules
	vulkan->device.destroyShaderModule(vertModule);
	vulkan->device.destroyShaderModule(fragModule);
}

void Bloom::createCombinePipeline(std::map<std::string, Image>& renderTargets)
{
	// Shader stages
	std::vector<char> vertCode = readFile("shaders/Common/vert.spv");
	vk::ShaderModuleCreateInfo vsmci;
	vsmci.codeSize = vertCode.size();
	vsmci.pCode = reinterpret_cast<const uint32_t*>(vertCode.data());
	vk::ShaderModule vertModule = vulkan->device.createShaderModule(vsmci);

	std::vector<char> fragCode = readFile("shaders/Bloom/fragCombine.spv");
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
	pipelineCombine.pipeinfo.stageCount = static_cast<uint32_t>(stages.size());
	pipelineCombine.pipeinfo.pStages = stages.data();

	// Vertex Input state
	vk::PipelineVertexInputStateCreateInfo pvisci;
	pipelineCombine.pipeinfo.pVertexInputState = &pvisci;

	// Input Assembly stage
	vk::PipelineInputAssemblyStateCreateInfo piasci;
	piasci.topology = vk::PrimitiveTopology::eTriangleList;
	piasci.primitiveRestartEnable = VK_FALSE;
	pipelineCombine.pipeinfo.pInputAssemblyState = &piasci;

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
	pipelineCombine.pipeinfo.pViewportState = &pvsci;

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
	pipelineCombine.pipeinfo.pRasterizationState = &prsci;

	// Multisample state
	vk::PipelineMultisampleStateCreateInfo pmsci;
	pmsci.rasterizationSamples = vk::SampleCountFlagBits::e1;
	pmsci.sampleShadingEnable = VK_FALSE;
	pmsci.minSampleShading = 1.0f;
	pmsci.pSampleMask = nullptr;
	pmsci.alphaToCoverageEnable = VK_FALSE;
	pmsci.alphaToOneEnable = VK_FALSE;
	pipelineCombine.pipeinfo.pMultisampleState = &pmsci;

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
	pipelineCombine.pipeinfo.pDepthStencilState = &pdssci;

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
	pipelineCombine.pipeinfo.pColorBlendState = &pcbsci;

	// Dynamic state
	pipelineCombine.pipeinfo.pDynamicState = nullptr;

	// Pipeline Layout
	if (!DSLayoutCombine)
	{
		auto layoutBinding = [](uint32_t binding, vk::DescriptorType descriptorType) {
			return vk::DescriptorSetLayoutBinding{ binding, descriptorType, 1, vk::ShaderStageFlagBits::eFragment, nullptr };
		};
		std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings{
			layoutBinding(0, vk::DescriptorType::eCombinedImageSampler),
			layoutBinding(1, vk::DescriptorType::eCombinedImageSampler),
		};
		vk::DescriptorSetLayoutCreateInfo descriptorLayout;
		descriptorLayout.bindingCount = static_cast<uint32_t>(setLayoutBindings.size());
		descriptorLayout.pBindings = setLayoutBindings.data();
		DSLayoutCombine = vulkan->device.createDescriptorSetLayout(descriptorLayout);
	}

	std::vector<vk::DescriptorSetLayout> descriptorSetLayouts = { DSLayoutCombine };

	vk::PushConstantRange pConstants;
	pConstants.stageFlags = vk::ShaderStageFlagBits::eFragment;
	pConstants.offset = 0;
	pConstants.size = 5 * sizeof(vec4);

	vk::PipelineLayoutCreateInfo plci;
	plci.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
	plci.pSetLayouts = descriptorSetLayouts.data();
	plci.pushConstantRangeCount = 1;
	plci.pPushConstantRanges = &pConstants;
	pipelineCombine.pipeinfo.layout = vulkan->device.createPipelineLayout(plci);

	// Render Pass
	pipelineCombine.pipeinfo.renderPass = renderPassCombine;

	// Subpass (Index of subpass this pipeline will be used in)
	pipelineCombine.pipeinfo.subpass = 0;

	// Base Pipeline Handle
	pipelineCombine.pipeinfo.basePipelineHandle = nullptr;

	// Base Pipeline Index
	pipelineCombine.pipeinfo.basePipelineIndex = -1;

	pipelineCombine.pipeline = vulkan->device.createGraphicsPipelines(nullptr, pipelineCombine.pipeinfo).at(0);

	// destroy Shader Modules
	vulkan->device.destroyShaderModule(vertModule);
	vulkan->device.destroyShaderModule(fragModule);
}

void Bloom::destroy()
{
	for (auto &frameBuffer : frameBuffers) {
		if (frameBuffer) {
			vulkan->device.destroyFramebuffer(frameBuffer);
		}
	}
	if (renderPassBrightFilter) {
		vulkan->device.destroyRenderPass(renderPassBrightFilter);
		renderPassBrightFilter = nullptr;
	}
	if (renderPassGaussianBlur) {
		vulkan->device.destroyRenderPass(renderPassGaussianBlur);
		renderPassGaussianBlur = nullptr;
	}
	if (renderPassCombine) {
		vulkan->device.destroyRenderPass(renderPassCombine);
		renderPassCombine = nullptr;
	}
	if (DSLayoutBrightFilter) {
		vulkan->device.destroyDescriptorSetLayout(DSLayoutBrightFilter);
		DSLayoutBrightFilter = nullptr;
	}
	if (DSLayoutGaussianBlurHorizontal) {
		vulkan->device.destroyDescriptorSetLayout(DSLayoutGaussianBlurHorizontal);
		DSLayoutGaussianBlurHorizontal = nullptr;
	}
	if (DSLayoutGaussianBlurVertical) {
		vulkan->device.destroyDescriptorSetLayout(DSLayoutGaussianBlurVertical);
		DSLayoutGaussianBlurVertical = nullptr;
	}
	if (DSLayoutCombine) {
		vulkan->device.destroyDescriptorSetLayout(DSLayoutCombine);
		DSLayoutCombine = nullptr;
	}
	frameImage.destroy();
	pipelineBrightFilter.destroy();
	pipelineGaussianBlurHorizontal.destroy();
	pipelineGaussianBlurVertical.destroy();
	pipelineCombine.destroy();
}
