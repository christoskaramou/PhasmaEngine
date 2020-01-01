#include "Shadows.h"
#include "../GUI/GUI.h"
#include "../Swapchain/Swapchain.h"
#include "../Vertex/Vertex.h"
#include "../Shader/Shader.h"

using namespace vm;

vk::DescriptorSetLayout		Shadows::descriptorSetLayout = nullptr;
uint32_t					Shadows::imageSize = 4096;

void Shadows::createDescriptorSets()
{
	vk::DescriptorSetAllocateInfo allocateInfo;
	allocateInfo.descriptorPool = VulkanContext::get()->descriptorPool;
	allocateInfo.descriptorSetCount = 1;
	allocateInfo.pSetLayouts = &descriptorSetLayout;

	descriptorSets.resize(textures.size()); // size of wanted number of cascaded shadows
	for (uint32_t i = 0; i < descriptorSets.size(); i++) {
		descriptorSets[i] = VulkanContext::get()->device.allocateDescriptorSets(allocateInfo).at(0);

		std::vector<vk::WriteDescriptorSet> textureWriteSets(2);
		// MVP
		vk::DescriptorBufferInfo dbi;
		dbi.buffer = uniformBuffers[i].buffer;
		dbi.offset = 0;
		dbi.range = sizeof(ShadowsUBO);

		textureWriteSets[0].dstSet = descriptorSets[i];
		textureWriteSets[0].dstBinding = 0;
		textureWriteSets[0].dstArrayElement = 0;
		textureWriteSets[0].descriptorCount = 1;
		textureWriteSets[0].descriptorType = vk::DescriptorType::eUniformBuffer;
		textureWriteSets[0].pBufferInfo = &dbi;

		// sampler
		vk::DescriptorImageInfo dii;
		dii.sampler = textures[i].sampler;
		dii.imageView = textures[i].view;
		dii.imageLayout = vk::ImageLayout::eDepthStencilReadOnlyOptimal;

		textureWriteSets[1].dstSet = descriptorSets[i];
		textureWriteSets[1].dstBinding = 1;
		textureWriteSets[1].dstArrayElement = 0;
		textureWriteSets[1].descriptorCount = 1;
		textureWriteSets[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
		textureWriteSets[1].pImageInfo = &dii;

		VulkanContext::get()->device.updateDescriptorSets(textureWriteSets, nullptr);
	}
}

void Shadows::createRenderPass()
{
	vk::AttachmentDescription attachment;
	attachment.format = VulkanContext::get()->depth->format;
	attachment.samples = vk::SampleCountFlagBits::e1;
	attachment.loadOp = vk::AttachmentLoadOp::eClear;
	attachment.storeOp = vk::AttachmentStoreOp::eStore;
	attachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
	attachment.stencilStoreOp = vk::AttachmentStoreOp::eStore;
	attachment.initialLayout = vk::ImageLayout::eUndefined;
	attachment.finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

	vk::AttachmentReference depthAttachmentRef;
	depthAttachmentRef.attachment = 0;
	depthAttachmentRef.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

	vk::SubpassDescription subpassDesc;
	subpassDesc.pDepthStencilAttachment = &depthAttachmentRef;

	vk::RenderPassCreateInfo rpci;
	rpci.attachmentCount = 1;
	rpci.pAttachments = &attachment;
	rpci.subpassCount = 1;
	rpci.pSubpasses = &subpassDesc;

	renderPass = VulkanContext::get()->device.createRenderPass(rpci);
}

void Shadows::createFrameBuffers()
{
	textures.resize(3);
	for (auto& texture : textures)
	{
		texture.format = VulkanContext::get()->depth->format;
		texture.initialLayout = vk::ImageLayout::eUndefined;
		texture.addressMode = vk::SamplerAddressMode::eClampToEdge;
		texture.maxAnisotropy = 1.f;
		texture.borderColor = vk::BorderColor::eFloatOpaqueWhite;
		texture.samplerCompareEnable = VK_TRUE;
		texture.compareOp = vk::CompareOp::eGreaterOrEqual;
		texture.samplerMipmapMode = vk::SamplerMipmapMode::eLinear;

		texture.createImage(Shadows::imageSize, Shadows::imageSize, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled, vk::MemoryPropertyFlagBits::eDeviceLocal);
		texture.transitionImageLayout(vk::ImageLayout::eUndefined, vk::ImageLayout::eDepthStencilAttachmentOptimal);
		texture.createImageView(vk::ImageAspectFlagBits::eDepth);
		texture.createSampler();
	}

	frameBuffers.resize(VulkanContext::get()->swapchain->images.size() * textures.size());
	for (uint32_t i = 0; i < frameBuffers.size(); ++i)
	{
		std::vector<vk::ImageView> attachments {
			textures[i % textures.size()].view
		};

		vk::FramebufferCreateInfo fbci;
		fbci.renderPass = renderPass;
		fbci.attachmentCount = static_cast<uint32_t>(attachments.size());
		fbci.pAttachments = attachments.data();
		fbci.width = Shadows::imageSize;
		fbci.height = Shadows::imageSize;
		fbci.layers = 1;

		frameBuffers[i] = VulkanContext::get()->device.createFramebuffer(fbci);
	}
}

void Shadows::createPipeline(vk::DescriptorSetLayout mesh, vk::DescriptorSetLayout model)
{
	// Shader stages
	Shader vert { "shaders/Shadows/shaderShadows.vert", ShaderType::Vertex, true };

	vk::ShaderModuleCreateInfo vsmci;
	vsmci.codeSize = vert.byte_size();
	vsmci.pCode = vert.get_spriv();
	vk::ShaderModule vertModule = VulkanContext::get()->device.createShaderModule(vsmci);

	vk::PipelineShaderStageCreateInfo pssci1;
	pssci1.stage = vk::ShaderStageFlagBits::eVertex;
	pssci1.module = vertModule;
	pssci1.pName = "main";

	std::vector<vk::PipelineShaderStageCreateInfo> stages{ pssci1 };
	pipeline.pipeinfo.stageCount = static_cast<uint32_t>(stages.size());
	pipeline.pipeinfo.pStages = stages.data();

	// Vertex Input state
	auto vibd = Vertex::getBindingDescriptionGeneral();
	auto viad = Vertex::getAttributeDescriptionGeneral();
	vk::PipelineVertexInputStateCreateInfo pvisci;
	pvisci.vertexBindingDescriptionCount = static_cast<uint32_t>(vibd.size());
	pvisci.pVertexBindingDescriptions = vibd.data();
	pvisci.vertexAttributeDescriptionCount = static_cast<uint32_t>(viad.size());
	pvisci.pVertexAttributeDescriptions = viad.data();
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
	vp.width = static_cast<float>(Shadows::imageSize);
	vp.height = static_cast<float>(Shadows::imageSize);
	vp.minDepth = 0.f;
	vp.maxDepth = 1.f;

	vk::Rect2D r2d;
	r2d.extent = vk::Extent2D{ Shadows::imageSize, Shadows::imageSize };

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
	prsci.cullMode = vk::CullModeFlagBits::eFront;
	prsci.frontFace = vk::FrontFace::eClockwise;
	prsci.depthBiasEnable = VK_TRUE;
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
		textures[0].blentAttachment
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
	std::vector<vk::DynamicState> dynamicStates{ vk::DynamicState::eDepthBias };
	vk::PipelineDynamicStateCreateInfo dsi;
	dsi.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
	dsi.pDynamicStates = dynamicStates.data();
	pipeline.pipeinfo.pDynamicState = &dsi;

	// Pipeline Layout
	std::vector<vk::DescriptorSetLayout> descriptorSetLayouts{ getDescriptorSetLayout(), mesh, model };
	vk::PipelineLayoutCreateInfo plci;
	plci.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
	plci.pSetLayouts = descriptorSetLayouts.data();
	pipeline.pipeinfo.layout = VulkanContext::get()->device.createPipelineLayout(plci);

	// Render Pass
	pipeline.pipeinfo.renderPass = renderPass;

	// Subpass
	pipeline.pipeinfo.subpass = 0;

	// Base Pipeline Handle
	pipeline.pipeinfo.basePipelineHandle = nullptr;

	// Base Pipeline Index
	pipeline.pipeinfo.basePipelineIndex = -1;

	pipeline.pipeline = VulkanContext::get()->device.createGraphicsPipelines(nullptr, pipeline.pipeinfo).at(0);

	// destroy Shader Modules
	VulkanContext::get()->device.destroyShaderModule(vertModule);
}

vk::DescriptorSetLayout Shadows::getDescriptorSetLayout()
{
	if (!descriptorSetLayout) {
		const auto layoutBinding = [](uint32_t binding, vk::DescriptorType descriptorType, const vk::ShaderStageFlags& stageFlags) {
			return vk::DescriptorSetLayoutBinding{ binding, descriptorType, 1, stageFlags, nullptr };
		};
		std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings{
			layoutBinding(0, vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eVertex),
			layoutBinding(1, vk::DescriptorType::eCombinedImageSampler,vk::ShaderStageFlagBits::eFragment),
		};
		vk::DescriptorSetLayoutCreateInfo descriptorLayout;
		descriptorLayout.bindingCount = static_cast<uint32_t>(setLayoutBindings.size());
		descriptorLayout.pBindings = setLayoutBindings.data();
		descriptorSetLayout = VulkanContext::get()->device.createDescriptorSetLayout(descriptorLayout);
	}
	return descriptorSetLayout;
}

void Shadows::createUniformBuffers()
{
	uniformBuffers.resize(textures.size());
	for (auto& buffer : uniformBuffers) {
		buffer.createBuffer(sizeof(ShadowsUBO), vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostVisible);
		buffer.map();
		buffer.zero();
		buffer.flush();
		buffer.unmap();
	}
}

void Shadows::destroy()
{
	if (renderPass)
		VulkanContext::get()->device.destroyRenderPass(renderPass);
	if (descriptorSetLayout) {
		VulkanContext::get()->device.destroyDescriptorSetLayout(Shadows::descriptorSetLayout);
		descriptorSetLayout = nullptr;
	}
	for (auto& texture : textures)
		texture.destroy();
	for (auto& fb : frameBuffers)
		VulkanContext::get()->device.destroyFramebuffer(fb);
	for (auto& buffer : uniformBuffers)
		buffer.destroy();
	pipeline.destroy();
}

void Shadows::update(Camera& camera)
{
	static ShadowsUBO shadows_UBO = {};

	if (GUI::shadow_cast) {
		// far/cos(x) = the side size
		const float sideSizeOfPyramid = camera.nearPlane / cos(radians(camera.FOV * .5f)); // near plane is actually the far plane (they are reversed)
		const vec3 p = &GUI::sun_position[0];

		vec3 pointOnPyramid = camera.front * (sideSizeOfPyramid * .01f);
		vec3 pos = p + camera.position + pointOnPyramid; // sun position will be moved, so its angle to the lookat position is the same always
		vec3 front = normalize(camera.position + pointOnPyramid - pos);
		vec3 right = normalize(cross(front, camera.worldUp()));
		vec3 up = normalize(cross(right, front));
		float orthoSide = sideSizeOfPyramid * .01f; // small area
		shadows_UBO = {
			ortho(-orthoSide, orthoSide, -orthoSide, orthoSide, camera.nearPlane, camera.farPlane),
			lookAt(pos, front, right, up),
			1.0f,
			sideSizeOfPyramid * .02f,
			sideSizeOfPyramid * .1f,
			sideSizeOfPyramid
		};
		uniformBuffers[0].map();
		memcpy(uniformBuffers[0].data, &shadows_UBO, sizeof(ShadowsUBO));
		uniformBuffers[0].flush();
		uniformBuffers[0].unmap();

		pointOnPyramid = camera.front * (sideSizeOfPyramid * .05f);
		pos = p + camera.position + pointOnPyramid; 
		front = normalize(camera.position + pointOnPyramid - pos);
		right = normalize(cross(front, camera.worldUp()));
		up = normalize(cross(right, front));
		orthoSide = sideSizeOfPyramid * .05f; // medium area
		shadows_UBO = {
			ortho(-orthoSide, orthoSide, -orthoSide, orthoSide, camera.nearPlane, camera.farPlane),
			lookAt(pos, front, right, up),
			1.0f,
			sideSizeOfPyramid * .02f,
			sideSizeOfPyramid * .1f,
			sideSizeOfPyramid 
		};
		uniformBuffers[1].map();
		memcpy(uniformBuffers[1].data, &shadows_UBO, sizeof(ShadowsUBO));
		uniformBuffers[1].flush();
		uniformBuffers[1].unmap();

		pointOnPyramid = camera.front * (sideSizeOfPyramid * .5f);
		pos = p + camera.position + pointOnPyramid;
		front = normalize(camera.position + pointOnPyramid - pos);
		right = normalize(cross(front, camera.worldUp()));
		up = normalize(cross(right, front));
		orthoSide = sideSizeOfPyramid * .5f; // large area
		shadows_UBO = {
			ortho(-orthoSide, orthoSide, -orthoSide, orthoSide, camera.nearPlane, camera.farPlane),
			lookAt(pos, front, right, up),
			1.0f,
			sideSizeOfPyramid * .02f,
			sideSizeOfPyramid * .1f,
			sideSizeOfPyramid
		};
		uniformBuffers[2].map();
		memcpy(uniformBuffers[2].data, &shadows_UBO, sizeof(ShadowsUBO));
		uniformBuffers[2].flush();
		uniformBuffers[2].unmap();
	}
	else
	{
		shadows_UBO.castShadows = 0.f;
		uniformBuffers[0].map();
		memcpy(uniformBuffers[0].data, &shadows_UBO, sizeof(ShadowsUBO));
		uniformBuffers[0].flush();
		uniformBuffers[0].unmap();

		uniformBuffers[1].map();
		memcpy(uniformBuffers[1].data, &shadows_UBO, sizeof(ShadowsUBO));
		uniformBuffers[1].flush();
		uniformBuffers[1].unmap();

		uniformBuffers[2].map();
		memcpy(uniformBuffers[2].data, &shadows_UBO, sizeof(ShadowsUBO));
		uniformBuffers[2].flush();
		uniformBuffers[2].unmap();
	}
}
