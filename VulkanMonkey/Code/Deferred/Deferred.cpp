#include "vulkanPCH.h"
#include "Deferred.h"
#include "../Model/Model.h"
#include "../Model/Mesh.h"
#include "../Swapchain/Swapchain.h"
#include "../Core/Surface.h"
#include "../Shader/Shader.h"
#include "../Core/Queue.h"
#include "../GUI/GUI.h"
#include "tinygltf/stb_image.h"
#include <deque>
#include "../Shader/Reflection.h"
#include "../VulkanContext/VulkanContext.h"

namespace vm
{
	Deferred::Deferred()
	{
		DSComposition = vk::DescriptorSet();
	}

	Deferred::~Deferred()
	{
	}

	void Deferred::batchStart(vk::CommandBuffer cmd, uint32_t imageIndex, const vk::Extent2D& extent)
	{
		vk::ClearValue clearColor;
		memcpy(clearColor.color.float32, GUI::clearColor.data(), 4 * sizeof(float));

		vk::ClearDepthStencilValue depthStencil;
		depthStencil.depth = 0.f;
		depthStencil.stencil = 0;

		std::vector<vk::ClearValue> clearValues = { clearColor, clearColor, clearColor, clearColor, clearColor, clearColor, depthStencil };

		vk::RenderPassBeginInfo rpi;
		rpi.renderPass = *renderPass;
		rpi.framebuffer = *framebuffers[imageIndex];
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
		Model::commandBuffer->endRenderPass();
		*Model::commandBuffer = nullptr;
		Model::pipeline = nullptr;
	}

	void Deferred::createDeferredUniforms(std::map<std::string, Image>& renderTargets, LightUniforms& lightUniforms)
	{
		uniform.createBuffer(sizeof(ubo), vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostVisible);
		uniform.map();
		uniform.zero();
		uniform.flush();
		uniform.unmap();

		auto vulkan = VulkanContext::get();
		const vk::DescriptorSetAllocateInfo allocInfo =
			vk::DescriptorSetAllocateInfo {
				vulkan->descriptorPool.Value(),			//DescriptorPool descriptorPool;
				1,										//uint32_t descriptorSetCount;
				&Pipeline::getDescriptorSetLayoutComposition() //const DescriptorSetLayout* pSetLayouts;
		};
		DSComposition = vulkan->device->allocateDescriptorSets(allocInfo).at(0);

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

			vulkan->graphicsQueue->waitIdle();
			vulkan->waitAndLockSubmits();

			Buffer staging;
			staging.createBuffer(imageSize, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible);
			staging.map();
			staging.copyData(pixels);
			staging.flush();
			staging.unmap();

			stbi_image_free(pixels);

			ibl_brdf_lut.format = vk::Format::eR8G8B8A8Unorm;
			ibl_brdf_lut.mipLevels = static_cast<uint32_t>(std::floor(std::log2(texWidth > texHeight ? texWidth : texHeight))) + 1;
			ibl_brdf_lut.createImage(texWidth, texHeight, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled, vk::MemoryPropertyFlagBits::eDeviceLocal);
			ibl_brdf_lut.transitionImageLayout(vk::ImageLayout::ePreinitialized, vk::ImageLayout::eTransferDstOptimal);
			ibl_brdf_lut.copyBufferToImage(staging.buffer.Value());
			ibl_brdf_lut.generateMipMaps();
			ibl_brdf_lut.createImageView(vk::ImageAspectFlagBits::eColor);
			ibl_brdf_lut.maxLod = static_cast<float>(ibl_brdf_lut.mipLevels.Value());
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
		auto const wSetImage = [&dsii](const vk::DescriptorSet& dstSet, uint32_t dstBinding, Image& image) {
			dsii.emplace_back(image.sampler.Value(), image.view.Value(), vk::ImageLayout::eShaderReadOnlyOptimal);
			return vk::WriteDescriptorSet{ dstSet, dstBinding, 0, 1, vk::DescriptorType::eCombinedImageSampler, &dsii.back(), nullptr, nullptr };
		};
		std::deque<vk::DescriptorBufferInfo> dsbi{};
		auto const wSetBuffer = [&dsbi](const vk::DescriptorSet& dstSet, uint32_t dstBinding, Buffer& buffer) {
			dsbi.emplace_back(buffer.buffer.Value(), 0, buffer.size.Value());
			return vk::WriteDescriptorSet{ dstSet, dstBinding, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &dsbi.back(), nullptr };
		};

		std::vector<vk::WriteDescriptorSet> writeDescriptorSets = {
			wSetImage(DSComposition.Value(), 0, renderTargets["depth"]),
			wSetImage(DSComposition.Value(), 1, renderTargets["normal"]),
			wSetImage(DSComposition.Value(), 2, renderTargets["albedo"]),
			wSetImage(DSComposition.Value(), 3, renderTargets["srm"]),
			wSetBuffer(DSComposition.Value(), 4, lightUniforms.uniform),
			wSetImage(DSComposition.Value(), 5, renderTargets["ssaoBlur"]),
			wSetImage(DSComposition.Value(), 6, renderTargets["ssr"]),
			wSetImage(DSComposition.Value(), 7, renderTargets["emissive"]),
			wSetImage(DSComposition.Value(), 8, ibl_brdf_lut),
			wSetBuffer(DSComposition.Value(), 9, uniform)
		};

		VulkanContext::get()->device->updateDescriptorSets(writeDescriptorSets, nullptr);
	}

