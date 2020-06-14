#include "DOF.h"
#include "../GUI/GUI.h"
#include "../Swapchain/Swapchain.h"
#include "../Surface/Surface.h"
#include "../Shader/Shader.h"
#include <deque>

using namespace vm;

void DOF::Init()
{
	frameImage.format = VulkanContext::get()->surface->formatKHR.format;
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

void DOF::copyFrameImage(const vk::CommandBuffer& cmd, Image& renderedImage) const
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

void DOF::createRenderPass(std::map<std::string, Image>& renderTargets)
{
	renderPass = CreateRef<RenderPass>();
	renderPass->Create(renderTargets["viewport"].format, vk::Format::eUndefined);
}

void DOF::createFrameBuffers(std::map<std::string, Image>& renderTargets)
{
	auto vulkan = VulkanContext::get();
	frameBuffers.resize(vulkan->swapchain->images.size());
	for (size_t i = 0; i < vulkan->swapchain->images.size(); ++i) {
		std::vector<vk::ImageView> attachments = {
			renderTargets["viewport"].view
		};
		vk::FramebufferCreateInfo fbci;
		fbci.renderPass = *renderPass->GetRef();
		fbci.attachmentCount = static_cast<uint32_t>(attachments.size());
		fbci.pAttachments = attachments.data();
		fbci.width = renderTargets["viewport"].width;
		fbci.height = renderTargets["viewport"].height;
		fbci.layers = 1;
		frameBuffers[i] = vulkan->device.createFramebuffer(fbci);
	}
}

void DOF::createUniforms(std::map<std::string, Image>& renderTargets)
{
	auto vulkan = VulkanContext::get();
	vk::DescriptorSetAllocateInfo allocateInfo;
	allocateInfo.descriptorPool = vulkan->descriptorPool;
	allocateInfo.descriptorSetCount = 1;

	allocateInfo.pSetLayouts = &DSLayout;
	DSet = vulkan->device.allocateDescriptorSets(allocateInfo).at(0);

	updateDescriptorSets(renderTargets);
}

void DOF::updateDescriptorSets(std::map<std::string, Image>& renderTargets)
{
	std::deque<vk::DescriptorImageInfo> dsii{};
	auto const wSetImage = [&dsii](vk::DescriptorSet& dstSet, uint32_t dstBinding, Image& image) {
		dsii.emplace_back(image.sampler, image.view, vk::ImageLayout::eShaderReadOnlyOptimal);
		return vk::WriteDescriptorSet{ dstSet, dstBinding, 0, 1, vk::DescriptorType::eCombinedImageSampler, &dsii.back(), nullptr, nullptr };
	};

	std::vector<vk::WriteDescriptorSet> textureWriteSets{
		wSetImage(DSet, 0, frameImage),
		wSetImage(DSet, 1, renderTargets["depth"])
	};
	VulkanContext::get()->device.updateDescriptorSets(textureWriteSets, nullptr);
}

void DOF::draw(vk::CommandBuffer cmd, uint32_t imageIndex, std::map<std::string, Image>& renderTargets)
{
	vk::ClearValue clearColor;
	memcpy(clearColor.color.float32, GUI::clearColor.data(), 4 * sizeof(float));

	std::vector<vk::ClearValue> clearValues = { clearColor };

	std::vector<float> values{ GUI::DOF_focus_scale, GUI::DOF_blur_range, 0.0f, 0.0f };

	vk::RenderPassBeginInfo rpi;
	rpi.renderPass = *renderPass->GetRef();
	rpi.framebuffer = frameBuffers[imageIndex];
	rpi.renderArea.offset = vk::Offset2D{0, 0};
	rpi.renderArea.extent = renderTargets["viewport"].extent;
	rpi.clearValueCount = 1;
	rpi.pClearValues = clearValues.data();

	cmd.beginRenderPass(rpi, vk::SubpassContents::eInline);
	cmd.pushConstants<float>(pipeline.pipeinfo.layout, vk::ShaderStageFlagBits::eFragment, 0, values);
	cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline.pipeline);
	cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline.pipeinfo.layout, 0, DSet, nullptr);
	cmd.draw(3, 1, 0, 0);
	cmd.endRenderPass();
}

void DOF::createPipeline(std::map<std::string, Image>& renderTargets)
{
	// Shader stages
	Shader vert{ "shaders/Common/quad.vert", ShaderType::Vertex, true };
	Shader frag{ "shaders/DepthOfField/DOF.frag", ShaderType::Fragment, true };

	vk::ShaderModuleCreateInfo vsmci;
	vsmci.codeSize = vert.byte_size();
	vsmci.pCode = vert.get_spriv();
	vk::ShaderModule vertModule = VulkanContext::get()->device.createShaderModule(vsmci);

	vk::ShaderModuleCreateInfo fsmci;
	fsmci.codeSize = frag.byte_size();
	fsmci.pCode = frag.get_spriv();
	vk::ShaderModule fragModule = VulkanContext::get()->device.createShaderModule(fsmci);

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
	vp.width = renderTargets["viewport"].width_f;
	vp.height = renderTargets["viewport"].height_f;
	vp.minDepth = 0.0f;
	vp.maxDepth = 1.0f;

	vk::Rect2D r2d;
	r2d.extent = vk::Extent2D{ static_cast<uint32_t>(vp.width), static_cast<uint32_t>(vp.height) };

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
		renderTargets["viewport"].blentAttachment
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
		};
		vk::DescriptorSetLayoutCreateInfo descriptorLayout;
		descriptorLayout.bindingCount = static_cast<uint32_t>(setLayoutBindings.size());
		descriptorLayout.pBindings = setLayoutBindings.data();
		DSLayout = VulkanContext::get()->device.createDescriptorSetLayout(descriptorLayout);
	}

	std::vector<vk::DescriptorSetLayout> descriptorSetLayouts = { DSLayout };

	vk::PushConstantRange pConstants;
	pConstants.stageFlags = vk::ShaderStageFlagBits::eFragment;
	pConstants.offset = 0;
	pConstants.size = 5 * sizeof(vec4);

	vk::PipelineLayoutCreateInfo plci;
	plci.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
	plci.pSetLayouts = descriptorSetLayouts.data();
	plci.pushConstantRangeCount = 1;
	plci.pPushConstantRanges = &pConstants;
	pipeline.pipeinfo.layout = VulkanContext::get()->device.createPipelineLayout(plci);

	// Render Pass
	pipeline.pipeinfo.renderPass = *renderPass->GetRef();

	// Subpass (Index of subpass this pipeline will be used in)
	pipeline.pipeinfo.subpass = 0;

	// Base Pipeline Handle
	pipeline.pipeinfo.basePipelineHandle = nullptr;

	// Base Pipeline Index
	pipeline.pipeinfo.basePipelineIndex = -1;

	pipeline.pipeline = VulkanContext::get()->device.createGraphicsPipelines(nullptr, pipeline.pipeinfo).at(0);

	// destroy Shader Modules
	VulkanContext::get()->device.destroyShaderModule(vertModule);
	VulkanContext::get()->device.destroyShaderModule(fragModule);
}

void DOF::destroy()
{
	auto vulkan = VulkanContext::get();
	for (auto &frameBuffer : frameBuffers) {
		if (frameBuffer) {
			vulkan->device.destroyFramebuffer(frameBuffer);
		}
	}

	renderPass->Destroy();

	if (DSLayout) {
		vulkan->device.destroyDescriptorSetLayout(DSLayout);
		DSLayout = nullptr;
	}
	frameImage.destroy();
	pipeline.destroy();
}
