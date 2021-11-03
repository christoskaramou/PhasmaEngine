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
#include "Renderer/Command.h"
#include "Renderer/Descriptor.h"
#include "Renderer/Framebuffer.h"
#include "Renderer/Image.h"
#include "Renderer/RenderPass.h"
#include "Renderer/Pipeline.h"

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
		// Previous Image
		ImageCreateInfo info{};
		info.format = RHII.surface->format;
		info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		info.width = static_cast<uint32_t>(WIDTH_f * GUI::renderTargetsScale);
		info.height = static_cast<uint32_t>(HEIGHT_f * GUI::renderTargetsScale);
		info.tiling = VK_IMAGE_TILING_OPTIMAL;
		info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		info.properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		previous = Image::Create(info);

		ImageViewCreateInfo viewInfo{};
		viewInfo.image = previous;
		previous->CreateImageView(viewInfo);

		SamplerCreateInfo samplerInfo{};
		previous->CreateSampler(samplerInfo);

		previous->TransitionImageLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		
		// Frame Image
		ImageCreateInfo info1{};
		info1.format = RHII.surface->format;
		info1.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		info1.width = static_cast<uint32_t>(WIDTH_f * GUI::renderTargetsScale);
		info1.height = static_cast<uint32_t>(HEIGHT_f * GUI::renderTargetsScale);
		info1.tiling = VK_IMAGE_TILING_OPTIMAL;
		info1.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		info1.properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		frameImage = Image::Create(info1);

		ImageViewCreateInfo viewInfo1{};
		viewInfo1.image = frameImage;
		frameImage->CreateImageView(viewInfo1);

		SamplerCreateInfo samplerInfo1{};
		frameImage->CreateSampler(samplerInfo1);

		frameImage->TransitionImageLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
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
	
	void TAA::createUniforms(std::map<std::string, Image*>& renderTargets)
	{
		uniform = Buffer::Create(
			sizeof(UBO),
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
		uniform->Map();
		uniform->Zero();
		uniform->Flush();
		uniform->Unmap();

		DSet = Descriptor::Create(Pipeline::getDescriptorSetLayoutTAA());
		DSetSharpen = Descriptor::Create(Pipeline::getDescriptorSetLayoutTAASharpen());

		updateDescriptorSets(renderTargets);
	}
	
	void TAA::updateDescriptorSets(std::map<std::string, Image*>& renderTargets)
	{
		std::array<DescriptorUpdateInfo, 5> infos{};

		infos[0].binding = 0;
		infos[0].pImage = previous;

		infos[1].binding = 1;
		infos[1].pImage = frameImage;

		infos[2].binding = 2;
		infos[2].pImage = RHII.depth;
		infos[2].imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

		infos[3].binding = 3;
		infos[3].pImage = renderTargets["velocity"];

		infos[4].binding = 4;
		infos[4].pBuffer = uniform;

		DSet->UpdateDescriptor(5, infos.data());

		std::array<DescriptorUpdateInfo, 2> infos2{};

		infos2[0].binding = 0;
		infos2[0].pImage = renderTargets["taa"];

		infos2[1].binding = 1;
		infos2[1].pBuffer = uniform;

		DSetSharpen->UpdateDescriptor(2, infos2.data());
	}
	
	void TAA::draw(CommandBuffer* cmd, uint32_t imageIndex, std::map<std::string, Image*>& renderTargets)
	{
		
		renderTargets["taa"]->ChangeLayout(cmd, LayoutState::ColorWrite);
		cmd->BeginPass(renderPass, framebuffers[imageIndex]);
		cmd->BindPipeline(pipeline);
		cmd->BindDescriptors(pipeline, 1, &DSet);
		cmd->Draw(3, 1, 0, 0);
		cmd->EndPass();
		renderTargets["taa"]->ChangeLayout(cmd, LayoutState::ColorRead);
		
		saveImage(cmd, renderTargets["taa"]);

		cmd->BeginPass(renderPassSharpen, framebuffersSharpen[imageIndex]);
		cmd->BindPipeline(pipelineSharpen);
		cmd->BindDescriptors(pipelineSharpen, 1, &DSetSharpen);
		cmd->Draw(3, 1, 0, 0);
		cmd->EndPass();
	}
	
	void TAA::createRenderPasses(std::map<std::string, Image*>& renderTargets)
	{
		Attachment attachment{};
		attachment.format = renderTargets["taa"]->imageInfo.format;
		renderPass = RenderPass::Create(attachment);

		attachment.format = renderTargets["viewport"]->imageInfo.format;
		renderPassSharpen = RenderPass::Create(attachment);
	}
	
	void TAA::createFrameBuffers(std::map<std::string, Image*>& renderTargets)
	{
		framebuffers.resize(RHII.swapchain->images.size());
		for (size_t i = 0; i < RHII.swapchain->images.size(); ++i)
		{
			uint32_t width = renderTargets["taa"]->imageInfo.width;
			uint32_t height = renderTargets["taa"]->imageInfo.height;
			ImageViewHandle view = renderTargets["taa"]->view;
			framebuffers[i] = FrameBuffer::Create(width, height, view, renderPass);
		}
		
		framebuffersSharpen.resize(RHII.swapchain->images.size());
		for (size_t i = 0; i < RHII.swapchain->images.size(); ++i)
		{
			uint32_t width = renderTargets["viewport"]->imageInfo.width;
			uint32_t height = renderTargets["viewport"]->imageInfo.height;
			ImageViewHandle view = renderTargets["viewport"]->view;
			framebuffersSharpen[i] = FrameBuffer::Create(width, height, view, renderPassSharpen);
		}
	}
	
	void TAA::createPipelines(std::map<std::string, Image*>& renderTargets)
	{
		createPipeline(renderTargets);
		createPipelineSharpen(renderTargets);
	}
	
	void TAA::createPipeline(std::map<std::string, Image*>& renderTargets)
	{
		Shader vert {"Shaders/Common/quad.vert", ShaderType::Vertex, true};
		Shader frag {"Shaders/TAA/TAA.frag", ShaderType::Fragment, true};
		
		PipelineCreateInfo info{};
		info.pVertShader = &vert;
		info.pFragShader = &frag;
		info.width = renderTargets["taa"]->width_f;
		info.height = renderTargets["taa"]->height_f;
		info.cullMode = CullMode::Back;
		info.colorBlendAttachments = { renderTargets["taa"]->blendAttachment };
		info.descriptorSetLayouts = { Pipeline::getDescriptorSetLayoutTAA() };
		info.renderPass = renderPass;
		
		pipeline = Pipeline::Create(info);
	}
	
	void TAA::createPipelineSharpen(std::map<std::string, Image*>& renderTargets)
	{
		Shader vert {"Shaders/Common/quad.vert", ShaderType::Vertex, true};
		Shader frag {"Shaders/TAA/TAASharpen.frag", ShaderType::Fragment, true};

		PipelineCreateInfo info{};
		info.pVertShader = &vert;
		info.pFragShader = &frag;
		info.width = renderTargets["viewport"]->width_f;
		info.height = renderTargets["viewport"]->height_f;
		info.cullMode = CullMode::Back;
		info.colorBlendAttachments = { renderTargets["viewport"]->blendAttachment };
		info.descriptorSetLayouts = { Pipeline::getDescriptorSetLayoutTAASharpen() };
		info.renderPass = renderPassSharpen;
		
		pipelineSharpen = Pipeline::Create(info);
	}
	
	void TAA::saveImage(CommandBuffer* cmd, Image* source)
	{
		previous->TransitionImageLayout(
			cmd,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_ACCESS_SHADER_READ_BIT,
			VK_ACCESS_TRANSFER_WRITE_BIT
		);
		source->TransitionImageLayout(
			cmd,
			source->imageInfo.layoutState == LayoutState::ColorRead ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			source->imageInfo.layoutState == LayoutState::ColorRead ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			source->imageInfo.layoutState == LayoutState::ColorRead ? VK_ACCESS_SHADER_READ_BIT : VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			VK_ACCESS_TRANSFER_READ_BIT
		);
		
		// copy the image
		VkImageCopy region{};
		region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.srcSubresource.layerCount = 1;
		region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.dstSubresource.layerCount = 1;
		region.extent.width = source->imageInfo.width;
		region.extent.height = source->imageInfo.height;
		region.extent.depth = 1;
		
		vkCmdCopyImage(
			cmd->Handle(),
			source->Handle(),
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			previous->Handle(),
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1, &region
		);
		
		previous->TransitionImageLayout(
			cmd,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			VK_ACCESS_TRANSFER_WRITE_BIT,
			VK_ACCESS_SHADER_READ_BIT
		);
		source->TransitionImageLayout(
			cmd,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			VK_ACCESS_TRANSFER_READ_BIT,
			VK_ACCESS_SHADER_READ_BIT
		);
		source->imageInfo.layoutState = LayoutState::ColorRead;
	}
	
	void TAA::destroy()
	{
		uniform->Destroy();
		previous->Destroy();
		frameImage->Destroy();
		
		for (auto framebuffer : framebuffers)
			framebuffer->Destroy();
		for (auto framebuffer : framebuffersSharpen)
			framebuffer->Destroy();
		
		renderPass->Destroy();
		renderPassSharpen->Destroy();
		
		Pipeline::getDescriptorSetLayoutTAA()->Destroy();
		Pipeline::getDescriptorSetLayoutTAASharpen()->Destroy();
		pipeline->Destroy();
		pipelineSharpen->Destroy();
	}
}
