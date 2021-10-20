/*
Copyright (c) 2018-2021 Christos Karamoustos

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include "PhasmaPch.h"
#include "Deferred.h"
#include "Model/Model.h"
#include "Model/Mesh.h"
#include "Swapchain.h"
#include "Surface.h"
#include "Shader/Shader.h"
#include "Core/Queue.h"
#include "GUI/GUI.h"
#include "tinygltf/stb_image.h"
#include <deque>
#include "Shader/Reflection.h"
#include "Renderer/Vulkan/Vulkan.h"
#include "Renderer/CommandBuffer.h"
#include "Core/Path.h"
#include "Core/Settings.h"
#include "ECS/Context.h"
#include "Systems/LightSystem.h"

namespace pe
{
	Deferred::Deferred()
	{
		DSComposition = {};
	}
	
	Deferred::~Deferred()
	{
	}
	
	void Deferred::batchStart(CommandBuffer* cmd, uint32_t imageIndex)
	{
		cmd->BeginPass(renderPass, framebuffers[imageIndex]);
		
		Model::commandBuffer = cmd;
		Model::pipeline = &pipeline;
	}
	
	void Deferred::batchEnd()
	{
		Model::commandBuffer->EndPass();
		Model::commandBuffer = nullptr;
		Model::pipeline = nullptr;
	}
	
	void Deferred::createDeferredUniforms(std::map<std::string, Image>& renderTargets)
	{
		uniform = Buffer::Create(
			sizeof(ubo),
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
		uniform->Map();
		uniform->Zero();
		uniform->Flush();
		uniform->Unmap();
		
		VkDescriptorSetLayout dsetLayout = Pipeline::getDescriptorSetLayoutComposition();
		VkDescriptorSetAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.pNext = nullptr;
		allocInfo.descriptorPool = *VULKAN.descriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &dsetLayout;

		VkDescriptorSet dset;
		vkAllocateDescriptorSets(*VULKAN.device, &allocInfo, &dset);
		DSComposition = dset;
		
		// Check if ibl_brdf_lut is already loaded
		const std::string path = Path::Assets + "Objects/ibl_brdf_lut.png";
		if (Mesh::uniqueTextures.find(path) != Mesh::uniqueTextures.end())
		{
			ibl_brdf_lut = Mesh::uniqueTextures[path];
		}
		else
		{
			int texWidth, texHeight, texChannels;
			stbi_set_flip_vertically_on_load(true);
			unsigned char* pixels = stbi_load(path.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
			stbi_set_flip_vertically_on_load(false);
			if (!pixels)
				throw std::runtime_error("No pixel data loaded");
			const VkDeviceSize imageSize = texWidth * texHeight * STBI_rgb_alpha;
			
			VULKAN.graphicsQueue->waitIdle();
			VULKAN.waitAndLockSubmits();
			
			SPtr<Buffer> staging = Buffer::Create(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
			staging->Map();
			staging->CopyData(pixels);
			staging->Flush();
			staging->Unmap();
			
			stbi_image_free(pixels);
			
			ibl_brdf_lut.format = (Format)VK_FORMAT_R8G8B8A8_UNORM;
			ibl_brdf_lut.mipLevels =
					static_cast<uint32_t>(std::floor(std::log2(texWidth > texHeight ? texWidth : texHeight))) + 1;
			ibl_brdf_lut.CreateImage(
				texWidth, texHeight, VK_IMAGE_TILING_OPTIMAL,
				VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
			);
			ibl_brdf_lut.TransitionImageLayout(VK_IMAGE_LAYOUT_PREINITIALIZED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
			ibl_brdf_lut.CopyBufferToImage(staging.get());
			ibl_brdf_lut.GenerateMipMaps();
			ibl_brdf_lut.CreateImageView(VK_IMAGE_ASPECT_COLOR_BIT);
			ibl_brdf_lut.maxLod = static_cast<float>(ibl_brdf_lut.mipLevels);
			ibl_brdf_lut.CreateSampler();
			
			staging->Destroy();
			
			VULKAN.unlockSubmits();
			
			Mesh::uniqueTextures[path] = ibl_brdf_lut;
		}
		
		updateDescriptorSets(renderTargets);
	}
	
	void Deferred::updateDescriptorSets(std::map<std::string, Image>& renderTargets)
	{
		std::deque<VkDescriptorImageInfo> dsii{};
		auto const wSetImage = [&dsii](DescriptorSetHandle dstSet, uint32_t dstBinding, Image& image, ImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
		{
			VkDescriptorImageInfo info{ image.sampler, image.view, (VkImageLayout)layout };
			dsii.push_back(info);

			VkWriteDescriptorSet writeSet{};
			writeSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writeSet.dstSet = dstSet;
			writeSet.dstBinding = dstBinding;
			writeSet.dstArrayElement = 0;
			writeSet.descriptorCount = 1;
			writeSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			writeSet.pImageInfo = &dsii.back();
			writeSet.pBufferInfo = nullptr;
			writeSet.pTexelBufferView = nullptr;

			return writeSet;
		};
		std::deque<VkDescriptorBufferInfo> dsbi{};
		const auto wSetBuffer = [&dsbi](DescriptorSetHandle dstSet, uint32_t dstBinding, Buffer& buffer)
		{
			VkDescriptorBufferInfo info{ buffer.Handle(), 0, buffer.Size() };
			dsbi.push_back(info);

			VkWriteDescriptorSet writeSet{};
			writeSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writeSet.dstSet = dstSet;
			writeSet.dstBinding = dstBinding;
			writeSet.dstArrayElement = 0;
			writeSet.descriptorCount = 1;
			writeSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			writeSet.pImageInfo = nullptr;
			writeSet.pBufferInfo = &dsbi.back();
			writeSet.pTexelBufferView = nullptr;

			return writeSet;
		};
		
		Buffer& lightsUniform = CONTEXT->GetSystem<LightSystem>()->GetUniform();
		std::vector<VkWriteDescriptorSet> writeDescriptorSets =
		{
			wSetImage(DSComposition, 0, VULKAN.depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL),
			wSetImage(DSComposition, 1, renderTargets["normal"]),
			wSetImage(DSComposition, 2, renderTargets["albedo"]),
			wSetImage(DSComposition, 3, renderTargets["srm"]),
			wSetBuffer(DSComposition, 4, lightsUniform),
			wSetImage(DSComposition, 5, renderTargets["ssaoBlur"]),
			wSetImage(DSComposition, 6, renderTargets["ssr"]),
			wSetImage(DSComposition, 7, renderTargets["emissive"]),
			wSetImage(DSComposition, 8, ibl_brdf_lut),
			wSetBuffer(DSComposition, 9, *uniform)
		};

		vkUpdateDescriptorSets(*VULKAN.device, (uint32_t)writeDescriptorSets.size(), writeDescriptorSets.data(), 0, nullptr);
	}
	
	void Deferred::update(mat4& invViewProj)
	{
		ubo.screenSpace[0] = {invViewProj[0]};
		ubo.screenSpace[1] = {invViewProj[1]};
		ubo.screenSpace[2] = {invViewProj[2]};
		ubo.screenSpace[3] = {invViewProj[3]};
		ubo.screenSpace[4] = {
				static_cast<float>(GUI::show_ssao), static_cast<float>(GUI::show_ssr),
				static_cast<float>(GUI::show_tonemapping), static_cast<float>(GUI::use_AntiAliasing)
		};
		ubo.screenSpace[5] = {
				static_cast<float>(GUI::use_IBL), static_cast<float>(GUI::use_Volumetric_lights),
				static_cast<float>(GUI::volumetric_steps), static_cast<float>(GUI::volumetric_dither_strength)
		};
		ubo.screenSpace[6] = {GUI::fog_global_thickness, GUI::lights_intensity, GUI::lights_range, GUI::fog_max_height};
		ubo.screenSpace[7] = {
				GUI::fog_ground_thickness, static_cast<float>(GUI::use_fog), static_cast<float>(GUI::shadow_cast), 0.0f
		};
		
		uniform->CopyRequest<Launch::AsyncDeferred>({ &ubo, sizeof(ubo), 0 });
	}
	
	void Deferred::draw(CommandBuffer* cmd, uint32_t imageIndex, Shadows& shadows, SkyBox& skybox)
	{
		mat4 values{};
		values[0] = shadows.viewZ;
		values[1] = vec4(GUI::shadow_cast);
		std::vector<DescriptorSetHandle> handles{ DSComposition, shadows.descriptorSetDeferred, skybox.descriptorSet };

		cmd->BeginPass(compositionRenderPass, compositionFramebuffers[imageIndex]);
		cmd->PushConstants(pipelineComposition, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(mat4), &values);
		cmd->BindPipeline(pipelineComposition);
		cmd->BindDescriptors(pipelineComposition, (uint32_t)handles.size(), handles.data());
		cmd->Draw(3, 1, 0, 0);
		cmd->EndPass();
	}
	
	void Deferred::createRenderPasses(std::map<std::string, Image>& renderTargets)
	{
		std::vector<Attachment> attachments(6);

		// Target attachments are initialized to match render targets by default
		attachments[0].format = renderTargets["normal"].format;
		attachments[1].format = renderTargets["albedo"].format;
		attachments[2].format = renderTargets["srm"].format;
		attachments[3].format = renderTargets["velocity"].format;
		attachments[4].format = renderTargets["emissive"].format;

		// Depth
		attachments[5].flags = {};
		attachments[5].format = VULKAN.depth.format;
		attachments[5].samples = VK_SAMPLE_COUNT_1_BIT;
		attachments[5].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachments[5].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachments[5].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachments[5].stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachments[5].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachments[5].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		renderPass.Create(attachments);

		Attachment attachment{};
		attachment.format = renderTargets["viewport"].format;
		compositionRenderPass.Create(attachment);
	}
	
	void Deferred::createFrameBuffers(std::map<std::string, Image>& renderTargets)
	{
		createGBufferFrameBuffers(renderTargets);
		createCompositionFrameBuffers(renderTargets);
	}
	
	void Deferred::createGBufferFrameBuffers(std::map<std::string, Image>& renderTargets)
	{
		auto vulkan = VulkanContext::Get();
		
		framebuffers.resize(vulkan->swapchain.images.size());
		for (size_t i = 0; i < vulkan->swapchain.images.size(); ++i)
		{
			uint32_t width = renderTargets["albedo"].width;
			uint32_t height = renderTargets["albedo"].height;
			std::vector<ImageViewHandle> views {
				renderTargets["normal"].view,
				renderTargets["albedo"].view,
				renderTargets["srm"].view,
				renderTargets["velocity"].view,
				renderTargets["emissive"].view,
				VULKAN.depth.view
			};
			framebuffers[i].Create(width, height, views, renderPass);
		}
	}
	
	void Deferred::createCompositionFrameBuffers(std::map<std::string, Image>& renderTargets)
	{
		auto vulkan = VulkanContext::Get();
		
		compositionFramebuffers.resize(vulkan->swapchain.images.size());
		for (size_t i = 0; i < vulkan->swapchain.images.size(); ++i)
		{
			uint32_t width = renderTargets["viewport"].width;
			uint32_t height = renderTargets["viewport"].height;
			ImageViewHandle view = renderTargets["viewport"].view;
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
		Shader vert{ "Shaders/Deferred/gBuffer.vert", ShaderType::Vertex, true };
		Shader frag{ "Shaders/Deferred/gBuffer.frag", ShaderType::Fragment, true };
		
		pipeline.info.pVertShader = &vert;
		pipeline.info.pFragShader = &frag;
		pipeline.info.vertexInputBindingDescriptions = Vertex::GetBindingDescriptionGeneral();
		pipeline.info.vertexInputAttributeDescriptions = Vertex::GetAttributeDescriptionGeneral();
		pipeline.info.width = renderTargets["albedo"].width_f;
		pipeline.info.height = renderTargets["albedo"].height_f;
		pipeline.info.cullMode = CullMode::Front;
		pipeline.info.colorBlendAttachments =
		{
			renderTargets["normal"].blendAttachment,
			renderTargets["albedo"].blendAttachment,
			renderTargets["srm"].blendAttachment,
			renderTargets["velocity"].blendAttachment,
			renderTargets["emissive"].blendAttachment,
		};
		pipeline.info.descriptorSetLayouts =
		{
			Pipeline::getDescriptorSetLayoutMesh(),
			Pipeline::getDescriptorSetLayoutPrimitive(),
			Pipeline::getDescriptorSetLayoutModel()
		};
		pipeline.info.renderPass = renderPass;
		
		pipeline.createGraphicsPipeline();
	}
	
	void Deferred::createCompositionPipeline(std::map<std::string, Image>& renderTargets)
	{
		const std::vector<Define> definesFrag
		{
			Define{ "SHADOWMAP_CASCADES",		std::to_string(SHADOWMAP_CASCADES) },
			Define{ "SHADOWMAP_SIZE",			std::to_string((float)SHADOWMAP_SIZE) },
			Define{ "SHADOWMAP_TEXEL_SIZE",		std::to_string(1.0f / (float)SHADOWMAP_SIZE) },
			Define{ "MAX_POINT_LIGHTS",			std::to_string(MAX_POINT_LIGHTS) },
			Define{ "MAX_SPOT_LIGHTS",			std::to_string(MAX_SPOT_LIGHTS) }
		};

		Shader vert{ "Shaders/Common/quad.vert", ShaderType::Vertex, true };
		Shader frag{ "Shaders/Deferred/composition.frag", ShaderType::Fragment, true, definesFrag };
		
		pipelineComposition.info.pVertShader = &vert;
		pipelineComposition.info.pFragShader = &frag;
		pipelineComposition.info.width = renderTargets["viewport"].width_f;
		pipelineComposition.info.height = renderTargets["viewport"].height_f;
		pipelineComposition.info.pushConstantStage = PushConstantStage::Fragment;
		pipelineComposition.info.pushConstantSize = sizeof(mat4);
		pipelineComposition.info.colorBlendAttachments = { renderTargets["viewport"].blendAttachment };
		pipelineComposition.info.descriptorSetLayouts =
		{
			Pipeline::getDescriptorSetLayoutComposition(),
			Pipeline::getDescriptorSetLayoutShadowsDeferred(),
			Pipeline::getDescriptorSetLayoutSkybox()
		};
		pipelineComposition.info.renderPass = compositionRenderPass;
		
		pipelineComposition.createGraphicsPipeline();
	}
	
	void Deferred::destroy()
	{
		auto vulkan = VulkanContext::Get();
		
		renderPass.Destroy();
		compositionRenderPass.Destroy();
		
		for (auto& framebuffer : framebuffers)
			framebuffer.Destroy();
		for (auto& framebuffer : compositionFramebuffers)
			framebuffer.Destroy();
		
		if (Pipeline::getDescriptorSetLayoutComposition())
		{
			vkDestroyDescriptorSetLayout(*VULKAN.device, Pipeline::getDescriptorSetLayoutComposition(), nullptr);
			Pipeline::getDescriptorSetLayoutComposition() = {};
		}
		uniform->Destroy();
		pipeline.destroy();
		pipelineComposition.destroy();
	}
}
