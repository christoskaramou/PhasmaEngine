#include "TAA.h"
#include "../GUI/GUI.h"
#include "../Surface/Surface.h"
#include "../Swapchain/Swapchain.h"
#include "../Shader/Shader.h"
#include "../Core/Queue.h"
#include <deque>

namespace vm
{
	void TAA::Init()
	{
		previous.format = VulkanContext::get()->surface->formatKHR.format;
		previous.initialLayout = vk::ImageLayout::eUndefined;
		previous.createImage(
			static_cast<uint32_t>(WIDTH_f * GUI::renderTargetsScale),
			static_cast<uint32_t>(HEIGHT_f * GUI::renderTargetsScale),
			vk::ImageTiling::eOptimal,
			vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled, vk::MemoryPropertyFlagBits::eDeviceLocal);
		previous.transitionImageLayout(vk::ImageLayout::eUndefined, vk::ImageLayout::eShaderReadOnlyOptimal);
		previous.createImageView(vk::ImageAspectFlagBits::eColor);
		previous.createSampler();

		frameImage.format = VulkanContext::get()->surface->formatKHR.format;
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
			ubo.sharpenValues = { GUI::TAA_sharp_strength, GUI::TAA_sharp_clamp, GUI::TAA_sharp_offset_bias , sin(static_cast<float>(ImGui::GetTime()) * 0.125f) };
			ubo.invProj = camera.invProjection;

			Queue::memcpyRequest(&uniform, { { &ubo, sizeof(ubo), 0 } });
			//uniform.map();
			//memcpy(uniform.data, &ubo, sizeof(ubo));
			//uniform.flush();
			//uniform.unmap();
		}
	}

	void TAA::createUniforms(std::map<std::string, Image>& renderTargets)
	{
		uniform.createBuffer(sizeof(UBO), vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostVisible);
		uniform.map();
		uniform.zero();
		uniform.flush();
		uniform.unmap();

		vk::DescriptorSetAllocateInfo allocateInfo2;
		allocateInfo2.descriptorPool = VulkanContext::get()->descriptorPool;
		allocateInfo2.descriptorSetCount = 1;
		allocateInfo2.pSetLayouts = &DSLayout;
		DSet = VulkanContext::get()->device.allocateDescriptorSets(allocateInfo2).at(0);

		allocateInfo2.pSetLayouts = &DSLayoutSharpen;
		DSetSharpen = VulkanContext::get()->device.allocateDescriptorSets(allocateInfo2).at(0);

		updateDescriptorSets(renderTargets);
	}

	void TAA::updateDescriptorSets(std::map<std::string, Image>& renderTargets)
	{
		std::deque<vk::DescriptorImageInfo> dsii{};
		const auto wSetImage = [&dsii](vk::DescriptorSet& dstSet, uint32_t dstBinding, Image& image) {
			dsii.emplace_back(image.sampler.Value(), image.view.Value(), vk::ImageLayout::eShaderReadOnlyOptimal);
			return vk::WriteDescriptorSet{ dstSet, dstBinding, 0, 1, vk::DescriptorType::eCombinedImageSampler, &dsii.back(), nullptr, nullptr };
		};
		std::deque<vk::DescriptorBufferInfo> dsbi{};
		const auto wSetBuffer = [&dsbi](vk::DescriptorSet& dstSet, uint32_t dstBinding, Buffer& buffer) {
			dsbi.emplace_back(buffer.buffer.Value(), 0, buffer.size.Value());
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

		VulkanContext::get()->device.updateDescriptorSets(writeDescriptorSets, nullptr);
	}

	void TAA::draw(vk::CommandBuffer cmd, uint32_t imageIndex, std::function<void(vk::CommandBuffer, Image&, LayoutState)>&& changeLayout, std::map<std::string, Image>& renderTargets)
	{
		vk::ClearValue clearColor;
		memcpy(clearColor.color.float32, GUI::clearColor.data(), 4 * sizeof(float));

		std::vector<vk::ClearValue> clearValues = { clearColor };

		// Main TAA pass
		vk::RenderPassBeginInfo rpi;
		rpi.renderPass = *renderPass;
		rpi.framebuffer = *framebuffers[imageIndex];
		rpi.renderArea.offset = vk::Offset2D{ 0, 0 };
		rpi.renderArea.extent = renderTargets["taa"].extent.Value();
		rpi.clearValueCount = 1;
		rpi.pClearValues = clearValues.data();

		changeLayout(cmd, renderTargets["taa"], LayoutState::ColorWrite);
		cmd.beginRenderPass(rpi, vk::SubpassContents::eInline);
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline.pipeline);
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline.pipeinfo->layout, 0, DSet, nullptr);
		cmd.draw(3, 1, 0, 0);
		cmd.endRenderPass();
		changeLayout(cmd, renderTargets["taa"], LayoutState::ColorRead);

		saveImage(cmd, renderTargets["taa"]);

		// TAA Sharpen pass
		vk::RenderPassBeginInfo rpi2;
		rpi2.renderPass = *renderPassSharpen;
		rpi2.framebuffer = *framebuffersSharpen[imageIndex];
		rpi2.renderArea.offset = vk::Offset2D{ 0, 0 };
		rpi2.renderArea.extent = renderTargets["viewport"].extent.Value();
		rpi2.clearValueCount = 1;
		rpi2.pClearValues = clearValues.data();

		cmd.beginRenderPass(rpi2, vk::SubpassContents::eInline);
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipelineSharpen.pipeline);
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineSharpen.pipeinfo->layout, 0, DSetSharpen, nullptr);
		cmd.draw(3, 1, 0, 0);
		cmd.endRenderPass();
	}

	void TAA::createRenderPasses(std::map<std::string, Image>& renderTargets)
	{
		renderPass.Create(renderTargets["taa"].format.Value(), vk::Format::eUndefined);
		renderPassSharpen.Create(renderTargets["viewport"].format.Value(), vk::Format::eUndefined);
	}

	void TAA::createFrameBuffers(std::map<std::string, Image>& renderTargets)
	{
		auto vulkan = VulkanContext::get();

		framebuffers.resize(vulkan->swapchain->images.size());
		for (size_t i = 0; i < vulkan->swapchain->images.size(); ++i)
		{
			uint32_t width = renderTargets["taa"].width.Value();
			uint32_t height = renderTargets["taa"].height.Value();
			vk::ImageView view = renderTargets["taa"].view.Value();
			framebuffers[i].Create(width, height, view, renderPass);
		}

		framebuffersSharpen.resize(vulkan->swapchain->images.size());
		for (size_t i = 0; i < vulkan->swapchain->images.size(); ++i)
		{
			uint32_t width = renderTargets["viewport"].width.Value();
			uint32_t height = renderTargets["viewport"].height.Value();
			vk::ImageView view = renderTargets["viewport"].view.Value();
			framebuffersSharpen[i].Create(width, height, view, renderPassSharpen);
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
		Shader vert{ "shaders/Common/quad.vert", ShaderType::Vertex, true };
		Shader frag{ "shaders/TAA/TAA.frag", ShaderType::Fragment, true };

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
		vp.width = renderTargets["taa"].width_f.Value();
		vp.height = renderTargets["taa"].height_f.Value();
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
			renderTargets["taa"].blentAttachment.Value()
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
			DSLayout = VulkanContext::get()->device.createDescriptorSetLayout(descriptorLayout);
		}

		std::vector<vk::DescriptorSetLayout> descriptorSetLayouts = { DSLayout };

		vk::PipelineLayoutCreateInfo plci;
		plci.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
		plci.pSetLayouts = descriptorSetLayouts.data();
		pipeline.pipeinfo->layout = VulkanContext::get()->device.createPipelineLayout(plci);

		// Render Pass
		pipeline.pipeinfo->renderPass = *renderPass;

		// Subpass (Index of subpass this pipeline will be used in)
		pipeline.pipeinfo->subpass = 0;

		// Base Pipeline Handle
		pipeline.pipeinfo->basePipelineHandle = nullptr;

		// Base Pipeline Index
		pipeline.pipeinfo->basePipelineIndex = -1;

		pipeline.pipeline = VulkanContext::get()->device.createGraphicsPipelines(nullptr, *pipeline.pipeinfo).at(0);

		// destroy Shader Modules
		VulkanContext::get()->device.destroyShaderModule(vertModule);
		VulkanContext::get()->device.destroyShaderModule(fragModule);
	}

	void vm::TAA::createPipelineSharpen(std::map<std::string, Image>& renderTargets)
	{
		// Shader stages
		Shader vert{ "shaders/Common/quad.vert", ShaderType::Vertex, true };
		Shader frag{ "shaders/TAA/TAASharpen.frag", ShaderType::Fragment, true };

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
		pipelineSharpen.pipeinfo->stageCount = static_cast<uint32_t>(stages.size());
		pipelineSharpen.pipeinfo->pStages = stages.data();

		// Vertex Input state
		vk::PipelineVertexInputStateCreateInfo pvisci;
		pipelineSharpen.pipeinfo->pVertexInputState = &pvisci;

		// Input Assembly stage
		vk::PipelineInputAssemblyStateCreateInfo piasci;
		piasci.topology = vk::PrimitiveTopology::eTriangleList;
		piasci.primitiveRestartEnable = VK_FALSE;
		pipelineSharpen.pipeinfo->pInputAssemblyState = &piasci;

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
		pipelineSharpen.pipeinfo->pViewportState = &pvsci;

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
		pipelineSharpen.pipeinfo->pRasterizationState = &prsci;

		// Multisample state
		vk::PipelineMultisampleStateCreateInfo pmsci;
		pmsci.rasterizationSamples = vk::SampleCountFlagBits::e1;
		pmsci.sampleShadingEnable = VK_FALSE;
		pmsci.minSampleShading = 1.0f;
		pmsci.pSampleMask = nullptr;
		pmsci.alphaToCoverageEnable = VK_FALSE;
		pmsci.alphaToOneEnable = VK_FALSE;
		pipelineSharpen.pipeinfo->pMultisampleState = &pmsci;

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
		pipelineSharpen.pipeinfo->pDepthStencilState = &pdssci;

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
		pipelineSharpen.pipeinfo->pColorBlendState = &pcbsci;

		// Dynamic state
		pipelineSharpen.pipeinfo->pDynamicState = nullptr;

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
			DSLayoutSharpen = VulkanContext::get()->device.createDescriptorSetLayout(descriptorLayout);
		}

		std::vector<vk::DescriptorSetLayout> descriptorSetLayouts = { DSLayoutSharpen };

		vk::PipelineLayoutCreateInfo plci;
		plci.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
		plci.pSetLayouts = descriptorSetLayouts.data();
		pipelineSharpen.pipeinfo->layout = VulkanContext::get()->device.createPipelineLayout(plci);

		// Render Pass
		pipelineSharpen.pipeinfo->renderPass = *renderPassSharpen;

		// Subpass (Index of subpass this pipeline will be used in)
		pipelineSharpen.pipeinfo->subpass = 0;

		// Base Pipeline Handle
		pipelineSharpen.pipeinfo->basePipelineHandle = nullptr;

		// Base Pipeline Index
		pipelineSharpen.pipeinfo->basePipelineIndex = -1;

		pipelineSharpen.pipeline = VulkanContext::get()->device.createGraphicsPipelines(nullptr, *pipelineSharpen.pipeinfo).at(0);

		// destroy Shader Modules
		VulkanContext::get()->device.destroyShaderModule(vertModule);
		VulkanContext::get()->device.destroyShaderModule(fragModule);
	}

	void TAA::copyFrameImage(const vk::CommandBuffer& cmd, Image& renderedImage) const
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
		region.extent.width = renderedImage.width.Value();
		region.extent.height = renderedImage.height.Value();
		region.extent.depth = 1;

		cmd.copyImage(
			renderedImage.image.Value(),
			vk::ImageLayout::eTransferSrcOptimal,
			frameImage.image.Value(),
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

	void TAA::saveImage(const vk::CommandBuffer& cmd, Image& source) const
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
			source.layoutState.Value() == LayoutState::ColorRead ? vk::ImageLayout::eShaderReadOnlyOptimal : vk::ImageLayout::eColorAttachmentOptimal,
			vk::ImageLayout::eTransferSrcOptimal,
			source.layoutState.Value() == LayoutState::ColorRead ? vk::PipelineStageFlagBits::eFragmentShader : vk::PipelineStageFlagBits::eColorAttachmentOutput,
			vk::PipelineStageFlagBits::eTransfer,
			source.layoutState.Value() == LayoutState::ColorRead ? vk::AccessFlagBits::eShaderRead : vk::AccessFlagBits::eColorAttachmentWrite,
			vk::AccessFlagBits::eTransferRead,
			vk::ImageAspectFlagBits::eColor);

		// copy the image
		vk::ImageCopy region;
		region.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
		region.srcSubresource.layerCount = 1;
		region.dstSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
		region.dstSubresource.layerCount = 1;
		region.extent.width = source.width.Value();
		region.extent.height = source.height.Value();
		region.extent.depth = 1;

		cmd.copyImage(
			source.image.Value(),
			vk::ImageLayout::eTransferSrcOptimal,
			previous.image.Value(),
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

		for (auto& framebuffer : framebuffers)
			framebuffer.Destroy();
		for (auto& framebuffer : framebuffersSharpen)
			framebuffer.Destroy();

		renderPass.Destroy();
		renderPassSharpen.Destroy();

		if (DSLayout) {
			VulkanContext::get()->device.destroyDescriptorSetLayout(DSLayout);
			DSLayout = nullptr;
		}
		if (DSLayoutSharpen) {
			VulkanContext::get()->device.destroyDescriptorSetLayout(DSLayoutSharpen);
			DSLayoutSharpen = nullptr;
		}
		pipeline.destroy();
		pipelineSharpen.destroy();
	}
}
