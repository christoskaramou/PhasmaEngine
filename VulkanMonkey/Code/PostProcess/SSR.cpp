#include "SSR.h"
#include <deque>
#include "../GUI/GUI.h"
#include "../Swapchain/Swapchain.h"
#include "../Surface/Surface.h"
#include "../Shader/Shader.h"
#include "../Queue/Queue.h"

using namespace vm;

void SSR::createSSRUniforms(std::map<std::string, Image>& renderTargets)
{
	UBReflection.createBuffer(4 * sizeof(mat4), vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostVisible);
	UBReflection.map();
	UBReflection.zero();
	UBReflection.flush();
	UBReflection.unmap();

	vk::DescriptorSetAllocateInfo allocateInfo2;
	allocateInfo2.descriptorPool = VulkanContext::get()->descriptorPool;
	allocateInfo2.descriptorSetCount = 1;
	allocateInfo2.pSetLayouts = &DSLayoutReflection;
	DSReflection = VulkanContext::get()->device.allocateDescriptorSets(allocateInfo2).at(0);

	updateDescriptorSets(renderTargets);
}

void SSR::updateDescriptorSets(std::map<std::string, Image>& renderTargets)
{
	std::deque<vk::DescriptorImageInfo> dsii{};
	const auto wSetImage = [&dsii](vk::DescriptorSet& dstSet, uint32_t dstBinding, Image& image) {
		dsii.emplace_back(image.sampler, image.view, vk::ImageLayout::eShaderReadOnlyOptimal);
		return vk::WriteDescriptorSet{ dstSet, dstBinding, 0, 1, vk::DescriptorType::eCombinedImageSampler, &dsii.back(), nullptr, nullptr };
	};
	std::deque<vk::DescriptorBufferInfo> dsbi{};
	const auto wSetBuffer = [&dsbi](vk::DescriptorSet& dstSet, uint32_t dstBinding, Buffer& buffer) {
		dsbi.emplace_back(buffer.buffer, 0, buffer.size);
		return vk::WriteDescriptorSet{ dstSet, dstBinding, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &dsbi.back(), nullptr };
	};

	std::vector<vk::WriteDescriptorSet> textureWriteSets{
		wSetImage(DSReflection, 0, renderTargets["albedo"]),
		wSetImage(DSReflection, 1, renderTargets["depth"]),
		wSetImage(DSReflection, 2, renderTargets["normal"]),
		wSetImage(DSReflection, 3, renderTargets["srm"]),
		wSetBuffer(DSReflection, 4, UBReflection)
	};
	VulkanContext::get()->device.updateDescriptorSets(textureWriteSets, nullptr);
}

void SSR::update(Camera& camera)
{
	if (GUI::show_ssr) {
		reflectionInput[0][0] = vec4(camera.position, 1.0f);
		reflectionInput[0][1] = vec4(camera.front, 1.0f);
		reflectionInput[0][2] = vec4();
		reflectionInput[0][3] = vec4();
		reflectionInput[1] = camera.projection;
		reflectionInput[2] = camera.view;
		reflectionInput[3] = camera.invProjection;
		
		Queue::memcpyRequest(&UBReflection, { { &reflectionInput, sizeof(reflectionInput), 0 } });
		//UBReflection.map();
		//memcpy(UBReflection.data, &reflectionInput, sizeof(reflectionInput));
		//UBReflection.flush();
		//UBReflection.unmap();
	}
}


void SSR::draw(vk::CommandBuffer cmd, uint32_t imageIndex, const vk::Extent2D& extent)
{
	vk::ClearValue clearColor;
	memcpy(clearColor.color.float32, GUI::clearColor.data(), 4 * sizeof(float));

	std::vector<vk::ClearValue> clearValues = { clearColor };

	vk::RenderPassBeginInfo renderPassInfo;
	renderPassInfo.renderPass = *renderPass;
	renderPassInfo.framebuffer = *framebuffers[imageIndex];
	renderPassInfo.renderArea.offset = vk::Offset2D{ 0, 0 };
	renderPassInfo.renderArea.extent = extent;
	renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
	renderPassInfo.pClearValues = clearValues.data();

	cmd.beginRenderPass(&renderPassInfo, vk::SubpassContents::eInline);
	cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline.pipeline);
	cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline.pipeinfo->layout, 0, DSReflection, nullptr);
	cmd.draw(3, 1, 0, 0);
	cmd.endRenderPass();
}

void SSR::createRenderPass(std::map<std::string, Image>& renderTargets)
{
	renderPass.Create(renderTargets["ssr"].format, vk::Format::eUndefined);
}

void SSR::createFrameBuffers(std::map<std::string, Image>& renderTargets)
{
	auto vulkan = VulkanContext::get();
	framebuffers.resize(vulkan->swapchain->images.size());
	for (size_t i = 0; i < vulkan->swapchain->images.size(); ++i)
	{
		uint32_t width = renderTargets["ssr"].width;
		uint32_t height = renderTargets["ssr"].height;
		vk::ImageView view = renderTargets["ssr"].view;
		framebuffers[i].Create(width, height, view, renderPass);
	}
}

