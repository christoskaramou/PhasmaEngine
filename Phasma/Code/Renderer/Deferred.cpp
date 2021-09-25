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
#include "../Model/Model.h"
#include "../Model/Mesh.h"
#include "Swapchain.h"
#include "Surface.h"
#include "../Shader/Shader.h"
#include "../Core/Queue.h"
#include "../GUI/GUI.h"
#include "tinygltf/stb_image.h"
#include <deque>
#include "../Shader/Reflection.h"
#include "RenderApi.h"
#include "../Core/Path.h"
#include "../ECS/Context.h"

namespace pe
{
	Deferred::Deferred()
	{
		DSComposition = make_ref(vk::DescriptorSet());
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
		
		std::vector<vk::ClearValue> clearValues = {
				clearColor, clearColor, clearColor, clearColor, clearColor, clearColor, depthStencil
		};
		
		vk::RenderPassBeginInfo rpi;
		rpi.renderPass = *renderPass.handle;
		rpi.framebuffer = *framebuffers[imageIndex].handle;
		rpi.renderArea.offset = vk::Offset2D {0, 0};
		rpi.renderArea.extent = extent;
		rpi.clearValueCount = static_cast<uint32_t>(clearValues.size());
		rpi.pClearValues = clearValues.data();
		
		cmd.beginRenderPass(rpi, vk::SubpassContents::eInline);
		
		*Model::commandBuffer = cmd;
		Model::pipeline = &pipeline;
	}
	
	void Deferred::batchEnd()
	{
		Model::commandBuffer->endRenderPass();
		*Model::commandBuffer = nullptr;
		Model::pipeline = nullptr;
	}
	
	void Deferred::createDeferredUniforms(std::map<std::string, Image>& renderTargets)
	{
		uniform.CreateBuffer(
			sizeof(ubo),
			(BufferUsageFlags)vk::BufferUsageFlagBits::eUniformBuffer,
			(MemoryPropertyFlags)vk::MemoryPropertyFlagBits::eHostVisible);
		uniform.Map();
		uniform.Zero();
		uniform.Flush();
		uniform.Unmap();
		uniform.SetDebugName("Deferred_UB");
		
		auto vulkan = VulkanContext::Get();
		const vk::DescriptorSetAllocateInfo allocInfo =
				vk::DescriptorSetAllocateInfo {
						*vulkan->descriptorPool,            //DescriptorPool descriptorPool;
						1,                                        //uint32_t descriptorSetCount;
						&Pipeline::getDescriptorSetLayoutComposition() //const DescriptorSetLayout* pSetLayouts;
				};
		DSComposition = make_ref(vulkan->device->allocateDescriptorSets(allocInfo).at(0));
		VulkanContext::Get()->SetDebugObjectName(*DSComposition, "Composition");
		
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
			const vk::DeviceSize imageSize = texWidth * texHeight * STBI_rgb_alpha;
			
			vulkan->graphicsQueue->waitIdle();
			vulkan->waitAndLockSubmits();
			
			Buffer staging;
			staging.CreateBuffer(
				imageSize,
				(BufferUsageFlags)vk::BufferUsageFlagBits::eTransferSrc,
				(MemoryPropertyFlags)vk::MemoryPropertyFlagBits::eHostVisible);
			staging.Map();
			staging.CopyData(pixels);
			staging.Flush();
			staging.Unmap();
			staging.SetDebugName("Staging");
			
			stbi_image_free(pixels);
			
			ibl_brdf_lut.format = make_ref(vk::Format::eR8G8B8A8Unorm);
			ibl_brdf_lut.mipLevels =
					static_cast<uint32_t>(std::floor(std::log2(texWidth > texHeight ? texWidth : texHeight))) + 1;
			ibl_brdf_lut.createImage(
					texWidth, texHeight, vk::ImageTiling::eOptimal,
					vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst |
					vk::ImageUsageFlagBits::eSampled, vk::MemoryPropertyFlagBits::eDeviceLocal
			);
			ibl_brdf_lut.transitionImageLayout(vk::ImageLayout::ePreinitialized, vk::ImageLayout::eTransferDstOptimal);
			ibl_brdf_lut.copyBufferToImage(*staging.GetBufferVK());
			ibl_brdf_lut.generateMipMaps();
			ibl_brdf_lut.createImageView(vk::ImageAspectFlagBits::eColor);
			ibl_brdf_lut.maxLod = static_cast<float>(ibl_brdf_lut.mipLevels);
			ibl_brdf_lut.createSampler();
			ibl_brdf_lut.SetDebugName("ibl_brdf_lut");
			
			staging.Destroy();
			
			vulkan->unlockSubmits();
			
			Mesh::uniqueTextures[path] = ibl_brdf_lut;
		}
		
