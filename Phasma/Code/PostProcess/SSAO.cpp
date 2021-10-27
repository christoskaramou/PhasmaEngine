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
#include "Renderer/CommandBuffer.h"
#include "Renderer/Descriptor.h"

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
		
		noiseTex.filter = VK_FILTER_NEAREST;
		noiseTex.minLod = 0.0f;
		noiseTex.maxLod = 0.0f;
		noiseTex.maxAnisotropy = 1.0f;
		noiseTex.format = VK_FORMAT_R16G16B16A16_SFLOAT;
		noiseTex.CreateImage(
			4, 4, VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		noiseTex.TransitionImageLayout(VK_IMAGE_LAYOUT_PREINITIALIZED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
		noiseTex.CopyBufferToImage(staging);
		noiseTex.TransitionImageLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		noiseTex.CreateImageView(VK_IMAGE_ASPECT_COLOR_BIT);
		noiseTex.CreateSampler();
		staging->Destroy();
		// pvm uniform
		UB_PVM = Buffer::Create(3 * sizeof(mat4), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
		UB_PVM->Map();
		UB_PVM->Zero();
		UB_PVM->Flush();
		UB_PVM->Unmap();
		
		// DESCRIPTOR SET FOR SSAO
		VkDescriptorSetLayout dsetLayout = Pipeline::getDescriptorSetLayoutSSAO();
		VkDescriptorSetAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.pNext = nullptr;
		allocInfo.descriptorPool = RHII.descriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &dsetLayout;

		VkDescriptorSet dset;
		vkAllocateDescriptorSets(RHII.device, &allocInfo, &dset);
		DSet = dset;
		
		// DESCRIPTOR SET FOR SSAO BLUR
		VkDescriptorSetLayout dsetLayoutBlur = Pipeline::getDescriptorSetLayoutSSAOBlur();
		VkDescriptorSetAllocateInfo allocInfoBlur{};
		allocInfoBlur.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfoBlur.pNext = nullptr;
		allocInfoBlur.descriptorPool = RHII.descriptorPool;
		allocInfoBlur.descriptorSetCount = 1;
		allocInfoBlur.pSetLayouts = &dsetLayoutBlur;

		VkDescriptorSet dsetBlur;
		vkAllocateDescriptorSets(RHII.device, &allocInfoBlur, &dsetBlur);
		DSBlur = dsetBlur;
		
		updateDescriptorSets(renderTargets);
	}
	
	void SSAO::updateDescriptorSets(std::map<std::string, Image>& renderTargets)
	{
		std::deque<VkDescriptorImageInfo> dsii {};
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
		std::deque<VkDescriptorBufferInfo> dsbi {};
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
		
		std::vector<VkWriteDescriptorSet> writeDescriptorSets
		{
			wSetImage(DSet, 0, RHII.depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL),
			wSetImage(DSet, 1, renderTargets["normal"]),
			wSetImage(DSet, 2, noiseTex),
			wSetBuffer(DSet, 3, *UB_Kernel),
			wSetBuffer(DSet, 4, *UB_PVM),
			wSetImage(DSBlur, 0, renderTargets["ssao"])
		};

		vkUpdateDescriptorSets(RHII.device, (uint32_t)writeDescriptorSets.size(), writeDescriptorSets.data(), 0, nullptr);
	}
	
	void SSAO::draw(CommandBuffer* cmd, uint32_t imageIndex, Image& image)
	{
		// SSAO image
		image.ChangeLayout(*cmd, LayoutState::ColorWrite);
		cmd->BeginPass(renderPass, framebuffers[imageIndex]);
		cmd->BindPipeline(pipeline);
		cmd->BindDescriptors(pipeline, 1, &DSet);
		cmd->Draw(3, 1, 0, 0);
		cmd->EndPass();
		
		image.ChangeLayout(*cmd, LayoutState::ColorRead);

		// new blurry SSAO image
		cmd->BeginPass(blurRenderPass, blurFramebuffers[imageIndex]);
		cmd->BindPipeline(pipelineBlur);
		cmd->BindDescriptors(pipelineBlur, 1, &DSBlur);
		cmd->Draw(3, 1, 0, 0);
		cmd->EndPass();
		image.ChangeLayout(*cmd, LayoutState::ColorRead);
	}
	
	void SSAO::destroy()
	{
		UB_Kernel->Destroy();
		UB_PVM->Destroy();
		noiseTex.Destroy();
		
		renderPass.Destroy();
		blurRenderPass.Destroy();
		
		for (auto& frameBuffer : framebuffers)
			frameBuffer.Destroy();
		for (auto& frameBuffer : blurFramebuffers)
			frameBuffer.Destroy();
		
		pipeline.destroy();
		pipelineBlur.destroy();
		if (Pipeline::getDescriptorSetLayoutSSAO())
		{
			vkDestroyDescriptorSetLayout(RHII.device, Pipeline::getDescriptorSetLayoutSSAO(), nullptr);
			Pipeline::getDescriptorSetLayoutSSAO() = {};
		}
		if (Pipeline::getDescriptorSetLayoutSSAOBlur())
		{
			vkDestroyDescriptorSetLayout(RHII.device, Pipeline::getDescriptorSetLayoutSSAOBlur(), nullptr);
			Pipeline::getDescriptorSetLayoutSSAOBlur() = {};
		}
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
		attachment.format = renderTargets["ssao"].format;
		renderPass.Create(attachment);

		attachment.format = renderTargets["ssaoBlur"].format;
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
			uint32_t width = renderTargets["ssao"].width;
			uint32_t height = renderTargets["ssao"].height;
			ImageViewHandle view = renderTargets["ssao"].view;
			framebuffers[i].Create(width, height, view, renderPass);
		}
	}
	
	void SSAO::createSSAOBlurFrameBuffers(std::map<std::string, Image>& renderTargets)
	{
		blurFramebuffers.resize(RHII.swapchain.images.size());
		for (size_t i = 0; i < RHII.swapchain.images.size(); ++i)
		{
			uint32_t width = renderTargets["ssaoBlur"].width;
			uint32_t height = renderTargets["ssaoBlur"].height;
			ImageViewHandle view = renderTargets["ssaoBlur"].view;
			blurFramebuffers[i].Create(width, height, view, blurRenderPass);
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
