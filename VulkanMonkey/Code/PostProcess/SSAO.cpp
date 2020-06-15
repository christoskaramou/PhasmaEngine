#include "SSAO.h"
#include <deque>
#include "../Swapchain/Swapchain.h"
#include "../Surface/Surface.h"
#include "../GUI/GUI.h"
#include "../Shader/Shader.h"
#include "../Core/Queue.h"

namespace vm
{
	void SSAO::createUniforms(std::map<std::string, Image>& renderTargets)
	{
		// kernel buffer
		std::vector<vec4> kernel{};
		for (unsigned i = 0; i < 16; i++) {
			vec3 sample(rand(-1.f, 1.f), rand(-1.f, 1.f), rand(0.f, 1.f));
			sample = normalize(sample);
			sample *= rand(0.f, 1.f);
			float scale = float(i) / 16.f;
			scale = lerp(.1f, 1.f, scale * scale);
			kernel.emplace_back(sample * scale, 0.f);
		}
		UB_Kernel.createBuffer(sizeof(vec4) * 16, vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostVisible);
		UB_Kernel.map();
		UB_Kernel.copyData(kernel.data());
		UB_Kernel.flush();
		UB_Kernel.unmap();

		// noise image
		std::vector<vec4> noise{};
		for (unsigned int i = 0; i < 16; i++)
			noise.emplace_back(rand(-1.f, 1.f), rand(-1.f, 1.f), 0.f, 1.f);

		Buffer staging;
		const uint64_t bufSize = sizeof(vec4) * 16;
		staging.createBuffer(bufSize, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible);
		staging.map();
		staging.copyData(noise.data());
		staging.flush();
		staging.unmap();

		noiseTex.filter = vk::Filter::eNearest;
		noiseTex.minLod = 0.0f;
		noiseTex.maxLod = 0.0f;
		noiseTex.maxAnisotropy = 1.0f;
		noiseTex.format = vk::Format::eR16G16B16A16Sfloat;
		noiseTex.createImage(4, 4, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled, vk::MemoryPropertyFlagBits::eDeviceLocal);
		noiseTex.transitionImageLayout(vk::ImageLayout::ePreinitialized, vk::ImageLayout::eTransferDstOptimal);
		noiseTex.copyBufferToImage(staging.buffer.Value());
		noiseTex.transitionImageLayout(vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal);
		noiseTex.createImageView(vk::ImageAspectFlagBits::eColor);
		noiseTex.createSampler();
		staging.destroy();
		// pvm uniform
		UB_PVM.createBuffer(3 * sizeof(mat4), vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostVisible);
		UB_PVM.map();
		UB_PVM.zero();
		UB_PVM.flush();
		UB_PVM.unmap();

		// DESCRIPTOR SET FOR SSAO
		const vk::DescriptorSetAllocateInfo allocInfo = vk::DescriptorSetAllocateInfo{
			VulkanContext::get()->descriptorPool,					//DescriptorPool descriptorPool;
			1,										//uint32_t descriptorSetCount;
			&DSLayout								//const DescriptorSetLayout* pSetLayouts;
		};
		DSet = VulkanContext::get()->device.allocateDescriptorSets(allocInfo).at(0);

		// DESCRIPTOR SET FOR SSAO BLUR
		const vk::DescriptorSetAllocateInfo allocInfoBlur = vk::DescriptorSetAllocateInfo{
			VulkanContext::get()->descriptorPool,					//DescriptorPool descriptorPool;
			1,										//uint32_t descriptorSetCount;
			&DSLayoutBlur							//const DescriptorSetLayout* pSetLayouts;
		};
		DSBlur = VulkanContext::get()->device.allocateDescriptorSets(allocInfoBlur).at(0);

		updateDescriptorSets(renderTargets);
	}

	void SSAO::updateDescriptorSets(std::map<std::string, Image>& renderTargets)
	{
		std::deque<vk::DescriptorImageInfo> dsii{};
		const auto wSetImage = [&dsii](vk::DescriptorSet& dstSet, uint32_t dstBinding, Image& image, vk::ImageLayout imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal) {
			dsii.emplace_back(image.sampler.Value(), image.view.Value(), imageLayout);
			return vk::WriteDescriptorSet{ dstSet, dstBinding, 0, 1, vk::DescriptorType::eCombinedImageSampler, &dsii.back(), nullptr, nullptr };
		};
		std::deque<vk::DescriptorBufferInfo> dsbi{};
		const auto wSetBuffer = [&dsbi](vk::DescriptorSet& dstSet, uint32_t dstBinding, Buffer& buffer) {
			dsbi.emplace_back(buffer.buffer.Value(), 0, buffer.size.Value());
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
		VulkanContext::get()->device.updateDescriptorSets(writeDescriptorSets, nullptr);
	}

	void SSAO::draw(vk::CommandBuffer cmd, uint32_t imageIndex, std::function<void(vk::CommandBuffer, Image&, LayoutState)>&& changeLayout, Image& image)
	{
		// SSAO image
		vk::ClearValue clearColor;
		memcpy(clearColor.color.float32, GUI::clearColor.data(), 4 * sizeof(float));


		std::vector<vk::ClearValue> clearValues = { clearColor };

		vk::RenderPassBeginInfo rpi;
		rpi.renderPass = *renderPass;
		rpi.framebuffer = *framebuffers[imageIndex];
		rpi.renderArea.offset = vk::Offset2D{ 0, 0 };
		rpi.renderArea.extent = image.extent.Value();
		rpi.clearValueCount = 1;
		rpi.pClearValues = clearValues.data();

		changeLayout(cmd, image, LayoutState::ColorWrite);
		cmd.beginRenderPass(rpi, vk::SubpassContents::eInline);
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline.pipeline);
		const vk::DescriptorSet descriptorSets = { DSet };
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline.pipeinfo->layout, 0, descriptorSets, nullptr);
		cmd.draw(3, 1, 0, 0);
		cmd.endRenderPass();
		changeLayout(cmd, image, LayoutState::ColorRead);

