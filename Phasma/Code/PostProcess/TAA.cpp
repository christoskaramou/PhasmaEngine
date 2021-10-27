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

#include "TAA.h"
#include "GUI/GUI.h"
#include "Renderer/Surface.h"
#include "Renderer/Swapchain.h"
#include "Shader/Shader.h"
#include "Core/Queue.h"
#include "Renderer/RHI.h"
#include "Renderer/CommandBuffer.h"

namespace pe
{
	TAA::TAA()
	{
		DSet = {};
		DSetSharpen = {};
	}
	
	TAA::~TAA()
	{
	}
	
	void TAA::Init()
	{
		previous.format = RHII.surface.format;
		previous.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		previous.CreateImage(
			static_cast<uint32_t>(WIDTH_f * GUI::renderTargetsScale),
			static_cast<uint32_t>(HEIGHT_f * GUI::renderTargetsScale),
			VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
		);
		previous.TransitionImageLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		previous.CreateImageView(VK_IMAGE_ASPECT_COLOR_BIT);
		previous.CreateSampler();
		
		frameImage.format = RHII.surface.format;
		frameImage.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		frameImage.CreateImage(
			static_cast<uint32_t>(WIDTH_f * GUI::renderTargetsScale),
			static_cast<uint32_t>(HEIGHT_f * GUI::renderTargetsScale),
			VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
		);
		frameImage.TransitionImageLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		frameImage.CreateImageView(VK_IMAGE_ASPECT_COLOR_BIT);
		frameImage.CreateSampler();
	}
	
	void TAA::update(const Camera& camera)
	{
		if (GUI::use_TAA)
		{
			ubo.values = {camera.projOffset.x, camera.projOffset.y, GUI::TAA_feedback_min, GUI::TAA_feedback_max};
			ubo.sharpenValues = {
					GUI::TAA_sharp_strength, GUI::TAA_sharp_clamp, GUI::TAA_sharp_offset_bias,
					sin(static_cast<float>(ImGui::GetTime()) * 0.125f)
			};
			ubo.invProj = camera.invProjection;
			
			uniform->CopyRequest<Launch::AsyncDeferred>({ &ubo, sizeof(ubo), 0 });
		}
	}
	
