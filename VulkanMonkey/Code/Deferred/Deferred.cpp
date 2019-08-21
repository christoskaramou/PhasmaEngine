#include "Deferred.h"
#include "../Model/Model.h"
#include "../Mesh/Mesh.h"
#include "../Swapchain/Swapchain.h"
#include "../Surface/Surface.h"
#include "tinygltf/stb_image.h"
#include <deque>

using namespace vm;

void Deferred::batchStart(vk::CommandBuffer cmd, uint32_t imageIndex, const vk::Extent2D& extent)
{
	vk::ClearValue clearColor;
	memcpy(clearColor.color.float32, GUI::clearColor.data(), 4 * sizeof(float));

	vk::ClearDepthStencilValue depthStencil;
	depthStencil.depth = 0.f;
	depthStencil.stencil = 0;

	std::vector<vk::ClearValue> clearValues = { clearColor, clearColor, clearColor, clearColor, clearColor, clearColor, depthStencil };

	vk::RenderPassBeginInfo rpi;
	rpi.renderPass = renderPass;
	rpi.framebuffer = frameBuffers[imageIndex];
	rpi.renderArea.offset = vk::Offset2D{ 0, 0 };
	rpi.renderArea.extent = extent;
	rpi.clearValueCount = static_cast<uint32_t>(clearValues.size());
	rpi.pClearValues = clearValues.data();

	cmd.beginRenderPass(rpi, vk::SubpassContents::eInline);

	Model::commandBuffer = cmd;
	Model::pipeline = &pipeline;
}

void Deferred::batchEnd()
{
	Model::commandBuffer.endRenderPass();
	Model::commandBuffer = nullptr;
	Model::pipeline = nullptr;
}

