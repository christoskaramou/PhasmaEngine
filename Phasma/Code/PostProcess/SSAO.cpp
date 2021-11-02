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

#include "SSAO.h"
#include "Renderer/Swapchain.h"
#include "Renderer/Surface.h"
#include "GUI/GUI.h"
#include "Shader/Shader.h"
#include "Core/Queue.h"
#include "Renderer/RHI.h"
#include "Renderer/Command.h"
#include "Renderer/Descriptor.h"
#include "Renderer/FrameBuffer.h"

namespace pe
{
	void SSAO::createUniforms(std::map<std::string, Image>& renderTargets)
	{
		// kernel buffer
		std::vector<vec4> kernel {};
		for (unsigned i = 0; i < 16; i++)
		{
			vec3 sample(rand(-1.f, 1.f), rand(-1.f, 1.f), rand(0.f, 1.f));
			sample = normalize(sample);
			sample *= rand(0.f, 1.f);
			float scale = (float)i / 16.f;
			scale = lerp(.1f, 1.f, scale * scale);
			kernel.emplace_back(sample * scale, 0.f);
		}
		UB_Kernel = Buffer::Create(sizeof(vec4) * 16, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
		UB_Kernel->Map();
		UB_Kernel->CopyData(kernel.data());
		UB_Kernel->Flush();
		UB_Kernel->Unmap();
		
		// noise image
		std::vector<vec4> noise {};
		for (unsigned int i = 0; i < 16; i++)
			noise.emplace_back(rand(-1.f, 1.f), rand(-1.f, 1.f), 0.f, 1.f);

		const uint64_t bufSize = sizeof(vec4) * 16;
		Buffer* staging = Buffer::Create(bufSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
		staging->Map();
		staging->CopyData(noise.data());
		staging->Flush();
		staging->Unmap();
		
		ImageCreateInfo info{};
		info.format = VK_FORMAT_R16G16B16A16_SFLOAT;
		info.width = 4;
		info.height = 4;
		info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		info.properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		info.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;
		noiseTex.CreateImage(info);

		ImageViewCreateInfo viewInfo{};
		viewInfo.image = &noiseTex;
		noiseTex.CreateImageView(viewInfo);

		SamplerCreateInfo samplerInfo{};
		samplerInfo.minFilter = VK_FILTER_NEAREST;
		samplerInfo.magFilter = VK_FILTER_NEAREST;
		samplerInfo.minLod = 0.0f;
		samplerInfo.maxLod = 0.0f;
		samplerInfo.maxAnisotropy = 1.0f;
		noiseTex.CreateSampler(samplerInfo);

		noiseTex.TransitionImageLayout(VK_IMAGE_LAYOUT_PREINITIALIZED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
		noiseTex.CopyBufferToImage(staging);
		noiseTex.TransitionImageLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		staging->Destroy();
		// pvm uniform
		UB_PVM = Buffer::Create(3 * sizeof(mat4), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
		UB_PVM->Map();
		UB_PVM->Zero();
		UB_PVM->Flush();
		UB_PVM->Unmap();
		
		// DESCRIPTOR SET FOR SSAO
		DSet = Descriptor::Create(Pipeline::getDescriptorSetLayoutSSAO());
		
		// DESCRIPTOR SET FOR SSAO BLUR
		DSBlur = Descriptor::Create(Pipeline::getDescriptorSetLayoutSSAOBlur());
		
		updateDescriptorSets(renderTargets);
	}
	
	void SSAO::updateDescriptorSets(std::map<std::string, Image>& renderTargets)
	{
		std::array<DescriptorUpdateInfo, 5> infos{};

		infos[0].binding = 0;
		infos[0].pImage = &RHII.depth;
		infos[0].imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

		infos[1].binding = 1;
		infos[1].pImage = &renderTargets["normal"];

		infos[2].binding = 2;
		infos[2].pImage = &noiseTex;

		infos[3].binding = 3;
		infos[3].pBuffer = UB_Kernel;

		infos[4].binding = 4;
		infos[4].pBuffer = UB_PVM;

		DSet->UpdateDescriptor(5, infos.data());

		DescriptorUpdateInfo info{};
		info.binding = 0;
		info.pImage = &renderTargets["ssao"];

		DSBlur->UpdateDescriptor(1, &info);
	}
	
	void SSAO::draw(CommandBuffer* cmd, uint32_t imageIndex, Image& image)
	{
		// SSAO image
		image.ChangeLayout(cmd, LayoutState::ColorWrite);
		cmd->BeginPass(&renderPass, framebuffers[imageIndex]);
		cmd->BindPipeline(&pipeline);
		cmd->BindDescriptors(&pipeline, 1, &DSet);
		cmd->Draw(3, 1, 0, 0);
		cmd->EndPass();
		
		image.ChangeLayout(cmd, LayoutState::ColorRead);

		// new blurry SSAO image
		cmd->BeginPass(&blurRenderPass, blurFramebuffers[imageIndex]);
		cmd->BindPipeline(&pipelineBlur);
		cmd->BindDescriptors(&pipelineBlur, 1, &DSBlur);
		cmd->Draw(3, 1, 0, 0);
		cmd->EndPass();
		image.ChangeLayout(cmd, LayoutState::ColorRead);
	}
	
	void SSAO::destroy()
	{
		UB_Kernel->Destroy();
		UB_PVM->Destroy();
		noiseTex.Destroy();
		
		renderPass.Destroy();
		blurRenderPass.Destroy();
		
		for (auto frameBuffer : framebuffers)
			frameBuffer->Destroy();
		for (auto frameBuffer : blurFramebuffers)
			frameBuffer->Destroy();
		
		pipeline.destroy();
		pipelineBlur.destroy();
		Pipeline::getDescriptorSetLayoutSSAO()->Destroy();
		Pipeline::getDescriptorSetLayoutSSAOBlur()->Destroy();
	}
	
	void SSAO::update(Camera& camera)
	{
		if (GUI::show_ssao)
		{
			pvm[0] = camera.projection;
			pvm[1] = camera.view;
			pvm[2] = camera.invProjection;
			
			UB_PVM->CopyRequest<Launch::AsyncDeferred>({ &pvm, sizeof(pvm), 0 });
		}
	}
	
	void SSAO::createRenderPasses(std::map<std::string, Image>& renderTargets)
	{
		Attachment attachment{};
		attachment.format = renderTargets["ssao"].imageInfo.format;
		renderPass.Create(attachment);

		attachment.format = renderTargets["ssaoBlur"].imageInfo.format;
		blurRenderPass.Create(attachment);
	}
	
	void SSAO::createFrameBuffers(std::map<std::string, Image>& renderTargets)
	{
		createSSAOFrameBuffers(renderTargets);
		createSSAOBlurFrameBuffers(renderTargets);
	}
	
	void SSAO::createSSAOFrameBuffers(std::map<std::string, Image>& renderTargets)
	{
		framebuffers.resize(RHII.swapchain.images.size());
		for (size_t i = 0; i < RHII.swapchain.images.size(); ++i)
		{
			uint32_t width = renderTargets["ssao"].imageInfo.width;
			uint32_t height = renderTargets["ssao"].imageInfo.height;
			ImageViewHandle view = renderTargets["ssao"].view;
			framebuffers[i] = FrameBuffer::Create(width, height, view, renderPass);
		}
	}
	
	void SSAO::createSSAOBlurFrameBuffers(std::map<std::string, Image>& renderTargets)
	{
		blurFramebuffers.resize(RHII.swapchain.images.size());
		for (size_t i = 0; i < RHII.swapchain.images.size(); ++i)
		{
			uint32_t width = renderTargets["ssaoBlur"].imageInfo.width;
			uint32_t height = renderTargets["ssaoBlur"].imageInfo.height;
			ImageViewHandle view = renderTargets["ssaoBlur"].view;
			blurFramebuffers[i] = FrameBuffer::Create(width, height, view, blurRenderPass);
		}
	}
	
	void SSAO::createPipelines(std::map<std::string, Image>& renderTargets)
	{
		createPipeline(renderTargets);
		createBlurPipeline(renderTargets);
	}
	
	void SSAO::createPipeline(std::map<std::string, Image>& renderTargets)
	{
		Shader vert {"Shaders/Common/quad.vert", ShaderType::Vertex, true};
		Shader frag {"Shaders/SSAO/ssao.frag", ShaderType::Fragment, true};
		
		pipeline.info.pVertShader = &vert;
		pipeline.info.pFragShader = &frag;
		pipeline.info.width = renderTargets["ssao"].width_f;
		pipeline.info.height = renderTargets["ssao"].height_f;
		pipeline.info.cullMode = CullMode::Back;
		pipeline.info.colorBlendAttachments = { renderTargets["ssao"].blendAttachment };
		pipeline.info.descriptorSetLayouts = { Pipeline::getDescriptorSetLayoutSSAO() };
		pipeline.info.renderPass = renderPass;
		
		pipeline.createGraphicsPipeline();
	}
	
	void SSAO::createBlurPipeline(std::map<std::string, Image>& renderTargets)
	{
		Shader vert {"Shaders/Common/quad.vert", ShaderType::Vertex, true};
		Shader frag {"Shaders/SSAO/ssaoBlur.frag", ShaderType::Fragment, true};
		
		pipelineBlur.info.pVertShader = &vert;
		pipelineBlur.info.pFragShader = &frag;
		pipelineBlur.info.width = renderTargets["ssaoBlur"].width_f;
		pipelineBlur.info.height = renderTargets["ssaoBlur"].height_f;
		pipelineBlur.info.cullMode = CullMode::Back;
		pipelineBlur.info.colorBlendAttachments = { renderTargets["ssaoBlur"].blendAttachment };
		pipelineBlur.info.descriptorSetLayouts = { Pipeline::getDescriptorSetLayoutSSAOBlur() };
		pipelineBlur.info.renderPass = blurRenderPass;
		
		pipelineBlur.createGraphicsPipeline();
	}
}