	void TAA::createUniforms(std::map<std::string, Image>& renderTargets)
	{
		uniform = Buffer::Create(
			sizeof(UBO),
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
		uniform->Map();
		uniform->Zero();
		uniform->Flush();
		uniform->Unmap();

		VkDescriptorSetAllocateInfo allocateInfo{};
		allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocateInfo.descriptorPool = RHII.descriptorPool;
		allocateInfo.descriptorSetCount = 1;

		VkDescriptorSet dset;

		VkDescriptorSetLayout dsetLayout = Pipeline::getDescriptorSetLayoutTAA();
		allocateInfo.pSetLayouts = &dsetLayout;
		vkAllocateDescriptorSets(RHII.device, &allocateInfo, &dset);
		DSet = dset;

		dsetLayout = Pipeline::getDescriptorSetLayoutTAASharpen();
		allocateInfo.pSetLayouts = &dsetLayout;
		vkAllocateDescriptorSets(RHII.device, &allocateInfo, &dset);
		DSetSharpen = dset;

		updateDescriptorSets(renderTargets);
	}
	
	void TAA::updateDescriptorSets(std::map<std::string, Image>& renderTargets)
	{
		std::deque<VkDescriptorImageInfo> dsii{};
		auto const wSetImage = [&dsii](DescriptorSetHandle dstSet, uint32_t dstBinding, Image& image, ImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
		{
			VkDescriptorImageInfo info{ image.sampler, image.view, (VkImageLayout)layout };
			dsii.push_back(info);

			VkWriteDescriptorSet textureWriteSet{};
			textureWriteSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			textureWriteSet.dstSet = dstSet;
			textureWriteSet.dstBinding = dstBinding;
			textureWriteSet.dstArrayElement = 0;
			textureWriteSet.descriptorCount = 1;
			textureWriteSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			textureWriteSet.pImageInfo = &dsii.back();

			return textureWriteSet;
		};

		std::deque<VkDescriptorBufferInfo> dsbi{};
		auto const wSetBuffer = [&dsbi](DescriptorSetHandle dstSet, uint32_t dstBinding, Buffer& buffer)
		{
			VkDescriptorBufferInfo info{ buffer.Handle(), 0, buffer.Size() };
			dsbi.push_back(info);

			VkWriteDescriptorSet textureWriteSet{};
			textureWriteSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			textureWriteSet.dstSet = dstSet;
			textureWriteSet.dstBinding = dstBinding;
			textureWriteSet.dstArrayElement = 0;
			textureWriteSet.descriptorCount = 1;
			textureWriteSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			textureWriteSet.pBufferInfo = &dsbi.back();

			return textureWriteSet;
		};
		
		std::vector<VkWriteDescriptorSet> textureWriteSets
		{
			wSetImage(DSet, 0, previous),
			wSetImage(DSet, 1, frameImage),
			wSetImage(DSet, 2, RHII.depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL),
			wSetImage(DSet, 3, renderTargets["velocity"]),
			wSetBuffer(DSet, 4, *uniform),
			wSetImage(DSetSharpen, 0, renderTargets["taa"]),
			wSetBuffer(DSetSharpen, 1, *uniform)
		};

		vkUpdateDescriptorSets(RHII.device, (uint32_t)textureWriteSets.size(), textureWriteSets.data(), 0, nullptr);
	}
	
	void TAA::draw(CommandBuffer* cmd, uint32_t imageIndex, std::map<std::string, Image>& renderTargets)
	{
		
		renderTargets["taa"].ChangeLayout(*cmd, LayoutState::ColorWrite);
		cmd->BeginPass(renderPass, framebuffers[imageIndex]);
		cmd->BindPipeline(pipeline);
		cmd->BindDescriptors(pipeline, 1, &DSet);
		cmd->Draw(3, 1, 0, 0);
		cmd->EndPass();
		renderTargets["taa"].ChangeLayout(*cmd, LayoutState::ColorRead);
		
		saveImage(cmd, renderTargets["taa"]);

		cmd->BeginPass(renderPassSharpen, framebuffersSharpen[imageIndex]);
		cmd->BindPipeline(pipelineSharpen);
		cmd->BindDescriptors(pipelineSharpen, 1, &DSetSharpen);
		cmd->Draw(3, 1, 0, 0);
		cmd->EndPass();
	}
	
	void TAA::createRenderPasses(std::map<std::string, Image>& renderTargets)
	{
		Attachment attachment{};
		attachment.format = renderTargets["taa"].format;
		renderPass.Create(attachment);

		attachment.format = renderTargets["viewport"].format;
		renderPassSharpen.Create(attachment);
	}
	
	void TAA::createFrameBuffers(std::map<std::string, Image>& renderTargets)
	{
		framebuffers.resize(RHII.swapchain.images.size());
		for (size_t i = 0; i < RHII.swapchain.images.size(); ++i)
		{
			uint32_t width = renderTargets["taa"].width;
			uint32_t height = renderTargets["taa"].height;
			ImageViewHandle view = renderTargets["taa"].view;
			framebuffers[i].Create(width, height, view, renderPass);
		}
		
		framebuffersSharpen.resize(RHII.swapchain.images.size());
		for (size_t i = 0; i < RHII.swapchain.images.size(); ++i)
		{
			uint32_t width = renderTargets["viewport"].width;
			uint32_t height = renderTargets["viewport"].height;
			ImageViewHandle view = renderTargets["viewport"].view;
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
		Shader vert {"Shaders/Common/quad.vert", ShaderType::Vertex, true};
		Shader frag {"Shaders/TAA/TAA.frag", ShaderType::Fragment, true};
		
		pipeline.info.pVertShader = &vert;
		pipeline.info.pFragShader = &frag;
		pipeline.info.width = renderTargets["taa"].width_f;
		pipeline.info.height = renderTargets["taa"].height_f;
		pipeline.info.cullMode = CullMode::Back;
		pipeline.info.colorBlendAttachments = { renderTargets["taa"].blendAttachment };
		pipeline.info.descriptorSetLayouts = { Pipeline::getDescriptorSetLayoutTAA() };
		pipeline.info.renderPass = renderPass;
		
		pipeline.createGraphicsPipeline();
	}
	
	void TAA::createPipelineSharpen(std::map<std::string, Image>& renderTargets)
	{
		Shader vert {"Shaders/Common/quad.vert", ShaderType::Vertex, true};
		Shader frag {"Shaders/TAA/TAASharpen.frag", ShaderType::Fragment, true};
		
		pipelineSharpen.info.pVertShader = &vert;
		pipelineSharpen.info.pFragShader = &frag;
		pipelineSharpen.info.width = renderTargets["viewport"].width_f;
		pipelineSharpen.info.height = renderTargets["viewport"].height_f;
		pipelineSharpen.info.cullMode = CullMode::Back;
		pipelineSharpen.info.colorBlendAttachments = { renderTargets["viewport"].blendAttachment };
		pipelineSharpen.info.descriptorSetLayouts = { Pipeline::getDescriptorSetLayoutTAASharpen() };
		pipelineSharpen.info.renderPass = renderPassSharpen;
		
		pipelineSharpen.createGraphicsPipeline();
	}
	
	void TAA::saveImage(CommandBuffer* cmd, Image& source)
	{
		previous.TransitionImageLayout(
			*cmd,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_ACCESS_SHADER_READ_BIT,
			VK_ACCESS_TRANSFER_WRITE_BIT,
			VK_IMAGE_ASPECT_COLOR_BIT
		);
		source.TransitionImageLayout(
			*cmd,
			source.layoutState == LayoutState::ColorRead ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			source.layoutState == LayoutState::ColorRead ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			source.layoutState == LayoutState::ColorRead ? VK_ACCESS_SHADER_READ_BIT : VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			VK_ACCESS_TRANSFER_READ_BIT,
			VK_IMAGE_ASPECT_COLOR_BIT
		);
		
		// copy the image
		VkImageCopy region{};
		region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.srcSubresource.layerCount = 1;
		region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.dstSubresource.layerCount = 1;
		region.extent.width = source.width;
		region.extent.height = source.height;
		region.extent.depth = 1;
		
		vkCmdCopyImage(
			cmd->Handle(),
			source.image,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			previous.image,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1, &region
		);
		
		previous.TransitionImageLayout(
			*cmd,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			VK_ACCESS_TRANSFER_WRITE_BIT,
			VK_ACCESS_SHADER_READ_BIT,
			VK_IMAGE_ASPECT_COLOR_BIT
		);
		source.TransitionImageLayout(
			*cmd,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			VK_ACCESS_TRANSFER_READ_BIT,
			VK_ACCESS_SHADER_READ_BIT,
			VK_IMAGE_ASPECT_COLOR_BIT
		);
		source.layoutState = LayoutState::ColorRead;
	}
	
	void TAA::destroy()
	{
		uniform->Destroy();
		previous.Destroy();
		frameImage.Destroy();
		
		for (auto& framebuffer : framebuffers)
			framebuffer.Destroy();
		for (auto& framebuffer : framebuffersSharpen)
			framebuffer.Destroy();
		
		renderPass.Destroy();
		renderPassSharpen.Destroy();
		
		if (Pipeline::getDescriptorSetLayoutTAA())
		{
			vkDestroyDescriptorSetLayout(RHII.device, Pipeline::getDescriptorSetLayoutTAA(), nullptr);
			Pipeline::getDescriptorSetLayoutTAA() = {};
		}
		if (Pipeline::getDescriptorSetLayoutTAASharpen())
		{
			vkDestroyDescriptorSetLayout(RHII.device, Pipeline::getDescriptorSetLayoutTAASharpen(), nullptr);
			Pipeline::getDescriptorSetLayoutTAASharpen() = {};
		}
		pipeline.destroy();
		pipelineSharpen.destroy();
	}
}