void Deferred::createDeferredUniforms(std::map<std::string, Image>& renderTargets, LightUniforms& lightUniforms)
{
	auto vulkan = VulkanContext::get();
	const vk::DescriptorSetAllocateInfo allocInfo = vk::DescriptorSetAllocateInfo{
		vulkan->descriptorPool,					//DescriptorPool descriptorPool;
		1,										//uint32_t descriptorSetCount;
		&DSLayoutComposition					//const DescriptorSetLayout* pSetLayouts;
	};
	DSComposition = vulkan->device.allocateDescriptorSets(allocInfo).at(0);

	// Check if ibl_brdf_lut is already loaded
	const std::string path = "objects/ibl_brdf_lut.png";
	if (Mesh::uniqueTextures.find(path) != Mesh::uniqueTextures.end()) {
		ibl_brdf_lut = Mesh::uniqueTextures[path];
	}
	else {
		int texWidth, texHeight, texChannels;
		stbi_set_flip_vertically_on_load(true);
		unsigned char* pixels = stbi_load(path.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
		stbi_set_flip_vertically_on_load(false);
		if (!pixels)
			throw std::runtime_error("No pixel data loaded");
		const vk::DeviceSize imageSize = texWidth * texHeight * STBI_rgb_alpha;

		vulkan->waitAndLockSubmits();

		Buffer staging;
		staging.createBuffer(imageSize, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
		staging.map();
		staging.copyData(pixels);
		staging.unmap();

		stbi_image_free(pixels);

		ibl_brdf_lut.name = path;
		ibl_brdf_lut.format = vk::Format::eR8G8B8A8Unorm;
		ibl_brdf_lut.mipLevels = static_cast<uint32_t>(std::floor(std::log2(texWidth > texHeight ? texWidth : texHeight))) + 1;
		ibl_brdf_lut.createImage(texWidth, texHeight, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled, vk::MemoryPropertyFlagBits::eDeviceLocal);
		ibl_brdf_lut.transitionImageLayout(vk::ImageLayout::ePreinitialized, vk::ImageLayout::eTransferDstOptimal);
		ibl_brdf_lut.copyBufferToImage(staging.buffer);
		ibl_brdf_lut.generateMipMaps();
		ibl_brdf_lut.createImageView(vk::ImageAspectFlagBits::eColor);
		ibl_brdf_lut.maxLod = static_cast<float>(ibl_brdf_lut.mipLevels);
		ibl_brdf_lut.createSampler();

		staging.destroy();

		vulkan->unlockSubmits();

		Mesh::uniqueTextures[path] = ibl_brdf_lut;
	}

	updateDescriptorSets(renderTargets, lightUniforms);
}

void Deferred::updateDescriptorSets(std::map<std::string, Image>& renderTargets, LightUniforms& lightUniforms)
{
	std::deque<vk::DescriptorImageInfo> dsii{};
	auto const wSetImage = [&dsii](vk::DescriptorSet& dstSet, uint32_t dstBinding, Image& image) {
		dsii.emplace_back(image.sampler, image.view, vk::ImageLayout::eShaderReadOnlyOptimal);
		return vk::WriteDescriptorSet{ dstSet, dstBinding, 0, 1, vk::DescriptorType::eCombinedImageSampler, &dsii.back(), nullptr, nullptr };
	};
	std::deque<vk::DescriptorBufferInfo> dsbi{};
	auto const wSetBuffer = [&dsbi](vk::DescriptorSet& dstSet, uint32_t dstBinding, Buffer& buffer) {
		dsbi.emplace_back(buffer.buffer, 0, buffer.size);
		return vk::WriteDescriptorSet{ dstSet, dstBinding, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &dsbi.back(), nullptr };
	};

	std::vector<vk::WriteDescriptorSet> writeDescriptorSets = {
		wSetImage(DSComposition, 0, renderTargets["depth"]),
		wSetImage(DSComposition, 1, renderTargets["normal"]),
		wSetImage(DSComposition, 2, renderTargets["albedo"]),
		wSetImage(DSComposition, 3, renderTargets["srm"]),
		wSetBuffer(DSComposition, 4, lightUniforms.uniform),
		wSetImage(DSComposition, 5, renderTargets["ssaoBlur"]),
		wSetImage(DSComposition, 6, renderTargets["ssr"]),
		wSetImage(DSComposition, 7, renderTargets["emissive"]),
		wSetImage(DSComposition, 8, ibl_brdf_lut)
	};

	VulkanContext::get()->device.updateDescriptorSets(writeDescriptorSets, nullptr);
}

void Deferred::draw(vk::CommandBuffer cmd, uint32_t imageIndex, Shadows& shadows, SkyBox& skybox, mat4& invViewProj, const vk::Extent2D& extent)
{
	// Begin Composition
	vk::ClearValue clearColor;
	memcpy(clearColor.color.float32, GUI::clearColor.data(), 4 * sizeof(float));

	std::vector<vk::ClearValue> clearValues = { clearColor, clearColor };

	vk::RenderPassBeginInfo rpi;
	rpi.renderPass = compositionRenderPass;
	rpi.framebuffer = compositionFrameBuffers[imageIndex];
	rpi.renderArea.offset = vk::Offset2D{ 0, 0 };
	rpi.renderArea.extent = extent;
	rpi.clearValueCount = static_cast<uint32_t>(clearValues.size());
	rpi.pClearValues = clearValues.data();
	cmd.beginRenderPass(rpi, vk::SubpassContents::eInline);

	std::vector<vec4> screenSpace(7);
	screenSpace[0] = { static_cast<float>(GUI::show_ssao), static_cast<float>(GUI::show_ssr) , static_cast<float>(GUI::show_tonemapping), static_cast<float>(GUI::use_AntiAliasing) };
	screenSpace[1] = { static_cast<float>(GUI::use_IBL), static_cast<float>(GUI::use_Volumetric_lights), static_cast<float>(GUI::volumetric_steps), static_cast<float>(GUI::volumetric_dither_strength) };
	screenSpace[2] = { invViewProj[0] };
	screenSpace[3] = { invViewProj[1] };
	screenSpace[4] = { invViewProj[2] };
	screenSpace[5] = { invViewProj[3] };
	screenSpace[6] = { GUI::exposure, GUI::lights_intensity, GUI::lights_range, GUI::fog_intensity };

	cmd.pushConstants<vec4>(pipelineComposition.pipeinfo.layout, vk::ShaderStageFlagBits::eFragment, 0, screenSpace);
	cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelineComposition.pipeline);
	cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineComposition.pipeinfo.layout, 0, { DSComposition, shadows.descriptorSets[0], shadows.descriptorSets[1], shadows.descriptorSets[2], skybox.descriptorSet }, nullptr);
	cmd.draw(3, 1, 0, 0);
	cmd.endRenderPass();
	// End Composition
}

