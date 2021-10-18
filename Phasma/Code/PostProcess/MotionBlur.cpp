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
#include "MotionBlur.h"
#include <deque>
#include "Renderer/Surface.h"
#include "Renderer/Swapchain.h"
#include "GUI/GUI.h"
#include "Shader/Shader.h"
#include "Core/Queue.h"
#include "Core/Timer.h"
#include "Renderer/Vulkan/Vulkan.h"

namespace pe
{
	MotionBlur::MotionBlur()
	{
		DSet = make_sptr(vk::DescriptorSet());
	}
	
	MotionBlur::~MotionBlur()
	{
	}
	
	void MotionBlur::Init()
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
		frameImage.SetDebugName("MotionBlur_FrameImage");
	}
	
	void MotionBlur::createMotionBlurUniforms(std::map<std::string, Image>& renderTargets)
	{
		auto size = 4 * sizeof(mat4);
		UBmotionBlur = Buffer::Create(
			size,
			(BufferUsageFlags)vk::BufferUsageFlagBits::eUniformBuffer,
			(MemoryPropertyFlags)vk::MemoryPropertyFlagBits::eHostVisible);
		UBmotionBlur->Map();
		UBmotionBlur->Zero();
		UBmotionBlur->Flush();
		UBmotionBlur->Unmap();
		UBmotionBlur->SetDebugName("MotionBlur_UB");
		
		vk::DescriptorSetAllocateInfo allocateInfo;
		allocateInfo.descriptorPool = *VULKAN.descriptorPool;
		allocateInfo.descriptorSetCount = 1;
		allocateInfo.pSetLayouts = &(vk::DescriptorSetLayout)Pipeline::getDescriptorSetLayoutMotionBlur();
		DSet = make_sptr(VULKAN.device->allocateDescriptorSets(allocateInfo).at(0));
		VULKAN.SetDebugObjectName(*DSet, "MotionBlur");
		
		updateDescriptorSets(renderTargets);
	}
	
	void MotionBlur::updateDescriptorSets(std::map<std::string, Image>& renderTargets)
	{
		std::deque<vk::DescriptorImageInfo> dsii {};
		auto const wSetImage = [&dsii](const vk::DescriptorSet& dstSet, uint32_t dstBinding, Image& image, vk::ImageLayout layout = vk::ImageLayout::eShaderReadOnlyOptimal)
		{
			dsii.emplace_back(vk::Sampler(image.sampler), vk::ImageView(image.view), layout);
			return vk::WriteDescriptorSet {
					dstSet, dstBinding, 0, 1, vk::DescriptorType::eCombinedImageSampler, &dsii.back(), nullptr, nullptr
			};
		};
		std::deque<vk::DescriptorBufferInfo> dsbi {};
		auto const wSetBuffer = [&dsbi](const vk::DescriptorSet& dstSet, uint32_t dstBinding, Buffer& buffer)
		{
			dsbi.emplace_back(buffer.Handle<vk::Buffer>(), 0, buffer.Size());
			return vk::WriteDescriptorSet {
					dstSet, dstBinding, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &dsbi.back(), nullptr
			};
		};
		
		std::vector<vk::WriteDescriptorSet> textureWriteSets {
				wSetImage(*DSet, 0, frameImage),
				wSetImage(*DSet, 1, VULKAN.depth, vk::ImageLayout::eDepthStencilReadOnlyOptimal),
				wSetImage(*DSet, 2, renderTargets["velocity"]),
				wSetBuffer(*DSet, 3, *UBmotionBlur)
		};
		VULKAN.device->updateDescriptorSets(textureWriteSets, nullptr);
	}
	
	void MotionBlur::draw(vk::CommandBuffer cmd, uint32_t imageIndex, const vk::Extent2D& extent)
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
		rpi.clearValueCount = static_cast<uint32_t>(clearValues.size());
		rpi.pClearValues = clearValues.data();
		cmd.beginRenderPass(rpi, vk::SubpassContents::eInline);
		
		const vec4 values {
				1.f / static_cast<float>(FrameTimer::Instance().delta),
				sin(static_cast<float>(FrameTimer::Instance().time) * 0.125f), GUI::motionBlur_strength, 0.f
		};
		cmd.pushConstants<vec4>(*pipeline.layout, vk::ShaderStageFlagBits::eFragment, 0, values);
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline.handle);
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipeline.layout, 0, *DSet, nullptr);
		cmd.draw(3, 1, 0, 0);
		cmd.endRenderPass();
	}
	
	void MotionBlur::destroy()
	{
		for (auto& frameBuffer : framebuffers)
			frameBuffer.Destroy();
		
		renderPass.Destroy();
		
		if (Pipeline::getDescriptorSetLayoutMotionBlur())
		{
			vkDestroyDescriptorSetLayout(*VULKAN.device, Pipeline::getDescriptorSetLayoutMotionBlur(), nullptr);
			Pipeline::getDescriptorSetLayoutMotionBlur() = {};
		}
		frameImage.Destroy();
		UBmotionBlur->Destroy();
		pipeline.destroy();
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
		pipeline.info.colorBlendAttachments = make_sptr(
				std::vector<vk::PipelineColorBlendAttachmentState> {renderTargets["viewport"].blendAttachment}
		);
		pipeline.info.pushConstantStage = PushConstantStage::Fragment;
		pipeline.info.pushConstantSize = sizeof(vec4);
		pipeline.info.descriptorSetLayouts = make_sptr(
				std::vector<vk::DescriptorSetLayout> {(vk::DescriptorSetLayout)Pipeline::getDescriptorSetLayoutMotionBlur()}
		);
		pipeline.info.renderPass = renderPass;
		
		pipeline.createGraphicsPipeline();
	}
}