		// new blurry SSAO image
		rpi.renderPass = *blurRenderPass;
		rpi.framebuffer = *blurFramebuffers[imageIndex];

		cmd.beginRenderPass(rpi, vk::SubpassContents::eInline);
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipelineBlur.pipeline);
		const vk::DescriptorSet descriptorSetsBlur = { DSBlur };
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineBlur.pipeinfo->layout, 0, descriptorSetsBlur, nullptr);
		cmd.draw(3, 1, 0, 0);
		cmd.endRenderPass();
	}

	void SSAO::destroy()
	{
		UB_Kernel.destroy();
		UB_PVM.destroy();
		noiseTex.destroy();

		renderPass.Destroy();
		blurRenderPass.Destroy();

		for (auto& frameBuffer : framebuffers)
			frameBuffer.Destroy();
		for (auto& frameBuffer : blurFramebuffers)
			frameBuffer.Destroy();

		pipeline.destroy();
		pipelineBlur.destroy();
		if (DSLayout) {
			VulkanContext::get()->device.destroyDescriptorSetLayout(DSLayout);
			DSLayout = nullptr;
		}
		if (DSLayoutBlur) {
			VulkanContext::get()->device.destroyDescriptorSetLayout(DSLayoutBlur);
			DSLayoutBlur = nullptr;
		}
	}

	void SSAO::update(Camera& camera)
	{
		if (GUI::show_ssao) {
			pvm[0] = camera.projection;
			pvm[1] = camera.view;
			pvm[2] = camera.invProjection;

			Queue::memcpyRequest(&UB_PVM, { { &pvm, sizeof(pvm), 0 } });
			//UB_PVM.map();
			//memcpy(UB_PVM.data, pvm, sizeof(pvm));
			//UB_PVM.flush();
			//UB_PVM.unmap();
		}
	}

	void vm::SSAO::createRenderPasses(std::map<std::string, Image>& renderTargets)
	{
		renderPass.Create(renderTargets["ssao"].format.Value(), vk::Format::eUndefined);
		blurRenderPass.Create(renderTargets["ssaoBlur"].format.Value(), vk::Format::eUndefined);
	}

	void vm::SSAO::createFrameBuffers(std::map<std::string, Image>& renderTargets)
	{
		createSSAOFrameBuffers(renderTargets);
		createSSAOBlurFrameBuffers(renderTargets);
	}

	void vm::SSAO::createSSAOFrameBuffers(std::map<std::string, Image>& renderTargets)
	{
		auto vulkan = VulkanContext::get();
		framebuffers.resize(vulkan->swapchain->images.size());
		for (size_t i = 0; i < vulkan->swapchain->images.size(); ++i)
		{
			uint32_t width = renderTargets["ssao"].width.Value();
			uint32_t height = renderTargets["ssao"].height.Value();
			vk::ImageView view = renderTargets["ssao"].view.Value();
			framebuffers[i].Create(width, height, view, renderPass);
		}
	}

	void vm::SSAO::createSSAOBlurFrameBuffers(std::map<std::string, Image>& renderTargets)
	{
		auto vulkan = VulkanContext::get();
		blurFramebuffers.resize(vulkan->swapchain->images.size());
		for (size_t i = 0; i < vulkan->swapchain->images.size(); ++i)
		{
			uint32_t width = renderTargets["ssaoBlur"].width.Value();
			uint32_t height = renderTargets["ssaoBlur"].height.Value();
			vk::ImageView view = renderTargets["ssaoBlur"].view.Value();
			blurFramebuffers[i].Create(width, height, view, blurRenderPass);
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
		Shader vert{ "shaders/Common/quad.vert", ShaderType::Vertex, true };
		Shader frag{ "shaders/SSAO/ssao.frag", ShaderType::Fragment, true };

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
		vp.width = renderTargets["ssao"].width_f.Value();
		vp.height = renderTargets["ssao"].height_f.Value();
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
			renderTargets["ssao"].blentAttachment.Value()
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
				layoutBinding(3, vk::DescriptorType::eUniformBuffer),
				layoutBinding(4, vk::DescriptorType::eUniformBuffer),
			};
			vk::DescriptorSetLayoutCreateInfo descriptorLayout;
			descriptorLayout.bindingCount = static_cast<uint32_t>(setLayoutBindings.size());
			descriptorLayout.pBindings = setLayoutBindings.data();
			DSLayout = VulkanContext::get()->device.createDescriptorSetLayout(descriptorLayout);
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

	void SSAO::createBlurPipeline(std::map<std::string, Image>& renderTargets)
	{
		// Shader stages
		Shader vert{ "shaders/Common/quad.vert", ShaderType::Vertex, true };
		Shader frag{ "shaders/SSAO/ssaoBlur.frag", ShaderType::Fragment, true };

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
		pipelineBlur.pipeinfo->stageCount = static_cast<uint32_t>(stages.size());
		pipelineBlur.pipeinfo->pStages = stages.data();

		// Vertex Input state
		vk::PipelineVertexInputStateCreateInfo pvisci;
		pipelineBlur.pipeinfo->pVertexInputState = &pvisci;

		// Input Assembly stage
		vk::PipelineInputAssemblyStateCreateInfo piasci;
		piasci.topology = vk::PrimitiveTopology::eTriangleList;
		piasci.primitiveRestartEnable = VK_FALSE;
		pipelineBlur.pipeinfo->pInputAssemblyState = &piasci;

		// Viewports and Scissors
		vk::Viewport vp;
		vp.x = 0.0f;
		vp.y = 0.0f;
		vp.width = renderTargets["ssaoBlur"].width_f.Value();
		vp.height = renderTargets["ssaoBlur"].height_f.Value();
		vp.minDepth = 0.0f;
		vp.maxDepth = 1.0f;

		vk::Rect2D r2d;
		r2d.extent = vk::Extent2D{ static_cast<uint32_t>(vp.width), static_cast<uint32_t>(vp.height) };

		vk::PipelineViewportStateCreateInfo pvsci;
		pvsci.viewportCount = 1;
		pvsci.pViewports = &vp;
		pvsci.scissorCount = 1;
		pvsci.pScissors = &r2d;
		pipelineBlur.pipeinfo->pViewportState = &pvsci;

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
		pipelineBlur.pipeinfo->pRasterizationState = &prsci;

		// Multisample state
		vk::PipelineMultisampleStateCreateInfo pmsci;
		pmsci.rasterizationSamples = vk::SampleCountFlagBits::e1;
		pmsci.sampleShadingEnable = VK_FALSE;
		pmsci.minSampleShading = 1.0f;
		pmsci.pSampleMask = nullptr;
		pmsci.alphaToCoverageEnable = VK_FALSE;
		pmsci.alphaToOneEnable = VK_FALSE;
		pipelineBlur.pipeinfo->pMultisampleState = &pmsci;

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
		pipelineBlur.pipeinfo->pDepthStencilState = &pdssci;

		// Color Blending state
		std::vector<vk::PipelineColorBlendAttachmentState> colorBlendAttachments = {
			renderTargets["ssaoBlur"].blentAttachment.Value()
		};
		vk::PipelineColorBlendStateCreateInfo pcbsci;
		pcbsci.logicOpEnable = VK_FALSE;
		pcbsci.logicOp = vk::LogicOp::eCopy;
		pcbsci.attachmentCount = static_cast<uint32_t>(colorBlendAttachments.size());
		pcbsci.pAttachments = colorBlendAttachments.data();
		float blendConstants[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		memcpy(pcbsci.blendConstants, blendConstants, 4 * sizeof(float));
		pipelineBlur.pipeinfo->pColorBlendState = &pcbsci;

		// Dynamic state
		pipelineBlur.pipeinfo->pDynamicState = nullptr;

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
			DSLayoutBlur = VulkanContext::get()->device.createDescriptorSetLayout(descriptorLayout);
		}

		std::vector<vk::DescriptorSetLayout> descriptorSetLayouts = { DSLayoutBlur };

		vk::PipelineLayoutCreateInfo plci;
		plci.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
		plci.pSetLayouts = descriptorSetLayouts.data();

		pipelineBlur.pipeinfo->layout = VulkanContext::get()->device.createPipelineLayout(plci);

		// Render Pass
		pipelineBlur.pipeinfo->renderPass = *blurRenderPass;

		// Subpass (Index of subpass this pipeline will be used in)
		pipelineBlur.pipeinfo->subpass = 0;

		// Base Pipeline Handle
		pipelineBlur.pipeinfo->basePipelineHandle = nullptr;

		// Base Pipeline Index
		pipelineBlur.pipeinfo->basePipelineIndex = -1;

		pipelineBlur.pipeline = VulkanContext::get()->device.createGraphicsPipelines(nullptr, *pipelineBlur.pipeinfo).at(0);

		// destroy Shader Modules
		VulkanContext::get()->device.destroyShaderModule(vertModule);
		VulkanContext::get()->device.destroyShaderModule(fragModule);
	}
}