void Deferred::createRenderPasses(std::map<std::string, Image>& renderTargets)
{
	createGBufferRenderPasses(renderTargets);
	createCompositionRenderPass(renderTargets);
}

void Deferred::createGBufferRenderPasses(std::map<std::string, Image>& renderTargets)
{
	std::array<vk::AttachmentDescription, 7> attachments{};
	// Depth store
	attachments[0].format = renderTargets["depth"].format;
	attachments[0].samples = vk::SampleCountFlagBits::e1;
	attachments[0].loadOp = vk::AttachmentLoadOp::eClear;
	attachments[0].storeOp = vk::AttachmentStoreOp::eStore;
	attachments[0].stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
	attachments[0].stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
	attachments[0].initialLayout = vk::ImageLayout::eUndefined;
	attachments[0].finalLayout = vk::ImageLayout::eColorAttachmentOptimal;
	// Normals
	attachments[1].format = renderTargets["normal"].format;
	attachments[1].samples = vk::SampleCountFlagBits::e1;
	attachments[1].loadOp = vk::AttachmentLoadOp::eClear;
	attachments[1].storeOp = vk::AttachmentStoreOp::eStore;
	attachments[1].stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
	attachments[1].stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
	attachments[1].initialLayout = vk::ImageLayout::eUndefined;
	attachments[1].finalLayout = vk::ImageLayout::eColorAttachmentOptimal;
	// Albedo
	attachments[2].format = renderTargets["albedo"].format;
	attachments[2].samples = vk::SampleCountFlagBits::e1;
	attachments[2].loadOp = vk::AttachmentLoadOp::eClear;
	attachments[2].storeOp = vk::AttachmentStoreOp::eStore;
	attachments[2].stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
	attachments[2].stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
	attachments[2].initialLayout = vk::ImageLayout::eUndefined;
	attachments[2].finalLayout = vk::ImageLayout::eColorAttachmentOptimal;
	// Specular Roughness Metallic
	attachments[3].format = renderTargets["srm"].format;
	attachments[3].samples = vk::SampleCountFlagBits::e1;
	attachments[3].loadOp = vk::AttachmentLoadOp::eClear;
	attachments[3].storeOp = vk::AttachmentStoreOp::eStore;
	attachments[3].stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
	attachments[3].stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
	attachments[3].initialLayout = vk::ImageLayout::eUndefined;
	attachments[3].finalLayout = vk::ImageLayout::eColorAttachmentOptimal;
	// Velocity
	attachments[4].format = renderTargets["velocity"].format;
	attachments[4].samples = vk::SampleCountFlagBits::e1;
	attachments[4].loadOp = vk::AttachmentLoadOp::eClear;
	attachments[4].storeOp = vk::AttachmentStoreOp::eStore;
	attachments[4].stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
	attachments[4].stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
	attachments[4].initialLayout = vk::ImageLayout::eUndefined;
	attachments[4].finalLayout = vk::ImageLayout::eColorAttachmentOptimal;
	// Emissive
	attachments[5].format = renderTargets["emissive"].format;
	attachments[5].samples = vk::SampleCountFlagBits::e1;
	attachments[5].loadOp = vk::AttachmentLoadOp::eClear;
	attachments[5].storeOp = vk::AttachmentStoreOp::eStore;
	attachments[5].stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
	attachments[5].stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
	attachments[5].initialLayout = vk::ImageLayout::eUndefined;
	attachments[5].finalLayout = vk::ImageLayout::eColorAttachmentOptimal;
	// Depth
	attachments[6].format = VulkanContext::get()->depth->format;
	attachments[6].samples = vk::SampleCountFlagBits::e1;
	attachments[6].loadOp = vk::AttachmentLoadOp::eClear;
	attachments[6].storeOp = vk::AttachmentStoreOp::eDontCare;
	attachments[6].stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
	attachments[6].stencilStoreOp = vk::AttachmentStoreOp::eStore;
	attachments[6].initialLayout = vk::ImageLayout::eUndefined;
	attachments[6].finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

	std::array<vk::SubpassDescription, 1> subpassDescriptions{};

	std::vector<vk::AttachmentReference> colorReferences{
		{ 0, vk::ImageLayout::eColorAttachmentOptimal },
		{ 1, vk::ImageLayout::eColorAttachmentOptimal },
		{ 2, vk::ImageLayout::eColorAttachmentOptimal },
		{ 3, vk::ImageLayout::eColorAttachmentOptimal },
		{ 4, vk::ImageLayout::eColorAttachmentOptimal },
		{ 5, vk::ImageLayout::eColorAttachmentOptimal }
	};
	vk::AttachmentReference depthReference = { 6, vk::ImageLayout::eDepthStencilAttachmentOptimal };

	subpassDescriptions[0].pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
	subpassDescriptions[0].colorAttachmentCount = static_cast<uint32_t>(colorReferences.size());
	subpassDescriptions[0].pColorAttachments = colorReferences.data();
	subpassDescriptions[0].pDepthStencilAttachment = &depthReference;

	vk::RenderPassCreateInfo renderPassInfo = {};
	renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
	renderPassInfo.pAttachments = attachments.data();
	renderPassInfo.subpassCount = static_cast<uint32_t>(subpassDescriptions.size());
	renderPassInfo.pSubpasses = subpassDescriptions.data();

	renderPass = VulkanContext::get()->device.createRenderPass(renderPassInfo);
}

