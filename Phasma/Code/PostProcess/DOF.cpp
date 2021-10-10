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
#include "DOF.h"
#include "GUI/GUI.h"
#include "Renderer/Swapchain.h"
#include "Renderer/Surface.h"
#include "Shader/Shader.h"
#include "Renderer/RenderApi.h"
#include <deque>

namespace pe
{
	DOF::DOF()
	{
		DSet = make_sptr(vk::DescriptorSet());
	}
	
	DOF::~DOF()
	{
	}
	
	void DOF::Init()
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
		frameImage.SetDebugName("DOF_FrameImage");
	}
	
	void DOF::createRenderPass(std::map<std::string, Image>& renderTargets)
	{
		renderPass.Create((vk::Format)renderTargets["viewport"].format);
	}
	
	void DOF::createFrameBuffers(std::map<std::string, Image>& renderTargets)
	{
		auto vulkan = VulkanContext::Get();
		framebuffers.resize(vulkan->swapchain.images.size());
		for (size_t i = 0; i < vulkan->swapchain.images.size(); ++i)
		{
			uint32_t width = renderTargets["viewport"].width;
			uint32_t height = renderTargets["viewport"].height;
			vk::ImageView view = renderTargets["viewport"].view;
			framebuffers[i].Create(width, height, view, renderPass);
		}
	}
	
	void DOF::createUniforms(std::map<std::string, Image>& renderTargets)
	{
		auto vulkan = VulkanContext::Get();
		vk::DescriptorSetAllocateInfo allocateInfo;
		allocateInfo.descriptorPool = *vulkan->descriptorPool;
		allocateInfo.descriptorSetCount = 1;
		
		allocateInfo.pSetLayouts = &Pipeline::getDescriptorSetLayoutDOF();
		DSet = make_sptr(vulkan->device->allocateDescriptorSets(allocateInfo).at(0));
		VULKAN.SetDebugObjectName(*DSet, "DOF");
		
		updateDescriptorSets(renderTargets);
	}
	
	void DOF::updateDescriptorSets(std::map<std::string, Image>& renderTargets)
	{
		std::deque<vk::DescriptorImageInfo> dsii {};
		auto const wSetImage = [&dsii](const vk::DescriptorSet& dstSet, uint32_t dstBinding, Image& image, vk::ImageLayout layout = vk::ImageLayout::eShaderReadOnlyOptimal)
		{
			dsii.emplace_back(vk::Sampler(image.sampler), vk::ImageView(image.view), layout);
			return vk::WriteDescriptorSet {
					dstSet, dstBinding, 0, 1, vk::DescriptorType::eCombinedImageSampler, &dsii.back(), nullptr, nullptr
			};
		};
		
		std::vector<vk::WriteDescriptorSet> textureWriteSets {
				wSetImage(*DSet, 0, frameImage),
				wSetImage(*DSet, 1, VULKAN.depth, vk::ImageLayout::eDepthStencilReadOnlyOptimal)
		};
		VULKAN.device->updateDescriptorSets(textureWriteSets, nullptr);
	}
	
	void DOF::draw(vk::CommandBuffer cmd, uint32_t imageIndex, std::map<std::string, Image>& renderTargets)
	{
		const vec4 color(0.0f, 0.0f, 0.0f, 1.0f);
		vk::ClearValue clearColor;
		memcpy(clearColor.color.float32, &color, sizeof(vec4));
		
		std::vector<vk::ClearValue> clearValues = {clearColor};
		
		std::vector<float> values {GUI::DOF_focus_scale, GUI::DOF_blur_range, 0.0f, 0.0f};
		
		vk::RenderPassBeginInfo rpi;
		rpi.renderPass = *renderPass.handle;
		rpi.framebuffer = *framebuffers[imageIndex].handle;
		rpi.renderArea.offset = vk::Offset2D {0, 0};
		vk::Extent2D extent{ renderTargets["viewport"].width, renderTargets["viewport"].height };
		rpi.renderArea.extent = extent;
		rpi.clearValueCount = 1;
		rpi.pClearValues = clearValues.data();
		
		cmd.beginRenderPass(rpi, vk::SubpassContents::eInline);
		cmd.pushConstants<float>(*pipeline.layout, vk::ShaderStageFlagBits::eFragment, 0, values);
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline.handle);
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipeline.layout, 0, *DSet, nullptr);
		cmd.draw(3, 1, 0, 0);
		cmd.endRenderPass();
	}
	
	void DOF::createPipeline(std::map<std::string, Image>& renderTargets)
	{
		Shader vert {"Shaders/Common/quad.vert", ShaderType::Vertex, true};
		Shader frag {"Shaders/DepthOfField/DOF.frag", ShaderType::Fragment, true};
		
		pipeline.info.pVertShader = &vert;
		pipeline.info.pFragShader = &frag;
		pipeline.info.width = renderTargets["viewport"].width_f;
		pipeline.info.height = renderTargets["viewport"].height_f;
		pipeline.info.cullMode = CullMode::Back;
		pipeline.info.colorBlendAttachments = make_sptr(
				std::vector<vk::PipelineColorBlendAttachmentState> {renderTargets["viewport"].blendAttachment}
		);
		pipeline.info.pushConstantStage = PushConstantStage::Fragment;
		pipeline.info.pushConstantSize = 5 * sizeof(vec4);
		pipeline.info.descriptorSetLayouts = make_sptr(
				std::vector<vk::DescriptorSetLayout> {Pipeline::getDescriptorSetLayoutDOF()}
		);
		pipeline.info.renderPass = renderPass;
		
		pipeline.createGraphicsPipeline();
		
	}
	
	void DOF::destroy()
	{
		auto vulkan = VulkanContext::Get();
		for (auto& framebuffer : framebuffers)
			framebuffer.Destroy();
		
		renderPass.Destroy();
		
		if (Pipeline::getDescriptorSetLayoutDOF())
		{
			vulkan->device->destroyDescriptorSetLayout(Pipeline::getDescriptorSetLayoutDOF());
			Pipeline::getDescriptorSetLayoutDOF() = nullptr;
		}
		frameImage.Destroy();
		pipeline.destroy();
	}
}
