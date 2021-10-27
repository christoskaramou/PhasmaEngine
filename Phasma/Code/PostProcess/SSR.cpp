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

#include "SSR.h"
#include "GUI/GUI.h"
#include "Renderer/Swapchain.h"
#include "Renderer/Surface.h"
#include "Shader/Shader.h"
#include "Core/Queue.h"
#include "Renderer/RHI.h"
#include "Renderer/CommandBuffer.h"

namespace pe
{
	SSR::SSR()
	{
		DSet = {};
	}
	
	SSR::~SSR()
	{
	}
	
	void SSR::createSSRUniforms(std::map<std::string, Image>& renderTargets)
	{
		UBReflection = Buffer::Create(
			4 * sizeof(mat4),
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
		UBReflection->Map();
		UBReflection->Zero();
		UBReflection->Flush();
		UBReflection->Unmap();

		VkDescriptorSetLayout dsetLayout = Pipeline::getDescriptorSetLayoutSSR();
		VkDescriptorSetAllocateInfo allocateInfo{};
		allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocateInfo.descriptorPool = RHII.descriptorPool;
		allocateInfo.descriptorSetCount = 1;
		allocateInfo.pSetLayouts = &dsetLayout;

		VkDescriptorSet dset;
		vkAllocateDescriptorSets(RHII.device, &allocateInfo, &dset);
		DSet = dset;

		updateDescriptorSets(renderTargets);
	}
	
	void SSR::updateDescriptorSets(std::map<std::string, Image>& renderTargets)
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
			wSetImage(DSet, 0, renderTargets["albedo"]),
			wSetImage(DSet, 1, RHII.depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL),
			wSetImage(DSet, 2, renderTargets["normal"]),
			wSetImage(DSet, 3, renderTargets["srm"]),
			wSetBuffer(DSet, 4, *UBReflection)
		};

		vkUpdateDescriptorSets(RHII.device, (uint32_t)textureWriteSets.size(), textureWriteSets.data(), 0, nullptr);
	}
	
	void SSR::update(Camera& camera)
	{
		if (GUI::show_ssr)
		{
			reflectionInput[0][0] = vec4(camera.position, 1.0f);
			reflectionInput[0][1] = vec4(camera.front, 1.0f);
			reflectionInput[0][2] = vec4();
			reflectionInput[0][3] = vec4();
			reflectionInput[1] = camera.projection;
			reflectionInput[2] = camera.view;
			reflectionInput[3] = camera.invProjection;
			
			UBReflection->CopyRequest<Launch::AsyncDeferred>({ &reflectionInput, sizeof(reflectionInput), 0 });
		}
	}
	
	
	void SSR::draw(CommandBuffer* cmd, uint32_t imageIndex)
	{
		cmd->BeginPass(renderPass, framebuffers[imageIndex]);
		cmd->BindPipeline(pipeline);
		cmd->BindDescriptors(pipeline, 1, &DSet);
		cmd->Draw(3, 1, 0, 0);
		cmd->EndPass();
	}
	
	void SSR::createRenderPass(std::map<std::string, Image>& renderTargets)
	{
		Attachment attachment{};
		attachment.format = renderTargets["ssr"].format;
		renderPass.Create(attachment);
	}
	
	void SSR::createFrameBuffers(std::map<std::string, Image>& renderTargets)
	{
		framebuffers.resize(RHII.swapchain.images.size());
		for (size_t i = 0; i < RHII.swapchain.images.size(); ++i)
		{
			uint32_t width = renderTargets["ssr"].width;
			uint32_t height = renderTargets["ssr"].height;
			ImageViewHandle view = renderTargets["ssr"].view;
			framebuffers[i].Create(width, height, view, renderPass);
		}
	}
	
	void SSR::createPipeline(std::map<std::string, Image>& renderTargets)
	{
		Shader vert {"Shaders/Common/quad.vert", ShaderType::Vertex, true};
		Shader frag {"Shaders/SSR/ssr.frag", ShaderType::Fragment, true};
		
		pipeline.info.pVertShader = &vert;
		pipeline.info.pFragShader = &frag;
		pipeline.info.width = renderTargets["ssr"].width_f;
		pipeline.info.height = renderTargets["ssr"].height_f;
		pipeline.info.cullMode = CullMode::Back;
		pipeline.info.colorBlendAttachments = { renderTargets["ssr"].blendAttachment };
		pipeline.info.descriptorSetLayouts = { Pipeline::getDescriptorSetLayoutSSR() };
		pipeline.info.renderPass = renderPass;
		
		pipeline.createGraphicsPipeline();
	}
	
	void SSR::destroy()
	{
		for (auto& framebuffer : framebuffers)
			framebuffer.Destroy();
		
		renderPass.Destroy();
		
		if (Pipeline::getDescriptorSetLayoutSSR())
		{
			vkDestroyDescriptorSetLayout(RHII.device, Pipeline::getDescriptorSetLayoutSSR(), nullptr);
			Pipeline::getDescriptorSetLayoutSSR() = {};
		}
		UBReflection->Destroy();
		pipeline.destroy();
	}
}