		updateDescriptorSets(renderTargets);
	}
	
	void Deferred::updateDescriptorSets(std::map<std::string, Image>& renderTargets)
	{
		std::deque<vk::DescriptorImageInfo> dsii {};
		auto const wSetImage = [&dsii](const vk::DescriptorSet& dstSet, uint32_t dstBinding, Image& image)
		{
			dsii.emplace_back(*image.sampler, *image.view, vk::ImageLayout::eShaderReadOnlyOptimal);
			return vk::WriteDescriptorSet {
					dstSet, dstBinding, 0, 1, vk::DescriptorType::eCombinedImageSampler, &dsii.back(), nullptr, nullptr
			};
		};
		std::deque<vk::DescriptorBufferInfo> dsbi {};
		auto const wSetBuffer = [&dsbi](const vk::DescriptorSet& dstSet, uint32_t dstBinding, Buffer& buffer)
		{
			dsbi.emplace_back(*buffer.GetBufferVK(), 0, buffer.Size());
			return vk::WriteDescriptorSet {
					dstSet, dstBinding, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &dsbi.back(), nullptr
			};
		};
		
		Buffer& lightsUniform = Context::Get()->GetSystem<LightSystem>()->GetUniform();
		std::vector<vk::WriteDescriptorSet> writeDescriptorSets = {
				wSetImage(*DSComposition, 0, renderTargets["depth"]),
				wSetImage(*DSComposition, 1, renderTargets["normal"]),
				wSetImage(*DSComposition, 2, renderTargets["albedo"]),
				wSetImage(*DSComposition, 3, renderTargets["srm"]),
				wSetBuffer(*DSComposition, 4, lightsUniform),
				wSetImage(*DSComposition, 5, renderTargets["ssaoBlur"]),
				wSetImage(*DSComposition, 6, renderTargets["ssr"]),
				wSetImage(*DSComposition, 7, renderTargets["emissive"]),
				wSetImage(*DSComposition, 8, ibl_brdf_lut),
				wSetBuffer(*DSComposition, 9, uniform)
		};
		
		VulkanContext::Get()->device->updateDescriptorSets(writeDescriptorSets, nullptr);
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
		
		Queue::memcpyRequest(&uniform, {{&ubo, sizeof(ubo), 0}});
		//uniform.map();
		//uniform.copyData(&ubo);
		//uniform.flush();
		//uniform.unmap();
	}
	
	void Deferred::draw(
			vk::CommandBuffer cmd, uint32_t imageIndex, Shadows& shadows, SkyBox& skybox, const vk::Extent2D& extent
	)
	{
		// Begin Composition
		vk::ClearValue clearColor;
		memcpy(clearColor.color.float32, GUI::clearColor.data(), 4 * sizeof(float));
		
		std::vector<vk::ClearValue> clearValues = {clearColor, clearColor};
		
		vk::RenderPassBeginInfo rpi;
		rpi.renderPass = *compositionRenderPass.handle;
		rpi.framebuffer = *compositionFramebuffers[imageIndex].handle;
		rpi.renderArea.offset = vk::Offset2D {0, 0};
		rpi.renderArea.extent = extent;
		rpi.clearValueCount = static_cast<uint32_t>(clearValues.size());
		rpi.pClearValues = clearValues.data();
		cmd.beginRenderPass(rpi, vk::SubpassContents::eInline);
		
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipelineComposition.handle);
		cmd.bindDescriptorSets(
				vk::PipelineBindPoint::eGraphics, *pipelineComposition.layout, 0, {
						*DSComposition, (*shadows.descriptorSets)[0], (*shadows.descriptorSets)[1],
						(*shadows.descriptorSets)[2], *skybox.descriptorSet
				}, nullptr
		);
		cmd.draw(3, 1, 0, 0);
		cmd.endRenderPass();
		// End Composition
	}
	
	void Deferred::createRenderPasses(std::map<std::string, Image>& renderTargets)
	{
		std::vector<vk::Format> formats
				{
						*renderTargets["depth"].format,
						*renderTargets["normal"].format,
						*renderTargets["albedo"].format,
						*renderTargets["srm"].format,
						*renderTargets["velocity"].format,
						*renderTargets["emissive"].format
				};
		renderPass.Create(formats, *VulkanContext::Get()->depth.format);
		compositionRenderPass.Create(*renderTargets["viewport"].format, vk::Format::eUndefined);
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
			std::vector<vk::ImageView> views = {
					*renderTargets["depth"].view,
					*renderTargets["normal"].view,
					*renderTargets["albedo"].view,
					*renderTargets["srm"].view,
					*renderTargets["velocity"].view,
					*renderTargets["emissive"].view,
					*VulkanContext::Get()->depth.view
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
			vk::ImageView view = *renderTargets["viewport"].view;
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
		Shader vert {"Shaders/Deferred/gBuffer.vert", ShaderType::Vertex, true};
		Shader frag {"Shaders/Deferred/gBuffer.frag", ShaderType::Fragment, true};
		
		pipeline.info.pVertShader = &vert;
		pipeline.info.pFragShader = &frag;
		pipeline.info.vertexInputBindingDescriptions = make_ref(Vertex::getBindingDescriptionGeneral());
		pipeline.info.vertexInputAttributeDescriptions = make_ref(Vertex::getAttributeDescriptionGeneral());
		pipeline.info.width = renderTargets["albedo"].width_f;
		pipeline.info.height = renderTargets["albedo"].height_f;
		pipeline.info.cullMode = CullMode::Front;
		pipeline.info.colorBlendAttachments = make_ref(
				std::vector<vk::PipelineColorBlendAttachmentState>
						{
								*renderTargets["depth"].blentAttachment,
								*renderTargets["normal"].blentAttachment,
								*renderTargets["albedo"].blentAttachment,
								*renderTargets["srm"].blentAttachment,
								*renderTargets["velocity"].blentAttachment,
								*renderTargets["emissive"].blentAttachment,
						}
		);
		pipeline.info.descriptorSetLayouts = make_ref(
				std::vector<vk::DescriptorSetLayout>
						{
								Pipeline::getDescriptorSetLayoutMesh(),
								Pipeline::getDescriptorSetLayoutPrimitive(),
								Pipeline::getDescriptorSetLayoutModel()
						}
		);
		pipeline.info.renderPass = renderPass;
		
		pipeline.createGraphicsPipeline();
	}
	
	void Deferred::createCompositionPipeline(std::map<std::string, Image>& renderTargets)
	{
		Shader vert {"Shaders/Deferred/composition.vert", ShaderType::Vertex, true};
		Shader frag {"Shaders/Deferred/composition.frag", ShaderType::Fragment, true};
		
		pipelineComposition.info.pVertShader = &vert;
		pipelineComposition.info.pFragShader = &frag;
		pipelineComposition.info.width = renderTargets["viewport"].width_f;
		pipelineComposition.info.height = renderTargets["viewport"].height_f;
		pipelineComposition.info.cullMode = CullMode::Back;
		pipelineComposition.info.colorBlendAttachments = make_ref(
				std::vector<vk::PipelineColorBlendAttachmentState> {
						*renderTargets["viewport"].blentAttachment
				}
		);
		pipelineComposition.info.descriptorSetLayouts = make_ref(
				std::vector<vk::DescriptorSetLayout>
						{
								Pipeline::getDescriptorSetLayoutComposition(),
								Pipeline::getDescriptorSetLayoutShadows(),
								Pipeline::getDescriptorSetLayoutShadows(),
								Pipeline::getDescriptorSetLayoutShadows(),
								Pipeline::getDescriptorSetLayoutSkybox()
						}
		);
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
			vulkan->device->destroyDescriptorSetLayout(Pipeline::getDescriptorSetLayoutComposition());
			Pipeline::getDescriptorSetLayoutComposition() = nullptr;
		}
		uniform.Destroy();
		pipeline.destroy();
		pipelineComposition.destroy();
	}
}
