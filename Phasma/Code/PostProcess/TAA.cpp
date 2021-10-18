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
#include "TAA.h"
#include "GUI/GUI.h"
#include "Renderer/Surface.h"
#include "Renderer/Swapchain.h"
#include "Shader/Shader.h"
#include "Core/Queue.h"
#include "Renderer/Vulkan/Vulkan.h"
#include "Renderer/CommandBuffer.h"
#include <deque>

namespace pe
{
	TAA::TAA()
	{
		DSet = make_sptr(vk::DescriptorSet());
		DSetSharpen = make_sptr(vk::DescriptorSet());
	}
	
	TAA::~TAA()
	{
	}
	
	void TAA::Init()
	{
		previous.format = (VkFormat)VULKAN.surface.formatKHR->format;
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
		previous.SetDebugName("TAA_PreviousFrameImage");
		
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
		frameImage.SetDebugName("TAA_FrameImage");
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
			(BufferUsageFlags)vk::BufferUsageFlagBits::eUniformBuffer,
			(MemoryPropertyFlags)vk::MemoryPropertyFlagBits::eHostVisible);
		uniform->Map();
		uniform->Zero();
		uniform->Flush();
		uniform->Unmap();
		uniform->SetDebugName("TAA_UB");
		
		vk::DescriptorSetAllocateInfo allocateInfo2;
		allocateInfo2.descriptorPool = *VULKAN.descriptorPool;
		allocateInfo2.descriptorSetCount = 1;
		allocateInfo2.pSetLayouts = &Pipeline::getDescriptorSetLayoutTAA();
		DSet = make_sptr(VULKAN.device->allocateDescriptorSets(allocateInfo2).at(0));
		VULKAN.SetDebugObjectName(*DSet, "TAA");
		
		allocateInfo2.pSetLayouts = &Pipeline::getDescriptorSetLayoutTAASharpen();
		DSetSharpen = make_sptr(VULKAN.device->allocateDescriptorSets(allocateInfo2).at(0));
		VULKAN.SetDebugObjectName(*DSetSharpen, "TAA_Sharpen");
		