void SSR::createPipeline(std::map<std::string, Image>& renderTargets)
{
	// Shader stages
	Shader vert{ "shaders/Common/quad.vert", ShaderType::Vertex, true };
	Shader frag{ "shaders/SSR/ssr.frag", ShaderType::Fragment, true };

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
	pipeline.pipeinfo->stageCount = static_cast<uint32_t>(stages.size());
	pipeline.pipeinfo->pStages = stages.data();

	// Vertex Input state
	vk::PipelineVertexInputStateCreateInfo pvisci;
	pipeline.pipeinfo->pVertexInputState = &pvisci;

	// Input Assembly stage
	vk::PipelineInputAssemblyStateCreateInfo piasci;
	piasci.topology = vk::PrimitiveTopology::eTriangleList;
	piasci.primitiveRestartEnable = VK_FALSE;
	pipeline.pipeinfo->pInputAssemblyState = &piasci;

	// Viewports and Scissors
	vk::Viewport vp;
	vp.x = 0.0f;
	vp.y = 0.0f;
	vp.width = renderTargets["ssr"].width_f;
	vp.height = renderTargets["ssr"].height_f;
	vp.minDepth = 0.0f;
	vp.maxDepth = 1.0f;

	vk::Rect2D r2d;
	r2d.extent = vk::Extent2D{ static_cast<uint32_t>(vp.width), static_cast<uint32_t>(vp.height) };

	vk::PipelineViewportStateCreateInfo pvsci;
	pvsci.viewportCount = 1;
	pvsci.pViewports = &vp;
	pvsci.scissorCount = 1;
	pvsci.pScissors = &r2d;
	pipeline.pipeinfo->pViewportState = &pvsci;

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
	pipeline.pipeinfo->pRasterizationState = &prsci;

	// Multisample state
	vk::PipelineMultisampleStateCreateInfo pmsci;
	pmsci.rasterizationSamples = vk::SampleCountFlagBits::e1;
	pmsci.sampleShadingEnable = VK_FALSE;
	pmsci.minSampleShading = 1.0f;
	pmsci.pSampleMask = nullptr;
	pmsci.alphaToCoverageEnable = VK_FALSE;
	pmsci.alphaToOneEnable = VK_FALSE;
	pipeline.pipeinfo->pMultisampleState = &pmsci;

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
	pipeline.pipeinfo->pDepthStencilState = &pdssci;

	// Color Blending state
	std::vector<vk::PipelineColorBlendAttachmentState> colorBlendAttachments = {
			renderTargets["ssr"].blentAttachment,
	};
	vk::PipelineColorBlendStateCreateInfo pcbsci;
	pcbsci.logicOpEnable = VK_FALSE;
	pcbsci.logicOp = vk::LogicOp::eCopy;
	pcbsci.attachmentCount = static_cast<uint32_t>(colorBlendAttachments.size());
	pcbsci.pAttachments = colorBlendAttachments.data();
	float blendConstants[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	memcpy(pcbsci.blendConstants, blendConstants, 4 * sizeof(float));
	pipeline.pipeinfo->pColorBlendState = &pcbsci;

	// Dynamic state
	pipeline.pipeinfo->pDynamicState = nullptr;

	// Pipeline Layout
	if (!DSLayoutReflection)
	{
		auto layoutBinding = [](uint32_t binding, vk::DescriptorType descriptorType) {
			return vk::DescriptorSetLayoutBinding{ binding, descriptorType, 1, vk::ShaderStageFlagBits::eFragment, nullptr };
		};
		std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings{
			layoutBinding(0, vk::DescriptorType::eCombinedImageSampler),
			layoutBinding(1, vk::DescriptorType::eCombinedImageSampler),
			layoutBinding(2, vk::DescriptorType::eCombinedImageSampler),
			layoutBinding(3, vk::DescriptorType::eCombinedImageSampler),
			layoutBinding(4, vk::DescriptorType::eUniformBuffer),
		};
		vk::DescriptorSetLayoutCreateInfo descriptorLayout;
		descriptorLayout.bindingCount = static_cast<uint32_t>(setLayoutBindings.size());
		descriptorLayout.pBindings = setLayoutBindings.data();
		DSLayoutReflection = VulkanContext::get()->device.createDescriptorSetLayout(descriptorLayout);
	}

	std::vector<vk::DescriptorSetLayout> descriptorSetLayouts = { DSLayoutReflection };

	//vk::PushConstantRange pConstants;
	//pConstants.stageFlags = vk::ShaderStageFlagBits::eFragment;
	//pConstants.offset = 0;
	//pConstants.size = 4 * sizeof(vec4);

	vk::PipelineLayoutCreateInfo plci;
	plci.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
	plci.pSetLayouts = descriptorSetLayouts.data();
	plci.pushConstantRangeCount = 0;
	plci.pPushConstantRanges = nullptr;
	pipeline.pipeinfo->layout = VulkanContext::get()->device.createPipelineLayout(plci);

	// Render Pass
	pipeline.pipeinfo->renderPass = *renderPass;

	// Subpass (Index of subpass this pipeline will be used in)
	pipeline.pipeinfo->subpass = 0;

	// Base Pipeline Handle
	pipeline.pipeinfo->basePipelineHandle = nullptr;

	// Base Pipeline Index
	pipeline.pipeinfo->basePipelineIndex = -1;

	pipeline.pipeline = CreateRef<vk::Pipeline>(VulkanContext::get()->device.createGraphicsPipelines(nullptr, *pipeline.pipeinfo).at(0));

	// destroy Shader Modules
	VulkanContext::get()->device.destroyShaderModule(vertModule);
	VulkanContext::get()->device.destroyShaderModule(fragModule);
}

void SSR::destroy()
{
	for (auto &framebuffer : framebuffers)
		framebuffer.Destroy();
	
	renderPass.Destroy();

	if (DSLayoutReflection) {
		VulkanContext::get()->device.destroyDescriptorSetLayout(DSLayoutReflection);
		DSLayoutReflection = nullptr;
	}
	UBReflection.destroy();
	pipeline.destroy();
}