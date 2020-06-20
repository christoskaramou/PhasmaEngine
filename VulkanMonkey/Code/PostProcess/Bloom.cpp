#include "Bloom.h"
#include "../GUI/GUI.h"
#include "../Swapchain/Swapchain.h"
#include "../Core/Surface.h"
#include "../Shader/Shader.h"
#include "../VulkanContext/VulkanContext.h"
#include <vulkan/vulkan.hpp>
#include <deque>

namespace vm
{
	Bloom::Bloom()
	{
		DSBrightFilter = vk::DescriptorSet();
		DSGaussianBlurHorizontal = vk::DescriptorSet();
		DSGaussianBlurVertical = vk::DescriptorSet();
		DSCombine = vk::DescriptorSet();
		DSLayoutBrightFilter = vk::DescriptorSetLayout();
		DSLayoutGaussianBlurHorizontal = vk::DescriptorSetLayout();
		DSLayoutGaussianBlurVertical = vk::DescriptorSetLayout();
		DSLayoutCombine = vk::DescriptorSetLayout();
	}

	Bloom::~Bloom()
	{
	}

	void Bloom::Init()
	{
		frameImage.format = VulkanContext::get()->surface.formatKHR->format;
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

	void Bloom::copyFrameImage(const vk::CommandBuffer& cmd, Image& renderedImage) const
	{
		frameImage.copyColorAttachment(cmd, renderedImage);
	}

	void vm::Bloom::createRenderPasses(std::map<std::string, Image>& renderTargets)
	{
		renderPassBrightFilter.Create(renderTargets["brightFilter"].format.Value(), vk::Format::eUndefined);
		renderPassGaussianBlur.Create(renderTargets["gaussianBlurHorizontal"].format.Value(), vk::Format::eUndefined);
		renderPassCombine.Create(renderTargets["viewport"].format.Value(), vk::Format::eUndefined);
	}

	void vm::Bloom::createFrameBuffers(std::map<std::string, Image>& renderTargets)
	{
		auto vulkan = VulkanContext::get();
		framebuffers.resize(vulkan->swapchain.images.size() * 4);
		for (size_t i = 0; i < vulkan->swapchain.images.size(); ++i)
		{
			uint32_t width = renderTargets["brightFilter"].width.Value();
			uint32_t height = renderTargets["brightFilter"].height.Value();
			vk::ImageView view = renderTargets["brightFilter"].view.Value();
			framebuffers[i].Create(width, height, view, renderPassBrightFilter);
		}

		for (size_t i = vulkan->swapchain.images.size(); i < vulkan->swapchain.images.size() * 2; ++i)
		{
			uint32_t width = renderTargets["gaussianBlurHorizontal"].width.Value();
			uint32_t height = renderTargets["gaussianBlurHorizontal"].height.Value();
			vk::ImageView view = renderTargets["gaussianBlurHorizontal"].view.Value();
			framebuffers[i].Create(width, height, view, renderPassGaussianBlur);
		}

		for (size_t i = vulkan->swapchain.images.size() * 2; i < vulkan->swapchain.images.size() * 3; ++i)
		{
			uint32_t width = renderTargets["gaussianBlurVertical"].width.Value();
			uint32_t height = renderTargets["gaussianBlurVertical"].height.Value();
			vk::ImageView view = renderTargets["gaussianBlurVertical"].view.Value();
			framebuffers[i].Create(width, height, view, renderPassGaussianBlur);
		}

		for (size_t i = vulkan->swapchain.images.size() * 3; i < vulkan->swapchain.images.size() * 4; ++i)
		{
			uint32_t width = renderTargets["viewport"].width.Value();
			uint32_t height = renderTargets["viewport"].height.Value();
			vk::ImageView view = renderTargets["viewport"].view.Value();
			framebuffers[i].Create(width, height, view, renderPassCombine);
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
		auto vulkan = VulkanContext::get();
		vk::DescriptorSetAllocateInfo allocateInfo;
		allocateInfo.descriptorPool = vulkan->descriptorPool.Value();
		allocateInfo.descriptorSetCount = 1;

		// Composition image to Bright Filter shader
		allocateInfo.pSetLayouts = &*DSLayoutBrightFilter;
		DSBrightFilter = vulkan->device->allocateDescriptorSets(allocateInfo).at(0);

		// Bright Filter image to Gaussian Blur Horizontal shader
		allocateInfo.pSetLayouts = &*DSLayoutGaussianBlurHorizontal;
		DSGaussianBlurHorizontal = vulkan->device->allocateDescriptorSets(allocateInfo).at(0);

		// Gaussian Blur Horizontal image to Gaussian Blur Vertical shader
		allocateInfo.pSetLayouts = &*DSLayoutGaussianBlurVertical;
		DSGaussianBlurVertical = vulkan->device->allocateDescriptorSets(allocateInfo).at(0);

		// Gaussian Blur Vertical image to Combine shader
		allocateInfo.pSetLayouts = &*DSLayoutCombine;
		DSCombine = vulkan->device->allocateDescriptorSets(allocateInfo).at(0);

		updateDescriptorSets(renderTargets);
	}

	void Bloom::updateDescriptorSets(std::map<std::string, Image>& renderTargets)
	{
		std::deque<vk::DescriptorImageInfo> dsii{};
		auto const wSetImage = [&dsii](const vk::DescriptorSet& dstSet, uint32_t dstBinding, Image& image) {
			dsii.emplace_back(image.sampler.Value(), image.view.Value(), vk::ImageLayout::eShaderReadOnlyOptimal);
			return vk::WriteDescriptorSet{ dstSet, dstBinding, 0, 1, vk::DescriptorType::eCombinedImageSampler, &dsii.back(), nullptr, nullptr };
		};

		std::vector<vk::WriteDescriptorSet> textureWriteSets{
			wSetImage(DSBrightFilter.Value(), 0, frameImage),
			wSetImage(DSGaussianBlurHorizontal.Value(), 0, renderTargets["brightFilter"]),
			wSetImage(DSGaussianBlurVertical.Value(), 0, renderTargets["gaussianBlurHorizontal"]),
			wSetImage(DSCombine.Value(), 0, frameImage),
			wSetImage(DSCombine.Value(), 1, renderTargets["gaussianBlurVertical"])
		};
		VulkanContext::get()->device->updateDescriptorSets(textureWriteSets, nullptr);
	}

	void Bloom::draw(vk::CommandBuffer cmd, uint32_t imageIndex, std::map<std::string, Image>& renderTargets)
	{
		uint32_t totalImages = static_cast<uint32_t>(VulkanContext::get()->swapchain.images.size());

		vk::ClearValue clearColor;
		memcpy(clearColor.color.float32, GUI::clearColor.data(), 4 * sizeof(float));

		std::vector<vk::ClearValue> clearValues = { clearColor };

		std::vector<float> values{ GUI::Bloom_Inv_brightness, GUI::Bloom_intensity, GUI::Bloom_range, GUI::Bloom_exposure, static_cast<float>(GUI::use_tonemap) };

		vk::RenderPassBeginInfo rpi;
		rpi.renderPass = *renderPassBrightFilter;
		rpi.framebuffer = *framebuffers[imageIndex];
		rpi.renderArea.offset = vk::Offset2D{ 0, 0 };
		rpi.renderArea.extent = renderTargets["brightFilter"].extent.Value();
		rpi.clearValueCount = 1;
		rpi.pClearValues = clearValues.data();

		renderTargets["brightFilter"].changeLayout(cmd, LayoutState::ColorWrite);
		cmd.beginRenderPass(rpi, vk::SubpassContents::eInline);
		cmd.pushConstants<float>(pipelineBrightFilter.pipeinfo->layout, vk::ShaderStageFlagBits::eFragment, 0, values);
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipelineBrightFilter.pipeline);
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineBrightFilter.pipeinfo->layout, 0, DSBrightFilter.Value(), nullptr);
		cmd.draw(3, 1, 0, 0);
		cmd.endRenderPass();
		renderTargets["brightFilter"].changeLayout(cmd, LayoutState::ColorRead);

		rpi.renderPass = *renderPassGaussianBlur;
		rpi.framebuffer = *framebuffers[static_cast<size_t>(totalImages) + static_cast<size_t>(imageIndex)];

		renderTargets["gaussianBlurHorizontal"].changeLayout(cmd, LayoutState::ColorWrite);
		cmd.beginRenderPass(rpi, vk::SubpassContents::eInline);
		cmd.pushConstants<float>(pipelineGaussianBlurHorizontal.pipeinfo->layout, vk::ShaderStageFlagBits::eFragment, 0, values);
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipelineGaussianBlurHorizontal.pipeline);
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineBrightFilter.pipeinfo->layout, 0, DSGaussianBlurHorizontal.Value(), nullptr);
		cmd.draw(3, 1, 0, 0);
		cmd.endRenderPass();
		renderTargets["gaussianBlurHorizontal"].changeLayout(cmd, LayoutState::ColorRead);

		rpi.framebuffer = *framebuffers[static_cast<size_t>(totalImages) * 2 + static_cast<size_t>(imageIndex)];

		renderTargets["gaussianBlurVertical"].changeLayout(cmd, LayoutState::ColorWrite);
		cmd.beginRenderPass(rpi, vk::SubpassContents::eInline);
		cmd.pushConstants<float>(pipelineGaussianBlurVertical.pipeinfo->layout, vk::ShaderStageFlagBits::eFragment, 0, values);
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipelineGaussianBlurVertical.pipeline);
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineGaussianBlurVertical.pipeinfo->layout, 0, DSGaussianBlurVertical.Value(), nullptr);
		cmd.draw(3, 1, 0, 0);
		cmd.endRenderPass();
		renderTargets["gaussianBlurVertical"].changeLayout(cmd, LayoutState::ColorRead);

		rpi.renderPass = *renderPassCombine;
		rpi.framebuffer = *framebuffers[static_cast<size_t>(totalImages) * 3 + static_cast<size_t>(imageIndex)];

		cmd.beginRenderPass(rpi, vk::SubpassContents::eInline);
		cmd.pushConstants<float>(pipelineCombine.pipeinfo->layout, vk::ShaderStageFlagBits::eFragment, 0, values);
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipelineCombine.pipeline);
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineCombine.pipeinfo->layout, 0, DSCombine.Value(), nullptr);
		cmd.draw(3, 1, 0, 0);
		cmd.endRenderPass();
	}

	void Bloom::createBrightFilterPipeline(std::map<std::string, Image>& renderTargets)
	{
		// Shader stages
		Shader vert{ "shaders/Common/quad.vert", ShaderType::Vertex, true };
		Shader frag{ "shaders/Bloom/brightFilter.frag", ShaderType::Fragment, true };

		vk::ShaderModuleCreateInfo vsmci;
		vsmci.codeSize = vert.byte_size();
		vsmci.pCode = vert.get_spriv();
		vk::ShaderModule vertModule = VulkanContext::get()->device->createShaderModule(vsmci);

		vk::ShaderModuleCreateInfo fsmci;
		fsmci.codeSize = frag.byte_size();
		fsmci.pCode = frag.get_spriv();
		vk::ShaderModule fragModule = VulkanContext::get()->device->createShaderModule(fsmci);

		vk::PipelineShaderStageCreateInfo pssci1;
		pssci1.stage = vk::ShaderStageFlagBits::eVertex;
		pssci1.module = vertModule;
		pssci1.pName = "main";

		vk::PipelineShaderStageCreateInfo pssci2;
		pssci2.stage = vk::ShaderStageFlagBits::eFragment;
		pssci2.module = fragModule;
		pssci2.pName = "main";

		std::vector<vk::PipelineShaderStageCreateInfo> stages{ pssci1, pssci2 };
		pipelineBrightFilter.pipeinfo->stageCount = static_cast<uint32_t>(stages.size());
		pipelineBrightFilter.pipeinfo->pStages = stages.data();

		// Vertex Input state
		vk::PipelineVertexInputStateCreateInfo pvisci;
		pipelineBrightFilter.pipeinfo->pVertexInputState = &pvisci;

		// Input Assembly stage
		vk::PipelineInputAssemblyStateCreateInfo piasci;
		piasci.topology = vk::PrimitiveTopology::eTriangleList;
		piasci.primitiveRestartEnable = VK_FALSE;
		pipelineBrightFilter.pipeinfo->pInputAssemblyState = &piasci;

		// Viewports and Scissors
		vk::Viewport vp;
		vp.x = 0.0f;
		vp.y = 0.0f;
		vp.width = renderTargets["brightFilter"].width_f.Value();
		vp.height = renderTargets["brightFilter"].height_f.Value();
		vp.minDepth = 0.0f;
		vp.maxDepth = 1.0f;

		vk::Rect2D r2d;
		r2d.extent = vk::Extent2D{ static_cast<uint32_t>(vp.width), static_cast<uint32_t>(vp.height) };

		vk::PipelineViewportStateCreateInfo pvsci;
		pvsci.viewportCount = 1;
		pvsci.pViewports = &vp;
		pvsci.scissorCount = 1;
		pvsci.pScissors = &r2d;
		pipelineBrightFilter.pipeinfo->pViewportState = &pvsci;

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
		pipelineBrightFilter.pipeinfo->pRasterizationState = &prsci;

		// Multisample state
		vk::PipelineMultisampleStateCreateInfo pmsci;
		pmsci.rasterizationSamples = vk::SampleCountFlagBits::e1;
		pmsci.sampleShadingEnable = VK_FALSE;
		pmsci.minSampleShading = 1.0f;
		pmsci.pSampleMask = nullptr;
		pmsci.alphaToCoverageEnable = VK_FALSE;
		pmsci.alphaToOneEnable = VK_FALSE;
		pipelineBrightFilter.pipeinfo->pMultisampleState = &pmsci;

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
		pipelineBrightFilter.pipeinfo->pDepthStencilState = &pdssci;

		// Color Blending state
		std::vector<vk::PipelineColorBlendAttachmentState> colorBlendAttachments = {
			renderTargets["brightFilter"].blentAttachment.Value()
		};
		vk::PipelineColorBlendStateCreateInfo pcbsci;
		pcbsci.logicOpEnable = VK_FALSE;
		pcbsci.logicOp = vk::LogicOp::eCopy;
		pcbsci.attachmentCount = static_cast<uint32_t>(colorBlendAttachments.size());
		pcbsci.pAttachments = colorBlendAttachments.data();
		float blendConstants[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		memcpy(pcbsci.blendConstants, blendConstants, 4 * sizeof(float));
		pipelineBrightFilter.pipeinfo->pColorBlendState = &pcbsci;

		// Dynamic state
		pipelineBrightFilter.pipeinfo->pDynamicState = nullptr;

		// Pipeline Layout
		if (!DSLayoutBrightFilter || !DSLayoutBrightFilter.Value())
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
			DSLayoutBrightFilter = VulkanContext::get()->device->createDescriptorSetLayout(descriptorLayout);
		}

		std::vector<vk::DescriptorSetLayout> descriptorSetLayouts = { DSLayoutBrightFilter.Value() };

		vk::PushConstantRange pConstants;
		pConstants.stageFlags = vk::ShaderStageFlagBits::eFragment;
		pConstants.offset = 0;
		pConstants.size = 5 * sizeof(vec4);

		vk::PipelineLayoutCreateInfo plci;
		plci.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
		plci.pSetLayouts = descriptorSetLayouts.data();
		plci.pushConstantRangeCount = 1;
		plci.pPushConstantRanges = &pConstants;
		pipelineBrightFilter.pipeinfo->layout = VulkanContext::get()->device->createPipelineLayout(plci);

		// Render Pass
		pipelineBrightFilter.pipeinfo->renderPass = *renderPassBrightFilter;

		// Subpass (Index of subpass this pipeline will be used in)
		pipelineBrightFilter.pipeinfo->subpass = 0;

		// Base Pipeline Handle
		pipelineBrightFilter.pipeinfo->basePipelineHandle = nullptr;

		// Base Pipeline Index
		pipelineBrightFilter.pipeinfo->basePipelineIndex = -1;

		pipelineBrightFilter.pipeline = VulkanContext::get()->device->createGraphicsPipelines(nullptr, *pipelineBrightFilter.pipeinfo).at(0);

		// destroy Shader Modules
		VulkanContext::get()->device->destroyShaderModule(vertModule);
		VulkanContext::get()->device->destroyShaderModule(fragModule);
	}

	void Bloom::createGaussianBlurHorizontaPipeline(std::map<std::string, Image>& renderTargets)
	{
		// Shader stages
		Shader vert{ "shaders/Common/quad.vert", ShaderType::Vertex, true };
		Shader frag{ "shaders/Bloom/gaussianBlurHorizontal.frag", ShaderType::Fragment, true };

		vk::ShaderModuleCreateInfo vsmci;
		vsmci.codeSize = vert.byte_size();
		vsmci.pCode = vert.get_spriv();
		vk::ShaderModule vertModule = VulkanContext::get()->device->createShaderModule(vsmci);

		vk::ShaderModuleCreateInfo fsmci;
		fsmci.codeSize = frag.byte_size();
		fsmci.pCode = frag.get_spriv();
		vk::ShaderModule fragModule = VulkanContext::get()->device->createShaderModule(fsmci);

		vk::PipelineShaderStageCreateInfo pssci1;
		pssci1.stage = vk::ShaderStageFlagBits::eVertex;
		pssci1.module = vertModule;
		pssci1.pName = "main";

		vk::PipelineShaderStageCreateInfo pssci2;
		pssci2.stage = vk::ShaderStageFlagBits::eFragment;
		pssci2.module = fragModule;
		pssci2.pName = "main";

		std::vector<vk::PipelineShaderStageCreateInfo> stages{ pssci1, pssci2 };
		pipelineGaussianBlurHorizontal.pipeinfo->stageCount = static_cast<uint32_t>(stages.size());
		pipelineGaussianBlurHorizontal.pipeinfo->pStages = stages.data();

		// Vertex Input state
		vk::PipelineVertexInputStateCreateInfo pvisci;
		pipelineGaussianBlurHorizontal.pipeinfo->pVertexInputState = &pvisci;

		// Input Assembly stage
		vk::PipelineInputAssemblyStateCreateInfo piasci;
		piasci.topology = vk::PrimitiveTopology::eTriangleList;
		piasci.primitiveRestartEnable = VK_FALSE;
		pipelineGaussianBlurHorizontal.pipeinfo->pInputAssemblyState = &piasci;

		// Viewports and Scissors
		vk::Viewport vp;
		vp.x = 0.0f;
		vp.y = 0.0f;
		vp.width = renderTargets["gaussianBlurHorizontal"].width_f.Value();
		vp.height = renderTargets["gaussianBlurHorizontal"].height_f.Value();
		vp.minDepth = 0.0f;
		vp.maxDepth = 1.0f;

		vk::Rect2D r2d;
		r2d.extent = vk::Extent2D{ static_cast<uint32_t>(vp.width), static_cast<uint32_t>(vp.height) };

		vk::PipelineViewportStateCreateInfo pvsci;
		pvsci.viewportCount = 1;
		pvsci.pViewports = &vp;
		pvsci.scissorCount = 1;
		pvsci.pScissors = &r2d;
		pipelineGaussianBlurHorizontal.pipeinfo->pViewportState = &pvsci;

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
		pipelineGaussianBlurHorizontal.pipeinfo->pRasterizationState = &prsci;

		// Multisample state
		vk::PipelineMultisampleStateCreateInfo pmsci;
		pmsci.rasterizationSamples = vk::SampleCountFlagBits::e1;
		pmsci.sampleShadingEnable = VK_FALSE;
		pmsci.minSampleShading = 1.0f;
		pmsci.pSampleMask = nullptr;
		pmsci.alphaToCoverageEnable = VK_FALSE;
		pmsci.alphaToOneEnable = VK_FALSE;
		pipelineGaussianBlurHorizontal.pipeinfo->pMultisampleState = &pmsci;

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
		pipelineGaussianBlurHorizontal.pipeinfo->pDepthStencilState = &pdssci;

		// Color Blending state
		std::vector<vk::PipelineColorBlendAttachmentState> colorBlendAttachments = {
			renderTargets["gaussianBlurHorizontal"].blentAttachment.Value()
		};
		vk::PipelineColorBlendStateCreateInfo pcbsci;
		pcbsci.logicOpEnable = VK_FALSE;
		pcbsci.logicOp = vk::LogicOp::eCopy;
		pcbsci.attachmentCount = static_cast<uint32_t>(colorBlendAttachments.size());
		pcbsci.pAttachments = colorBlendAttachments.data();
		float blendConstants[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		memcpy(pcbsci.blendConstants, blendConstants, 4 * sizeof(float));
		pipelineGaussianBlurHorizontal.pipeinfo->pColorBlendState = &pcbsci;

		// Dynamic state
		pipelineGaussianBlurHorizontal.pipeinfo->pDynamicState = nullptr;

		// Pipeline Layout
		if (!DSLayoutGaussianBlurHorizontal || !DSLayoutGaussianBlurHorizontal.Value())
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
			DSLayoutGaussianBlurHorizontal = VulkanContext::get()->device->createDescriptorSetLayout(descriptorLayout);
		}

		std::vector<vk::DescriptorSetLayout> descriptorSetLayouts = { DSLayoutGaussianBlurHorizontal.Value() };

		vk::PushConstantRange pConstants;
		pConstants.stageFlags = vk::ShaderStageFlagBits::eFragment;
		pConstants.offset = 0;
		pConstants.size = 5 * sizeof(vec4);

		vk::PipelineLayoutCreateInfo plci;
		plci.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
		plci.pSetLayouts = descriptorSetLayouts.data();
		plci.pushConstantRangeCount = 1;
		plci.pPushConstantRanges = &pConstants;
		pipelineGaussianBlurHorizontal.pipeinfo->layout = VulkanContext::get()->device->createPipelineLayout(plci);

		// Render Pass
		pipelineGaussianBlurHorizontal.pipeinfo->renderPass = *renderPassGaussianBlur;

		// Subpass (Index of subpass this pipeline will be used in)
		pipelineGaussianBlurHorizontal.pipeinfo->subpass = 0;

		// Base Pipeline Handle
		pipelineGaussianBlurHorizontal.pipeinfo->basePipelineHandle = nullptr;

		// Base Pipeline Index
		pipelineGaussianBlurHorizontal.pipeinfo->basePipelineIndex = -1;

		pipelineGaussianBlurHorizontal.pipeline = VulkanContext::get()->device->createGraphicsPipelines(nullptr, *pipelineGaussianBlurHorizontal.pipeinfo).at(0);

		// destroy Shader Modules
		VulkanContext::get()->device->destroyShaderModule(vertModule);
		VulkanContext::get()->device->destroyShaderModule(fragModule);
	}

	void Bloom::createGaussianBlurVerticalPipeline(std::map<std::string, Image>& renderTargets)
	{
		// Shader stages
		Shader vert{ "shaders/Common/quad.vert", ShaderType::Vertex, true };
		Shader frag{ "shaders/Bloom/gaussianBlurVertical.frag", ShaderType::Fragment, true };

		vk::ShaderModuleCreateInfo vsmci;
		vsmci.codeSize = vert.byte_size();
		vsmci.pCode = vert.get_spriv();
		vk::ShaderModule vertModule = VulkanContext::get()->device->createShaderModule(vsmci);

		vk::ShaderModuleCreateInfo fsmci;
		fsmci.codeSize = frag.byte_size();
		fsmci.pCode = frag.get_spriv();
		vk::ShaderModule fragModule = VulkanContext::get()->device->createShaderModule(fsmci);

		vk::PipelineShaderStageCreateInfo pssci1;
		pssci1.stage = vk::ShaderStageFlagBits::eVertex;
		pssci1.module = vertModule;
		pssci1.pName = "main";

		vk::PipelineShaderStageCreateInfo pssci2;
		pssci2.stage = vk::ShaderStageFlagBits::eFragment;
		pssci2.module = fragModule;
		pssci2.pName = "main";

		std::vector<vk::PipelineShaderStageCreateInfo> stages{ pssci1, pssci2 };
		pipelineGaussianBlurVertical.pipeinfo->stageCount = static_cast<uint32_t>(stages.size());
		pipelineGaussianBlurVertical.pipeinfo->pStages = stages.data();

		// Vertex Input state
		vk::PipelineVertexInputStateCreateInfo pvisci;
		pipelineGaussianBlurVertical.pipeinfo->pVertexInputState = &pvisci;

		// Input Assembly stage
		vk::PipelineInputAssemblyStateCreateInfo piasci;
		piasci.topology = vk::PrimitiveTopology::eTriangleList;
		piasci.primitiveRestartEnable = VK_FALSE;
		pipelineGaussianBlurVertical.pipeinfo->pInputAssemblyState = &piasci;

		// Viewports and Scissors
		vk::Viewport vp;
		vp.x = 0.0f;
		vp.y = 0.0f;
		vp.width = renderTargets["gaussianBlurVertical"].width_f.Value();
		vp.height = renderTargets["gaussianBlurVertical"].height_f.Value();
		vp.minDepth = 0.0f;
		vp.maxDepth = 1.0f;

		vk::Rect2D r2d;
		r2d.extent = vk::Extent2D{ static_cast<uint32_t>(vp.width), static_cast<uint32_t>(vp.height) };

		vk::PipelineViewportStateCreateInfo pvsci;
		pvsci.viewportCount = 1;
		pvsci.pViewports = &vp;
		pvsci.scissorCount = 1;
		pvsci.pScissors = &r2d;
		pipelineGaussianBlurVertical.pipeinfo->pViewportState = &pvsci;

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
		pipelineGaussianBlurVertical.pipeinfo->pRasterizationState = &prsci;

		// Multisample state
		vk::PipelineMultisampleStateCreateInfo pmsci;
		pmsci.rasterizationSamples = vk::SampleCountFlagBits::e1;
		pmsci.sampleShadingEnable = VK_FALSE;
		pmsci.minSampleShading = 1.0f;
		pmsci.pSampleMask = nullptr;
		pmsci.alphaToCoverageEnable = VK_FALSE;
		pmsci.alphaToOneEnable = VK_FALSE;
		pipelineGaussianBlurVertical.pipeinfo->pMultisampleState = &pmsci;

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
		pipelineGaussianBlurVertical.pipeinfo->pDepthStencilState = &pdssci;

		// Color Blending state
		std::vector<vk::PipelineColorBlendAttachmentState> colorBlendAttachments = {
			renderTargets["gaussianBlurVertical"].blentAttachment.Value()
		};
		vk::PipelineColorBlendStateCreateInfo pcbsci;
		pcbsci.logicOpEnable = VK_FALSE;
		pcbsci.logicOp = vk::LogicOp::eCopy;
		pcbsci.attachmentCount = static_cast<uint32_t>(colorBlendAttachments.size());
		pcbsci.pAttachments = colorBlendAttachments.data();
		float blendConstants[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		memcpy(pcbsci.blendConstants, blendConstants, 4 * sizeof(float));
		pipelineGaussianBlurVertical.pipeinfo->pColorBlendState = &pcbsci;

		// Dynamic state
		pipelineGaussianBlurVertical.pipeinfo->pDynamicState = nullptr;

		// Pipeline Layout
		if (!DSLayoutGaussianBlurVertical || !DSLayoutGaussianBlurVertical.Value())
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
			DSLayoutGaussianBlurVertical = VulkanContext::get()->device->createDescriptorSetLayout(descriptorLayout);
		}

		std::vector<vk::DescriptorSetLayout> descriptorSetLayouts = { DSLayoutGaussianBlurVertical.Value() };
		vk::PushConstantRange pConstants;
		pConstants.stageFlags = vk::ShaderStageFlagBits::eFragment;
		pConstants.offset = 0;
		pConstants.size = 5 * sizeof(vec4);

		vk::PipelineLayoutCreateInfo plci;
		plci.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
		plci.pSetLayouts = descriptorSetLayouts.data();
		plci.pushConstantRangeCount = 1;
		plci.pPushConstantRanges = &pConstants;
		pipelineGaussianBlurVertical.pipeinfo->layout = VulkanContext::get()->device->createPipelineLayout(plci);

		// Render Pass
		pipelineGaussianBlurVertical.pipeinfo->renderPass = *renderPassGaussianBlur;

		// Subpass (Index of subpass this pipeline will be used in)
		pipelineGaussianBlurVertical.pipeinfo->subpass = 0;

		// Base Pipeline Handle
		pipelineGaussianBlurVertical.pipeinfo->basePipelineHandle = nullptr;

		// Base Pipeline Index
		pipelineGaussianBlurVertical.pipeinfo->basePipelineIndex = -1;

		pipelineGaussianBlurVertical.pipeline = VulkanContext::get()->device->createGraphicsPipelines(nullptr, *pipelineGaussianBlurVertical.pipeinfo).at(0);

		// destroy Shader Modules
		VulkanContext::get()->device->destroyShaderModule(vertModule);
		VulkanContext::get()->device->destroyShaderModule(fragModule);
	}

	void Bloom::createCombinePipeline(std::map<std::string, Image>& renderTargets)
	{
		// Shader stages
		Shader vert{ "shaders/Common/quad.vert", ShaderType::Vertex, true };
		Shader frag{ "shaders/Bloom/combine.frag", ShaderType::Fragment, true };

		vk::ShaderModuleCreateInfo vsmci;
		vsmci.codeSize = vert.byte_size();
		vsmci.pCode = vert.get_spriv();
		vk::ShaderModule vertModule = VulkanContext::get()->device->createShaderModule(vsmci);

		vk::ShaderModuleCreateInfo fsmci;
		fsmci.codeSize = frag.byte_size();
		fsmci.pCode = frag.get_spriv();
		vk::ShaderModule fragModule = VulkanContext::get()->device->createShaderModule(fsmci);

		vk::PipelineShaderStageCreateInfo pssci1;
		pssci1.stage = vk::ShaderStageFlagBits::eVertex;
		pssci1.module = vertModule;
		pssci1.pName = "main";

		vk::PipelineShaderStageCreateInfo pssci2;
		pssci2.stage = vk::ShaderStageFlagBits::eFragment;
		pssci2.module = fragModule;
		pssci2.pName = "main";

		std::vector<vk::PipelineShaderStageCreateInfo> stages{ pssci1, pssci2 };
		pipelineCombine.pipeinfo->stageCount = static_cast<uint32_t>(stages.size());
		pipelineCombine.pipeinfo->pStages = stages.data();

		// Vertex Input state
		vk::PipelineVertexInputStateCreateInfo pvisci;
		pipelineCombine.pipeinfo->pVertexInputState = &pvisci;

		// Input Assembly stage
		vk::PipelineInputAssemblyStateCreateInfo piasci;
		piasci.topology = vk::PrimitiveTopology::eTriangleList;
		piasci.primitiveRestartEnable = VK_FALSE;
		pipelineCombine.pipeinfo->pInputAssemblyState = &piasci;

		// Viewports and Scissors
		vk::Viewport vp;
		vp.x = 0.0f;
		vp.y = 0.0f;
		vp.width = renderTargets["viewport"].width_f.Value();
		vp.height = renderTargets["viewport"].height_f.Value();
		vp.minDepth = 0.0f;
		vp.maxDepth = 1.0f;

		vk::Rect2D r2d;
		r2d.extent = vk::Extent2D{ static_cast<uint32_t>(vp.width), static_cast<uint32_t>(vp.height) };

		vk::PipelineViewportStateCreateInfo pvsci;
		pvsci.viewportCount = 1;
		pvsci.pViewports = &vp;
		pvsci.scissorCount = 1;
		pvsci.pScissors = &r2d;
		pipelineCombine.pipeinfo->pViewportState = &pvsci;

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
		pipelineCombine.pipeinfo->pRasterizationState = &prsci;

		// Multisample state
		vk::PipelineMultisampleStateCreateInfo pmsci;
		pmsci.rasterizationSamples = vk::SampleCountFlagBits::e1;
		pmsci.sampleShadingEnable = VK_FALSE;
		pmsci.minSampleShading = 1.0f;
		pmsci.pSampleMask = nullptr;
		pmsci.alphaToCoverageEnable = VK_FALSE;
		pmsci.alphaToOneEnable = VK_FALSE;
		pipelineCombine.pipeinfo->pMultisampleState = &pmsci;

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
		pipelineCombine.pipeinfo->pDepthStencilState = &pdssci;

		// Color Blending state
		std::vector<vk::PipelineColorBlendAttachmentState> colorBlendAttachments = {
			renderTargets["viewport"].blentAttachment.Value()
		};
		vk::PipelineColorBlendStateCreateInfo pcbsci;
		pcbsci.logicOpEnable = VK_FALSE;
		pcbsci.logicOp = vk::LogicOp::eCopy;
		pcbsci.attachmentCount = static_cast<uint32_t>(colorBlendAttachments.size());
		pcbsci.pAttachments = colorBlendAttachments.data();
		float blendConstants[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		memcpy(pcbsci.blendConstants, blendConstants, 4 * sizeof(float));
		pipelineCombine.pipeinfo->pColorBlendState = &pcbsci;

		// Dynamic state
		pipelineCombine.pipeinfo->pDynamicState = nullptr;

		// Pipeline Layout
		if (!DSLayoutCombine || !DSLayoutCombine.Value())
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
			DSLayoutCombine = VulkanContext::get()->device->createDescriptorSetLayout(descriptorLayout);
		}

		std::vector<vk::DescriptorSetLayout> descriptorSetLayouts = { DSLayoutCombine.Value() };

		vk::PushConstantRange pConstants;
		pConstants.stageFlags = vk::ShaderStageFlagBits::eFragment;
		pConstants.offset = 0;
		pConstants.size = 5 * sizeof(vec4);

		vk::PipelineLayoutCreateInfo plci;
		plci.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
		plci.pSetLayouts = descriptorSetLayouts.data();
		plci.pushConstantRangeCount = 1;
		plci.pPushConstantRanges = &pConstants;
		pipelineCombine.pipeinfo->layout = VulkanContext::get()->device->createPipelineLayout(plci);

		// Render Pass
		pipelineCombine.pipeinfo->renderPass = *renderPassCombine;

		// Subpass (Index of subpass this pipeline will be used in)
		pipelineCombine.pipeinfo->subpass = 0;

		// Base Pipeline Handle
		pipelineCombine.pipeinfo->basePipelineHandle = nullptr;

		// Base Pipeline Index
		pipelineCombine.pipeinfo->basePipelineIndex = -1;

		pipelineCombine.pipeline = VulkanContext::get()->device->createGraphicsPipelines(nullptr, *pipelineCombine.pipeinfo).at(0);

		// destroy Shader Modules
		VulkanContext::get()->device->destroyShaderModule(vertModule);
		VulkanContext::get()->device->destroyShaderModule(fragModule);
	}

	void Bloom::destroy()
	{
		auto vulkan = VulkanContext::get();
		for (auto& frameBuffer : framebuffers)
			frameBuffer.Destroy();

		renderPassBrightFilter.Destroy();
		renderPassGaussianBlur.Destroy();
		renderPassCombine.Destroy();

		if (DSLayoutBrightFilter.Value()) {
			vulkan->device->destroyDescriptorSetLayout(DSLayoutBrightFilter.Value());
			*DSLayoutBrightFilter = nullptr;
		}
		if (DSLayoutGaussianBlurHorizontal.Value()) {
			vulkan->device->destroyDescriptorSetLayout(DSLayoutGaussianBlurHorizontal.Value());
			*DSLayoutGaussianBlurHorizontal = nullptr;
		}
		if (DSLayoutGaussianBlurVertical.Value()) {
			vulkan->device->destroyDescriptorSetLayout(DSLayoutGaussianBlurVertical.Value());
			*DSLayoutGaussianBlurVertical = nullptr;
		}
		if (DSLayoutCombine.Value()) {
			vulkan->device->destroyDescriptorSetLayout(DSLayoutCombine.Value());
			*DSLayoutCombine = nullptr;
		}
		frameImage.destroy();
		pipelineBrightFilter.destroy();
		pipelineGaussianBlurHorizontal.destroy();
		pipelineGaussianBlurVertical.destroy();
		pipelineCombine.destroy();
	}
}
