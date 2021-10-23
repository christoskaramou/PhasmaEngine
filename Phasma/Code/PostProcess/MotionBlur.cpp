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

#include "MotionBlur.h"
#include "Renderer/Surface.h"
#include "Renderer/Swapchain.h"
#include "GUI/GUI.h"
#include "Shader/Shader.h"
#include "Core/Queue.h"
#include "Core/Timer.h"
#include "Renderer/Vulkan/Vulkan.h"
#include "Renderer/CommandBuffer.h"

namespace pe
{
	MotionBlur::MotionBlur()
	{
		DSet = {};
	}
	
	MotionBlur::~MotionBlur()
	{
	}
	
	void MotionBlur::Init()
	{
		frameImage.format = VULKAN.surface.formatKHR.format;
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
	
	void MotionBlur::createMotionBlurUniforms(std::map<std::string, Image>& renderTargets)
	{
		auto size = 4 * sizeof(mat4);
		UBmotionBlur = Buffer::Create(size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
		UBmotionBlur->Map();
		UBmotionBlur->Zero();
		UBmotionBlur->Flush();
		UBmotionBlur->Unmap();

		VkDescriptorSetLayout dsetLayout = Pipeline::getDescriptorSetLayoutMotionBlur();
		VkDescriptorSetAllocateInfo allocateInfo{};
		allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocateInfo.descriptorPool = VULKAN.descriptorPool;
		allocateInfo.descriptorSetCount = 1;
		allocateInfo.pSetLayouts = &dsetLayout;

		VkDescriptorSet dset;
		vkAllocateDescriptorSets(VULKAN.device, &allocateInfo, &dset);
		DSet = dset;

		updateDescriptorSets(renderTargets);
	}
	
	void MotionBlur::updateDescriptorSets(std::map<std::string, Image>& renderTargets)
	{
		std::deque<VkDescriptorImageInfo> dsii {};
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
			wSetImage(DSet, 0, frameImage),
			wSetImage(DSet, 1, VULKAN.depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL),
			wSetImage(DSet, 2, renderTargets["velocity"]),
			wSetBuffer(DSet, 3, *UBmotionBlur)
		};

		vkUpdateDescriptorSets(VULKAN.device, (uint32_t)textureWriteSets.size(), textureWriteSets.data(), 0, nullptr);
	}
	
	void MotionBlur::draw(CommandBuffer* cmd, uint32_t imageIndex)
	{
		const vec4 values
		{
			1.f / static_cast<float>(FrameTimer::Instance().delta),
			sin(static_cast<float>(FrameTimer::Instance().time) * 0.125f),
			GUI::motionBlur_strength,
			0.f
		};

		cmd->BeginPass(renderPass, framebuffers[imageIndex]);
		cmd->PushConstants(pipeline, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(vec4), &values);
		cmd->BindPipeline(pipeline);
		cmd->BindDescriptors(pipeline, 1, &DSet);
		cmd->Draw(3, 1, 0, 0);
		cmd->EndPass();
	}
	
	void MotionBlur::update(Camera& camera)
	{
		if (GUI::show_motionBlur)
		{
			static mat4 previousView = camera.view;
			
			motionBlurInput[0] = camera.projection;
			motionBlurInput[1] = camera.view;
			motionBlurInput[2] = previousView;
			motionBlurInput[3] = camera.invViewProjection;
			
			previousView = camera.view;
			
			UBmotionBlur->CopyRequest<Launch::AsyncDeferred>({ &motionBlurInput, sizeof(motionBlurInput), 0 });
		}
	}
	
	void MotionBlur::createRenderPass(std::map<std::string, Image>& renderTargets)
	{
		Attachment attachment{};
		attachment.format = renderTargets["viewport"].format;
		renderPass.Create(attachment);
	}
	
	void MotionBlur::createFrameBuffers(std::map<std::string, Image>& renderTargets)
	{
		auto vulkan = VulkanContext::Get();
		framebuffers.resize(vulkan->swapchain.images.size());
		for (size_t i = 0; i < vulkan->swapchain.images.size(); ++i)
		{
			uint32_t width = renderTargets["viewport"].width;
			uint32_t height = renderTargets["viewport"].height;
			ImageViewHandle view = renderTargets["viewport"].view;
			framebuffers[i].Create(width, height, view, renderPass);
		}
	}
	
	void MotionBlur::createPipeline(std::map<std::string, Image>& renderTargets)
	{
		// Shader stages
		Shader vert {"Shaders/Common/quad.vert", ShaderType::Vertex, true};
		Shader frag {"Shaders/MotionBlur/motionBlur.frag", ShaderType::Fragment, true};
		
		pipeline.info.pVertShader = &vert;
		pipeline.info.pFragShader = &frag;
		pipeline.info.width = renderTargets["viewport"].width_f;
		pipeline.info.height = renderTargets["viewport"].height_f;
		pipeline.info.cullMode = CullMode::Back;
		pipeline.info.colorBlendAttachments = { renderTargets["viewport"].blendAttachment };
		pipeline.info.pushConstantStage = PushConstantStage::Fragment;
		pipeline.info.pushConstantSize = sizeof(vec4);
		pipeline.info.descriptorSetLayouts = { Pipeline::getDescriptorSetLayoutMotionBlur() };
		pipeline.info.renderPass = renderPass;
		
		pipeline.createGraphicsPipeline();
	}

	void MotionBlur::destroy()
	{
		for (auto& frameBuffer : framebuffers)
			frameBuffer.Destroy();

		renderPass.Destroy();

		if (Pipeline::getDescriptorSetLayoutMotionBlur())
		{
			vkDestroyDescriptorSetLayout(VULKAN.device, Pipeline::getDescriptorSetLayoutMotionBlur(), nullptr);
			Pipeline::getDescriptorSetLayoutMotionBlur() = {};
		}
		frameImage.Destroy();
		UBmotionBlur->Destroy();
		pipeline.destroy();
	}
}
