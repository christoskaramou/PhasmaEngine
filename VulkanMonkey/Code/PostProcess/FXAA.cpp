#include "FXAA.h"
#include "../GUI/GUI.h"
#include "../Swapchain/Swapchain.h"
#include "../Core/Surface.h"
#include"../Shader/Shader.h"
#include "../VulkanContext/VulkanContext.h"
#include <vulkan/vulkan.hpp>

namespace vm
{
	FXAA::FXAA()
	{
		DSet = vk::DescriptorSet();
		DSLayout = vk::DescriptorSetLayout();
	}

	FXAA::~FXAA()
	{
	}

	void FXAA::Init()
	{
		frameImage.format = VulkanContext::get()->surface.formatKHR->format;
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

	void FXAA::createUniforms(std::map<std::string, Image>& renderTargets)
	{
		vk::DescriptorSetAllocateInfo allocateInfo2;
		allocateInfo2.descriptorPool = VulkanContext::get()->descriptorPool.Value();
		allocateInfo2.descriptorSetCount = 1;
		allocateInfo2.pSetLayouts = &*DSLayout;
		DSet = VulkanContext::get()->device->allocateDescriptorSets(allocateInfo2).at(0);

		updateDescriptorSets(renderTargets);
	}

	void FXAA::updateDescriptorSets(std::map<std::string, Image>& renderTargets) const
	{
		// Composition sampler
		vk::DescriptorImageInfo dii;
		dii.sampler = frameImage.sampler.Value();
		dii.imageView = frameImage.view.Value();
		dii.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

		vk::WriteDescriptorSet textureWriteSet;
		textureWriteSet.dstSet = DSet.Value();
		textureWriteSet.dstBinding = 0;
		textureWriteSet.dstArrayElement = 0;
		textureWriteSet.descriptorCount = 1;
		textureWriteSet.descriptorType = vk::DescriptorType::eCombinedImageSampler;
		textureWriteSet.pImageInfo = &dii;

		VulkanContext::get()->device->updateDescriptorSets(textureWriteSet, nullptr);
	}

	void FXAA::draw(vk::CommandBuffer cmd, uint32_t imageIndex, const vk::Extent2D& extent)
	{
		vk::ClearValue clearColor;
		memcpy(clearColor.color.float32, GUI::clearColor.data(), 4 * sizeof(float));

		std::vector<vk::ClearValue> clearValues = { clearColor };

		vk::RenderPassBeginInfo rpi;
		rpi.renderPass = *renderPass;
		rpi.framebuffer = *framebuffers[imageIndex];
		rpi.renderArea.offset = vk::Offset2D{ 0, 0 };
		rpi.renderArea.extent = extent;
		rpi.clearValueCount = 1;
		rpi.pClearValues = clearValues.data();

		cmd.beginRenderPass(rpi, vk::SubpassContents::eInline);
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline.pipeline);
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline.pipeinfo->layout, 0, DSet.Value(), nullptr);
		cmd.draw(3, 1, 0, 0);
		cmd.endRenderPass();
	}

	void vm::FXAA::createRenderPass(std::map<std::string, Image>& renderTargets)
	{
		renderPass.Create(renderTargets["viewport"].format.Value(), vk::Format::eUndefined);
	}

	void vm::FXAA::createFrameBuffers(std::map<std::string, Image>& renderTargets)
	{
		auto vulkan = VulkanContext::get();
		framebuffers.resize(vulkan->swapchain.images.size());
		for (size_t i = 0; i < vulkan->swapchain.images.size(); ++i)
		{
			uint32_t width = renderTargets["viewport"].width.Value();
			uint32_t height = renderTargets["viewport"].height.Value();
			vk::ImageView view = renderTargets["viewport"].view.Value();
			framebuffers[i].Create(width, height, view, renderPass);
		}
	}

	void FXAA::createPipeline(std::map<std::string, Image>& renderTargets)
	{
		// Shader stages
		Shader vert{ "shaders/Common/quad.vert", ShaderType::Vertex, true };
		Shader frag{ "shaders/FXAA/FXAA.frag", ShaderType::Fragment, true };

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
			renderTargets["viewport"].blentAttachment.Value()
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
		if (!DSLayout || !DSLayout.Value())
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
			DSLayout = VulkanContext::get()->device->createDescriptorSetLayout(descriptorLayout);
		}

		std::vector<vk::DescriptorSetLayout> descriptorSetLayouts = { DSLayout.Value() };

		vk::PipelineLayoutCreateInfo plci;
		plci.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
		plci.pSetLayouts = descriptorSetLayouts.data();
		pipeline.pipeinfo->layout = VulkanContext::get()->device->createPipelineLayout(plci);

		// Render Pass
		pipeline.pipeinfo->renderPass = *renderPass;

		// Subpass (Index of subpass this pipeline will be used in)
		pipeline.pipeinfo->subpass = 0;

		// Base Pipeline Handle
		pipeline.pipeinfo->basePipelineHandle = nullptr;

		// Base Pipeline Index
		pipeline.pipeinfo->basePipelineIndex = -1;

		pipeline.pipeline = VulkanContext::get()->device->createGraphicsPipelines(nullptr, *pipeline.pipeinfo).at(0);

		// destroy Shader Modules
		VulkanContext::get()->device->destroyShaderModule(vertModule);
		VulkanContext::get()->device->destroyShaderModule(fragModule);
	}

	void FXAA::copyFrameImage(const vk::CommandBuffer& cmd, Image& renderedImage) const
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

	void FXAA::destroy()
	{
		for (auto& frameBuffer : framebuffers)
			frameBuffer.Destroy();

		renderPass.Destroy();

		if (DSLayout.Value()) {
			VulkanContext::get()->device->destroyDescriptorSetLayout(DSLayout.Value());
			*DSLayout = nullptr;
		}
		frameImage.destroy();
		pipeline.destroy();
	}
}