void Deferred::createCompositionRenderPass(std::map<std::string, Image>& renderTargets)
{
	std::array<vk::AttachmentDescription, 1> attachments{};
	// Color target
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

	std::array<vk::SubpassDescription, 1> subpassDescriptions{};
	subpassDescriptions[0].pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
	subpassDescriptions[0].colorAttachmentCount = static_cast<uint32_t>(colorReferences.size());
	subpassDescriptions[0].pColorAttachments = colorReferences.data();
	subpassDescriptions[0].pDepthStencilAttachment = nullptr;

	vk::RenderPassCreateInfo renderPassInfo = {};
	renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
	renderPassInfo.pAttachments = attachments.data();
	renderPassInfo.subpassCount = static_cast<uint32_t>(subpassDescriptions.size());
	renderPassInfo.pSubpasses = subpassDescriptions.data();

	compositionRenderPass = VulkanContext::get()->device.createRenderPass(renderPassInfo);
}

void Deferred::createFrameBuffers(std::map<std::string, Image>& renderTargets)
{
	createGBufferFrameBuffers(renderTargets);
	createCompositionFrameBuffers(renderTargets);
}

void Deferred::createGBufferFrameBuffers(std::map<std::string, Image>& renderTargets)
{
	frameBuffers.resize(VulkanContext::get()->swapchain->images.size());
	for (auto& frameBuffer : frameBuffers) {
		std::vector<vk::ImageView> attachments = {
			renderTargets["depth"].view,
			renderTargets["normal"].view,
			renderTargets["albedo"].view,
			renderTargets["srm"].view,
			renderTargets["velocity"].view,
			renderTargets["emissive"].view,
			VulkanContext::get()->depth->view
		};
		vk::FramebufferCreateInfo fbci;
		fbci.renderPass = renderPass;
		fbci.attachmentCount = static_cast<uint32_t>(attachments.size());
		fbci.pAttachments = attachments.data();
		fbci.width = renderTargets["albedo"].width;
		fbci.height = renderTargets["albedo"].height;
		fbci.layers = 1;
		frameBuffer = VulkanContext::get()->device.createFramebuffer(fbci);
	}
}