	void Deferred::update(mat4& invViewProj)
	{
		ubo.screenSpace[0] = { invViewProj[0] };
		ubo.screenSpace[1] = { invViewProj[1] };
		ubo.screenSpace[2] = { invViewProj[2] };
		ubo.screenSpace[3] = { invViewProj[3] };
		ubo.screenSpace[4] = { static_cast<float>(GUI::show_ssao), static_cast<float>(GUI::show_ssr) , static_cast<float>(GUI::show_tonemapping), static_cast<float>(GUI::use_AntiAliasing) };
		ubo.screenSpace[5] = { static_cast<float>(GUI::use_IBL), static_cast<float>(GUI::use_Volumetric_lights), static_cast<float>(GUI::volumetric_steps), static_cast<float>(GUI::volumetric_dither_strength) };
		ubo.screenSpace[6] = { GUI::fog_global_thickness, GUI::lights_intensity, GUI::lights_range, GUI::fog_max_height };
		ubo.screenSpace[7] = { GUI::fog_ground_thickness, static_cast<float>(GUI::use_fog), static_cast<float>(GUI::shadow_cast), 0.0f };

		Queue::memcpyRequest(&uniform, { { &ubo, sizeof(ubo), 0 } });
		//uniform.map();
		//uniform.copyData(&ubo);
		//uniform.flush();
		//uniform.unmap();
	}