		updateDescriptorSets(renderTargets);
	}
	
	void TAA::updateDescriptorSets(std::map<std::string, Image>& renderTargets)
	{
		std::deque<vk::DescriptorImageInfo> dsii {};
		auto const wSetImage = [&dsii](const vk::DescriptorSet& dstSet, uint32_t dstBinding, Image& image, vk::ImageLayout layout = vk::ImageLayout::eShaderReadOnlyOptimal)
		{
			dsii.emplace_back(vk::Sampler(image.sampler), vk::ImageView(image.view), layout);
			return vk::WriteDescriptorSet{
					dstSet, dstBinding, 0, 1, vk::DescriptorType::eCombinedImageSampler, &dsii.back(), nullptr, nullptr
			};
		};
		std::deque<vk::DescriptorBufferInfo> dsbi {};
		const auto wSetBuffer = [&dsbi](const vk::DescriptorSet& dstSet, uint32_t dstBinding, Buffer& buffer)
		{
			dsbi.emplace_back(buffer.Handle<vk::Buffer>(), 0, buffer.Size());
			return vk::WriteDescriptorSet {
					dstSet, dstBinding, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &dsbi.back(), nullptr
			};
		};
		
		std::vector<vk::WriteDescriptorSet> writeDescriptorSets = {
				wSetImage(*DSet, 0, previous),
				wSetImage(*DSet, 1, frameImage),
				wSetImage(*DSet, 2, VULKAN.depth, vk::ImageLayout::eDepthStencilReadOnlyOptimal),
				wSetImage(*DSet, 3, renderTargets["velocity"]),
				wSetBuffer(*DSet, 4, *uniform),
				wSetImage(*DSetSharpen, 0, renderTargets["taa"]),
				wSetBuffer(*DSetSharpen, 1, *uniform)
		};
		
		VULKAN.device->updateDescriptorSets(writeDescriptorSets, nullptr);
	}
	
	void TAA::draw(vk::CommandBuffer cmd, uint32_t imageIndex, std::map<std::string, Image>& renderTargets)
	{
		CommandBuffer cmdBuf = VkCommandBuffer(cmd);

		const vec4 color(0.0f, 0.0f, 0.0f, 1.0f);
		vk::ClearValue clearColor;
		memcpy(clearColor.color.float32, &color, sizeof(vec4));
		
		std::vector<vk::ClearValue> clearValues = {clearColor};
		
		// Main TAA pass
		vk::RenderPassBeginInfo rpi;
		rpi.renderPass = *renderPass.handle;
		rpi.framebuffer = *framebuffers[imageIndex].handle;
		rpi.renderArea.offset = vk::Offset2D {0, 0};
		vk::Extent2D extent{ renderTargets["taa"].width, renderTargets["taa"].height };
		rpi.renderArea.extent = extent;
		rpi.clearValueCount = 1;
		rpi.pClearValues = clearValues.data();
		
		renderTargets["taa"].ChangeLayout(cmdBuf, LayoutState::ColorWrite);
		cmd.beginRenderPass(rpi, vk::SubpassContents::eInline);
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline.handle);
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipeline.layout, 0, *DSet, nullptr);
		cmd.draw(3, 1, 0, 0);
		cmd.endRenderPass();
		renderTargets["taa"].ChangeLayout(cmdBuf, LayoutState::ColorRead);
		
		saveImage(cmd, renderTargets["taa"]);
		
		// TAA Sharpen pass
		vk::RenderPassBeginInfo rpi2;
		rpi2.renderPass = *renderPassSharpen.handle;
		rpi2.framebuffer = *framebuffersSharpen[imageIndex].handle;
		rpi2.renderArea.offset = vk::Offset2D {0, 0};
		extent = vk::Extent2D{ renderTargets["viewport"].width, renderTargets["viewport"].height };
		rpi2.renderArea.extent = extent;
		rpi2.clearValueCount = 1;
		rpi2.pClearValues = clearValues.data();
		
		cmd.beginRenderPass(rpi2, vk::SubpassContents::eInline);
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipelineSharpen.handle);
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipelineSharpen.layout, 0, *DSetSharpen, nullptr);
		cmd.draw(3, 1, 0, 0);
		cmd.endRenderPass();
	}
	
	void TAA::createRenderPasses(std::map<std::string, Image>& renderTargets)
	{
		renderPass.Create((vk::Format)renderTargets["taa"].format);
		renderPassSharpen.Create((vk::Format)renderTargets["viewport"].format);
	}
	
	void TAA::createFrameBuffers(std::map<std::string, Image>& renderTargets)
	{
		auto vulkan = VulkanContext::Get();
		
		framebuffers.resize(vulkan->swapchain.images.size());
		for (size_t i = 0; i < vulkan->swapchain.images.size(); ++i)
		{
			uint32_t width = renderTargets["taa"].width;
			uint32_t height = renderTargets["taa"].height;
			vk::ImageView view = renderTargets["taa"].view;
			framebuffers[i].Create(width, height, view, renderPass);
		}
		
		framebuffersSharpen.resize(vulkan->swapchain.images.size());
		for (size_t i = 0; i < vulkan->swapchain.images.size(); ++i)
		{
			uint32_t width = renderTargets["viewport"].width;
			uint32_t height = renderTargets["viewport"].height;
			vk::ImageView view = renderTargets["viewport"].view;
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
		pipeline.info.colorBlendAttachments = make_sptr(
				std::vector<vk::PipelineColorBlendAttachmentState> {renderTargets["taa"].blendAttachment}
		);
		pipeline.info.descriptorSetLayouts = make_sptr(
				std::vector<vk::DescriptorSetLayout> {Pipeline::getDescriptorSetLayoutTAA()}
		);
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
		pipelineSharpen.info.colorBlendAttachments = make_sptr(
				std::vector<vk::PipelineColorBlendAttachmentState> {renderTargets["viewport"].blendAttachment}
		);
		pipelineSharpen.info.descriptorSetLayouts = make_sptr(
				std::vector<vk::DescriptorSetLayout> {Pipeline::getDescriptorSetLayoutTAASharpen()}
		);
		pipelineSharpen.info.renderPass = renderPassSharpen;
		
		pipelineSharpen.createGraphicsPipeline();
	}
	
	void TAA::saveImage(vk::CommandBuffer& cmd, Image& source) const
	{
		CommandBuffer cmdBuf = VkCommandBuffer(cmd);

		previous.TransitionImageLayout(
			cmdBuf,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_ACCESS_SHADER_READ_BIT,
			VK_ACCESS_TRANSFER_WRITE_BIT,
			VK_IMAGE_ASPECT_COLOR_BIT
		);
		source.TransitionImageLayout(
			cmdBuf,
			source.layoutState == LayoutState::ColorRead ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			source.layoutState == LayoutState::ColorRead ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			source.layoutState == LayoutState::ColorRead ? VK_ACCESS_SHADER_READ_BIT : VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			VK_ACCESS_TRANSFER_READ_BIT,
			VK_IMAGE_ASPECT_COLOR_BIT
		);
		
		// copy the image
		vk::ImageCopy region{};
		region.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
		region.srcSubresource.layerCount = 1;
		region.dstSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
		region.dstSubresource.layerCount = 1;
		region.extent.width = source.width;
		region.extent.height = source.height;
		region.extent.depth = 1;
		
		cmd.copyImage(
				source.image,
				vk::ImageLayout::eTransferSrcOptimal,
				previous.image,
				vk::ImageLayout::eTransferDstOptimal,
				region
		);
		
		previous.TransitionImageLayout(
			cmdBuf,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			VK_ACCESS_TRANSFER_WRITE_BIT,
			VK_ACCESS_SHADER_READ_BIT,
			VK_IMAGE_ASPECT_COLOR_BIT
		);
		source.TransitionImageLayout(
			cmdBuf,
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
			VULKAN.device->destroyDescriptorSetLayout(Pipeline::getDescriptorSetLayoutTAA());
			Pipeline::getDescriptorSetLayoutTAA() = nullptr;
		}
		if (Pipeline::getDescriptorSetLayoutTAASharpen())
		{
			VULKAN.device->destroyDescriptorSetLayout(Pipeline::getDescriptorSetLayoutTAASharpen());
			Pipeline::getDescriptorSetLayoutTAASharpen() = nullptr;
		}
		pipeline.destroy();
		pipelineSharpen.destroy();
	}
}