void Deferred::createCompositionFrameBuffers(std::map<std::string, Image>& renderTargets)
{
	compositionFrameBuffers.resize(VulkanContext::get()->swapchain->images.size());

	for (auto& compositionFrameBuffer : compositionFrameBuffers) {
		std::vector<vk::ImageView> attachments = {
			renderTargets["viewport"].view
		};
		vk::FramebufferCreateInfo fbci;
		fbci.renderPass = compositionRenderPass;
		fbci.attachmentCount = static_cast<uint32_t>(attachments.size());
		fbci.pAttachments = attachments.data();
		fbci.width = renderTargets["viewport"].width;
		fbci.height = renderTargets["viewport"].height;
		fbci.layers = 1;
		compositionFrameBuffer = VulkanContext::get()->device.createFramebuffer(fbci);
	}
}

void Deferred::createPipelines(std::map<std::string, Image>& renderTargets)
{
	createGBufferPipeline(renderTargets);
	createCompositionPipeline(renderTargets);
}

void Deferred::createGBufferPipeline(std::map<std::string, Image>& renderTargets)
{
	// Shader stages
	std::vector<char> vertCode = readFile("shaders/Deferred/vert.spv");
	vk::ShaderModuleCreateInfo vsmci;
	vsmci.codeSize = vertCode.size();
	vsmci.pCode = reinterpret_cast<const uint32_t*>(vertCode.data());
	vk::ShaderModule vertModule = VulkanContext::get()->device.createShaderModule(vsmci);

	std::vector<char> fragCode = readFile("shaders/Deferred/frag.spv");
	vk::ShaderModuleCreateInfo fsmci;
	fsmci.codeSize = fragCode.size();
	fsmci.pCode = reinterpret_cast<const uint32_t*>(fragCode.data());
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
	vp.width = renderTargets["albedo"].width_f;
	vp.height = renderTargets["albedo"].height_f;
	vp.minDepth = 0.f;
	vp.maxDepth = 1.f;

	vk::Rect2D r2d;
	r2d.extent = vk::Extent2D{ static_cast<uint32_t>(WIDTH_f * GUI::renderTargetsScale), static_cast<uint32_t>(HEIGHT_f * GUI::renderTargetsScale) };

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
	VulkanContext::get()->swapchain->images[0].blentAttachment.blendEnable = VK_TRUE;
	std::vector<vk::PipelineColorBlendAttachmentState> colorBlendAttachments = { 
			renderTargets["depth"].blentAttachment,
			renderTargets["normal"].blentAttachment,
			renderTargets["albedo"].blentAttachment,
			renderTargets["srm"].blentAttachment,
			renderTargets["velocity"].blentAttachment,
			renderTargets["emissive"].blentAttachment,
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
	std::vector<vk::DynamicState> dynamicStates{};//vk::DynamicState::eViewport, vk::DynamicState::eScissor };
	vk::PipelineDynamicStateCreateInfo dsi;
	dsi.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
	dsi.pDynamicStates = dynamicStates.data();
	pipeline.pipeinfo.pDynamicState = &dsi;

	// Pipeline Layout
	std::vector<vk::DescriptorSetLayout> descriptorSetLayouts{ *Mesh::getDescriptorSetLayout(), *Primitive::getDescriptorSetLayout(), *Model::getDescriptorSetLayout() };
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
	VulkanContext::get()->device.destroyShaderModule(fragModule);
}

void Deferred::createCompositionPipeline(std::map<std::string, Image>& renderTargets)
{
	// Shader stages
	std::vector<char> vertCode = readFile("shaders/Deferred/cvert.spv");
	vk::ShaderModuleCreateInfo vsmci;
	vsmci.codeSize = vertCode.size();
	vsmci.pCode = reinterpret_cast<const uint32_t*>(vertCode.data());
	vk::ShaderModule vertModule = VulkanContext::get()->device.createShaderModule(vsmci);

	std::vector<char> fragCode = readFile("shaders/Deferred/cfrag.spv");
	vk::ShaderModuleCreateInfo fsmci;
	fsmci.codeSize = fragCode.size();
	fsmci.pCode = reinterpret_cast<const uint32_t*>(fragCode.data());
	vk::ShaderModule fragModule = VulkanContext::get()->device.createShaderModule(fsmci);

	vk::PipelineShaderStageCreateInfo pssci1;
	pssci1.stage = vk::ShaderStageFlagBits::eVertex;
	pssci1.module = vertModule;
	pssci1.pName = "main";

	auto max_lights = MAX_LIGHTS;
	vk::SpecializationMapEntry sme;
	sme.size = sizeof(max_lights);
	vk::SpecializationInfo si;
	si.mapEntryCount = 1;
	si.pMapEntries = &sme;
	si.dataSize = sizeof(max_lights);
	si.pData = &max_lights;

	vk::PipelineShaderStageCreateInfo pssci2;
	pssci2.stage = vk::ShaderStageFlagBits::eFragment;
	pssci2.module = fragModule;
	pssci2.pName = "main";
	pssci2.pSpecializationInfo = &si;

	std::vector<vk::PipelineShaderStageCreateInfo> stages{ pssci1, pssci2 };
	pipelineComposition.pipeinfo.stageCount = static_cast<uint32_t>(stages.size());
	pipelineComposition.pipeinfo.pStages = stages.data();

	// Vertex Input state
	vk::PipelineVertexInputStateCreateInfo pvisci;
	pipelineComposition.pipeinfo.pVertexInputState = &pvisci;

	// Input Assembly stage
	vk::PipelineInputAssemblyStateCreateInfo piasci;
	piasci.topology = vk::PrimitiveTopology::eTriangleList;
	piasci.primitiveRestartEnable = VK_FALSE;
	pipelineComposition.pipeinfo.pInputAssemblyState = &piasci;

	// Viewports and Scissors
	vk::Viewport vp;
	vp.x = 0.0f;
	vp.y = 0.0f;
	vp.width = renderTargets["viewport"].width_f;
	vp.height = renderTargets["viewport"].height_f;
	vp.minDepth = 0.0f;
	vp.maxDepth = 1.0f;

	vk::Rect2D r2d;
	r2d.extent = VulkanContext::get()->surface->actualExtent;

	vk::PipelineViewportStateCreateInfo pvsci;
	pvsci.viewportCount = 1;
	pvsci.pViewports = &vp;
	pvsci.scissorCount = 1;
	pvsci.pScissors = &r2d;
	pipelineComposition.pipeinfo.pViewportState = &pvsci;

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
	pipelineComposition.pipeinfo.pRasterizationState = &prsci;

	// Multisample state
	vk::PipelineMultisampleStateCreateInfo pmsci;
	pmsci.rasterizationSamples = vk::SampleCountFlagBits::e1;
	pmsci.sampleShadingEnable = VK_FALSE;
	pmsci.minSampleShading = 1.0f;
	pmsci.pSampleMask = nullptr;
	pmsci.alphaToCoverageEnable = VK_FALSE;
	pmsci.alphaToOneEnable = VK_FALSE;
	pipelineComposition.pipeinfo.pMultisampleState = &pmsci;

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
	pipelineComposition.pipeinfo.pDepthStencilState = &pdssci;

	// Color Blending state
	std::vector<vk::PipelineColorBlendAttachmentState> colorBlendAttachments = { 
		renderTargets["viewport"].blentAttachment
	};
	vk::PipelineColorBlendStateCreateInfo pcbsci;
	pcbsci.logicOpEnable = VK_FALSE;
	pcbsci.logicOp = vk::LogicOp::eAnd;
	pcbsci.attachmentCount = static_cast<uint32_t>(colorBlendAttachments.size());
	pcbsci.pAttachments = colorBlendAttachments.data();
	float blendConstants[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	memcpy(pcbsci.blendConstants, blendConstants, 4 * sizeof(float));
	pipelineComposition.pipeinfo.pColorBlendState = &pcbsci;

	// Dynamic state
	pipelineComposition.pipeinfo.pDynamicState = nullptr;

	// Pipeline Layout
	if (!DSLayoutComposition)
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
			layoutBinding(5, vk::DescriptorType::eCombinedImageSampler),
			layoutBinding(6, vk::DescriptorType::eCombinedImageSampler),
			layoutBinding(7, vk::DescriptorType::eCombinedImageSampler),
			layoutBinding(8, vk::DescriptorType::eCombinedImageSampler),
		};
		vk::DescriptorSetLayoutCreateInfo descriptorLayout;
		descriptorLayout.bindingCount = static_cast<uint32_t>(setLayoutBindings.size());
		descriptorLayout.pBindings = setLayoutBindings.data();
		DSLayoutComposition = VulkanContext::get()->device.createDescriptorSetLayout(descriptorLayout);
	}

	std::vector<vk::DescriptorSetLayout> descriptorSetLayouts = {
		DSLayoutComposition,
		Shadows::getDescriptorSetLayout(),
		Shadows::getDescriptorSetLayout(),
		Shadows::getDescriptorSetLayout(),
		SkyBox::getDescriptorSetLayout() };

	vk::PushConstantRange pConstants;
	pConstants.stageFlags = vk::ShaderStageFlagBits::eFragment;
	pConstants.offset = 0;
	pConstants.size = 7 * sizeof(vec4);

	vk::PipelineLayoutCreateInfo plci;
	plci.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
	plci.pSetLayouts = descriptorSetLayouts.data();
	plci.pushConstantRangeCount = 1;
	plci.pPushConstantRanges = &pConstants;
	pipelineComposition.pipeinfo.layout = VulkanContext::get()->device.createPipelineLayout(plci);

	// Render Pass
	pipelineComposition.pipeinfo.renderPass = compositionRenderPass;

	// Subpass (Index of subpass this pipeline will be used in)
	pipelineComposition.pipeinfo.subpass = 0;

	// Base Pipeline Handle
	pipelineComposition.pipeinfo.basePipelineHandle = nullptr;

	// Base Pipeline Index
	pipelineComposition.pipeinfo.basePipelineIndex = -1;

	pipelineComposition.pipeline = VulkanContext::get()->device.createGraphicsPipelines(nullptr, pipelineComposition.pipeinfo).at(0);

	// destroy Shader Modules
	VulkanContext::get()->device.destroyShaderModule(vertModule);
	VulkanContext::get()->device.destroyShaderModule(fragModule);
}

void Deferred::destroy()
{
	auto vulkan = VulkanContext::get();
	if (renderPass) {
		vulkan->device.destroyRenderPass(renderPass);
		renderPass = nullptr;
	}
	if (compositionRenderPass) {
		vulkan->device.destroyRenderPass(compositionRenderPass);
		compositionRenderPass = nullptr;
	}
	for (auto &frameBuffer : frameBuffers) {
		if (frameBuffer) {
			vulkan->device.destroyFramebuffer(frameBuffer);
		}
	}
	for (auto &frameBuffer : compositionFrameBuffers) {
		if (frameBuffer) {
			vulkan->device.destroyFramebuffer(frameBuffer);
		}
	}
	if (DSLayoutComposition) {
		vulkan->device.destroyDescriptorSetLayout(DSLayoutComposition);
		DSLayoutComposition = nullptr;
	}
	pipeline.destroy();
	pipelineComposition.destroy();
}
