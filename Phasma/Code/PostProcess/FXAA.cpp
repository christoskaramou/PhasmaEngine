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
#include "FXAA.h"
#include "GUI/GUI.h"
#include "Renderer/Swapchain.h"
#include "Renderer/Surface.h"
#include "Shader/Shader.h"
#include "Renderer/Vulkan/Vulkan.h"

namespace pe
{
	FXAA::FXAA()
	{
		DSet = make_sptr(vk::DescriptorSet());
	}
	
	FXAA::~FXAA()
	{
	}
	
	void FXAA::Init()
	{
		frameImage.format = (VkFormat)VULKAN.surface.formatKHR->format;
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
		frameImage.SetDebugName("FXAA_FrameImage");
	}
	
	void FXAA::createUniforms(std::map<std::string, Image>& renderTargets)
	{
		vk::DescriptorSetAllocateInfo allocateInfo2;
		allocateInfo2.descriptorPool = *VULKAN.descriptorPool;
		allocateInfo2.descriptorSetCount = 1;
		allocateInfo2.pSetLayouts = &(vk::DescriptorSetLayout)Pipeline::getDescriptorSetLayoutFXAA();
		DSet = make_sptr(VULKAN.device->allocateDescriptorSets(allocateInfo2).at(0));
		VULKAN.SetDebugObjectName(*DSet, "FXAA");
		
		updateDescriptorSets(renderTargets);
	}
	
	void FXAA::updateDescriptorSets(std::map<std::string, Image>& renderTargets)
	{
		// Composition sampler
		vk::DescriptorImageInfo dii;
		dii.sampler = (VkSampler)frameImage.sampler;
		dii.imageView = (VkImageView)frameImage.view;
		dii.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		
		vk::WriteDescriptorSet textureWriteSet;
		textureWriteSet.dstSet = *DSet;
		textureWriteSet.dstBinding = 0;
		textureWriteSet.dstArrayElement = 0;
		textureWriteSet.descriptorCount = 1;
		textureWriteSet.descriptorType = vk::DescriptorType::eCombinedImageSampler;
		textureWriteSet.pImageInfo = &dii;
		
		VULKAN.device->updateDescriptorSets(textureWriteSet, nullptr);
	}
	
	void FXAA::draw(vk::CommandBuffer cmd, uint32_t imageIndex, const vk::Extent2D& extent)
	{
		const vec4 color(0.0f, 0.0f, 0.0f, 1.0f);
		vk::ClearValue clearColor;
		memcpy(clearColor.color.float32, &color, sizeof(vec4));
		
		std::vector<vk::ClearValue> clearValues = {clearColor};
		
		vk::RenderPassBeginInfo rpi;
		rpi.renderPass = renderPass.handle;
		rpi.framebuffer = framebuffers[imageIndex].handle;
		rpi.renderArea.offset = vk::Offset2D {0, 0};
		rpi.renderArea.extent = extent;
		rpi.clearValueCount = 1;
		rpi.pClearValues = clearValues.data();
		
		cmd.beginRenderPass(rpi, vk::SubpassContents::eInline);
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline.handle);
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipeline.layout, 0, *DSet, nullptr);
		cmd.draw(3, 1, 0, 0);
		cmd.endRenderPass();
	}
	
	void FXAA::createRenderPass(std::map<std::string, Image>& renderTargets)
	{
		Attachment attachment{};
		attachment.format = renderTargets["viewport"].format;
		renderPass.Create(attachment);
	}
	
	void FXAA::createFrameBuffers(std::map<std::string, Image>& renderTargets)
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
	
	void FXAA::createPipeline(std::map<std::string, Image>& renderTargets)
	{
		Shader vert {"Shaders/Common/quad.vert", ShaderType::Vertex, true};
		Shader frag {"Shaders/FXAA/FXAA.frag", ShaderType::Fragment, true};
		
		pipeline.info.pVertShader = &vert;
		pipeline.info.pFragShader = &frag;
		pipeline.info.width = renderTargets["viewport"].width_f;
		pipeline.info.height = renderTargets["viewport"].height_f;
		pipeline.info.cullMode = CullMode::Back;
		pipeline.info.colorBlendAttachments = make_sptr(
				std::vector<vk::PipelineColorBlendAttachmentState> {renderTargets["viewport"].blendAttachment}
		);
		pipeline.info.descriptorSetLayouts = make_sptr(
				std::vector<vk::DescriptorSetLayout> {(vk::DescriptorSetLayout)Pipeline::getDescriptorSetLayoutFXAA()}
		);
		pipeline.info.renderPass = renderPass;
		
		pipeline.createGraphicsPipeline();
	}
	
	void FXAA::destroy()
	{
		for (auto& frameBuffer : framebuffers)
			frameBuffer.Destroy();
		
		renderPass.Destroy();
		
		if (Pipeline::getDescriptorSetLayoutFXAA())
		{
			vkDestroyDescriptorSetLayout(*VULKAN.device, Pipeline::getDescriptorSetLayoutFXAA(), nullptr);
			Pipeline::getDescriptorSetLayoutFXAA() = {};
		}
		frameImage.Destroy();
		pipeline.destroy();
	}
}