	void Deferred::draw(vk::CommandBuffer cmd, uint32_t imageIndex, Shadows& shadows, SkyBox& skybox, const vk::Extent2D& extent)
	{
		// Begin Composition
		vk::ClearValue clearColor;
		memcpy(clearColor.color.float32, GUI::clearColor.data(), 4 * sizeof(float));

		std::vector<vk::ClearValue> clearValues = { clearColor, clearColor };

		vk::RenderPassBeginInfo rpi;
		rpi.renderPass = *compositionRenderPass;
		rpi.framebuffer = *compositionFramebuffers[imageIndex];
		rpi.renderArea.offset = vk::Offset2D{ 0, 0 };
		rpi.renderArea.extent = extent;
		rpi.clearValueCount = static_cast<uint32_t>(clearValues.size());
		rpi.pClearValues = clearValues.data();
		cmd.beginRenderPass(rpi, vk::SubpassContents::eInline);

		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelineComposition.pipeline.Value());
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineComposition.pipelineLayout.Value(), 0, { DSComposition.Value(), (*shadows.descriptorSets)[0], (*shadows.descriptorSets)[1], (*shadows.descriptorSets)[2], skybox.descriptorSet.Value() }, nullptr);
		cmd.draw(3, 1, 0, 0);
		cmd.endRenderPass();
		// End Composition
	}

	void Deferred::createRenderPasses(std::map<std::string, Image>& renderTargets)
	{
		std::vector<vk::Format> formats
		{
			renderTargets["depth"].format.Value(),
			renderTargets["normal"].format.Value(),
			renderTargets["albedo"].format.Value(),
			renderTargets["srm"].format.Value(),
			renderTargets["velocity"].format.Value(),
			renderTargets["emissive"].format.Value()
		};
		renderPass.Create(formats, VulkanContext::get()->depth.format.Value());
		compositionRenderPass.Create(renderTargets["viewport"].format.Value(), vk::Format::eUndefined);
	}

	void Deferred::createFrameBuffers(std::map<std::string, Image>& renderTargets)
	{
		createGBufferFrameBuffers(renderTargets);
		createCompositionFrameBuffers(renderTargets);
	}

	void Deferred::createGBufferFrameBuffers(std::map<std::string, Image>& renderTargets)
	{
		auto vulkan = VulkanContext::get();

		framebuffers.resize(vulkan->swapchain.images.size());
		for (size_t i = 0; i < vulkan->swapchain.images.size(); ++i)
		{
			uint32_t width = renderTargets["albedo"].width.Value();
			uint32_t height = renderTargets["albedo"].height.Value();
			std::vector<vk::ImageView> views = {
				renderTargets["depth"].view.Value(),
				renderTargets["normal"].view.Value(),
				renderTargets["albedo"].view.Value(),
				renderTargets["srm"].view.Value(),
				renderTargets["velocity"].view.Value(),
				renderTargets["emissive"].view.Value(),
				VulkanContext::get()->depth.view.Value()
			};
			framebuffers[i].Create(width, height, views, renderPass);
		}
	}

	void Deferred::createCompositionFrameBuffers(std::map<std::string, Image>& renderTargets)
	{
		auto vulkan = VulkanContext::get();

		compositionFramebuffers.resize(vulkan->swapchain.images.size());
		for (size_t i = 0; i < vulkan->swapchain.images.size(); ++i)
		{
			uint32_t width = renderTargets["viewport"].width.Value();
			uint32_t height = renderTargets["viewport"].height.Value();
			vk::ImageView view = renderTargets["viewport"].view.Value();
			compositionFramebuffers[i].Create(width, height, view, compositionRenderPass);
		}
	}

	void Deferred::createPipelines(std::map<std::string, Image>& renderTargets)
	{
		createGBufferPipeline(renderTargets);
		createCompositionPipeline(renderTargets);
	}

	void Deferred::createGBufferPipeline(std::map<std::string, Image>& renderTargets)
	{
		Shader vert{ "shaders/Deferred/gBuffer.vert", ShaderType::Vertex, true };
		Shader frag{ "shaders/Deferred/gBuffer.frag", ShaderType::Fragment, true };

		pipeline.info.pVertShader = &vert;
		pipeline.info.pFragShader = &frag;
		pipeline.info.vertexInputBindingDescriptions = Vertex::getBindingDescriptionGeneral();
		pipeline.info.vertexInputAttributeDescriptions = Vertex::getAttributeDescriptionGeneral();
		pipeline.info.width = renderTargets["albedo"].width_f.Value();
		pipeline.info.height = renderTargets["albedo"].height_f.Value();
		pipeline.info.cullMode = CullMode::Front;
		pipeline.info.colorBlendAttachments = {
				renderTargets["depth"].blentAttachment.Value(),
				renderTargets["normal"].blentAttachment.Value(),
				renderTargets["albedo"].blentAttachment.Value(),
				renderTargets["srm"].blentAttachment.Value(),
				renderTargets["velocity"].blentAttachment.Value(),
				renderTargets["emissive"].blentAttachment.Value(),
		};
		pipeline.info.descriptorSetLayouts = {
			Pipeline::getDescriptorSetLayoutMesh(),
			Pipeline::getDescriptorSetLayoutPrimitive(),
			Pipeline::getDescriptorSetLayoutModel()
		};
		pipeline.info.renderPass = renderPass;

		pipeline.createGraphicsPipeline();
	}

	void Deferred::createCompositionPipeline(std::map<std::string, Image>& renderTargets)
	{
		Shader vert{ "shaders/Deferred/composition.vert", ShaderType::Vertex, true };
		Shader frag{ "shaders/Deferred/composition.frag", ShaderType::Fragment, true };

		pipelineComposition.info.pVertShader = &vert;
		pipelineComposition.info.pFragShader = &frag;
		pipelineComposition.info.width = renderTargets["viewport"].width_f.Value();
		pipelineComposition.info.height = renderTargets["viewport"].height_f.Value();
		pipelineComposition.info.cullMode = CullMode::Back;
		pipelineComposition.info.colorBlendAttachments = {
			renderTargets["viewport"].blentAttachment.Value()
		};
		pipelineComposition.info.descriptorSetLayouts = {
			Pipeline::getDescriptorSetLayoutComposition(),
			Pipeline::getDescriptorSetLayoutShadows(),
			Pipeline::getDescriptorSetLayoutShadows(),
			Pipeline::getDescriptorSetLayoutShadows(),
			Pipeline::getDescriptorSetLayoutSkybox()
		};
		pipelineComposition.info.renderPass = compositionRenderPass;

		pipelineComposition.createGraphicsPipeline();
	}

	void Deferred::destroy()
	{
		auto vulkan = VulkanContext::get();

		renderPass.Destroy();
		compositionRenderPass.Destroy();

		for (auto& framebuffer : framebuffers)
			framebuffer.Destroy();
		for (auto& framebuffer : compositionFramebuffers)
			framebuffer.Destroy();

		if (Pipeline::getDescriptorSetLayoutComposition()) {
			vulkan->device->destroyDescriptorSetLayout(Pipeline::getDescriptorSetLayoutComposition());
			Pipeline::getDescriptorSetLayoutComposition() = nullptr;
		}
		uniform.destroy();
		pipeline.destroy();
		pipelineComposition.destroy();
	}
}
