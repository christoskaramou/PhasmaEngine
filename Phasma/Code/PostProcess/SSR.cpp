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
#include "SSR.h"
#include <deque>
#include "GUI/GUI.h"
#include "Renderer/Swapchain.h"
#include "Renderer/Surface.h"
#include "Shader/Shader.h"
#include "Core/Queue.h"
#include "Renderer/Vulkan/Vulkan.h"

namespace pe
{
	SSR::SSR()
	{
		DSet = make_sptr(vk::DescriptorSet());
	}
	
	SSR::~SSR()
	{
	}
	
	void SSR::createSSRUniforms(std::map<std::string, Image>& renderTargets)
	{
		UBReflection = Buffer::Create(
			4 * sizeof(mat4),
			(BufferUsageFlags)vk::BufferUsageFlagBits::eUniformBuffer,
			(MemoryPropertyFlags)vk::MemoryPropertyFlagBits::eHostVisible);
		UBReflection->Map();
		UBReflection->Zero();
		UBReflection->Flush();
		UBReflection->Unmap();
		UBReflection->SetDebugName("SSR_UB_Reflection");
		
		vk::DescriptorSetAllocateInfo allocateInfo2;
		allocateInfo2.descriptorPool = *VULKAN.descriptorPool;
		allocateInfo2.descriptorSetCount = 1;
		allocateInfo2.pSetLayouts = &(vk::DescriptorSetLayout)Pipeline::getDescriptorSetLayoutSSR();
		DSet = make_sptr(VULKAN.device->allocateDescriptorSets(allocateInfo2).at(0));
		VULKAN.SetDebugObjectName(*DSet, "SSR");
		
		updateDescriptorSets(renderTargets);
	}
	
	void SSR::updateDescriptorSets(std::map<std::string, Image>& renderTargets)
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
		
		std::vector<vk::WriteDescriptorSet> textureWriteSets {
				wSetImage(*DSet, 0, renderTargets["albedo"]),
				wSetImage(*DSet, 1, VULKAN.depth, vk::ImageLayout::eDepthStencilReadOnlyOptimal),
				wSetImage(*DSet, 2, renderTargets["normal"]),
				wSetImage(*DSet, 3, renderTargets["srm"]),
				wSetBuffer(*DSet, 4, *UBReflection)
		};
		VULKAN.device->updateDescriptorSets(textureWriteSets, nullptr);
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
	
	
	void SSR::draw(vk::CommandBuffer cmd, uint32_t imageIndex, const vk::Extent2D& extent)
	{
		const vec4 color(0.0f, 0.0f, 0.0f, 1.0f);
		vk::ClearValue clearColor;
		memcpy(clearColor.color.float32, &color, sizeof(vec4));
		
		std::vector<vk::ClearValue> clearValues = {clearColor};
		
		vk::RenderPassBeginInfo renderPassInfo;
		renderPassInfo.renderPass = renderPass.handle;
		renderPassInfo.framebuffer = framebuffers[imageIndex].handle;
		renderPassInfo.renderArea.offset = vk::Offset2D {0, 0};
		renderPassInfo.renderArea.extent = extent;
		renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
		renderPassInfo.pClearValues = clearValues.data();
		
		cmd.beginRenderPass(&renderPassInfo, vk::SubpassContents::eInline);
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline.handle);
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipeline.layout, 0, *DSet, nullptr);
		cmd.draw(3, 1, 0, 0);
		cmd.endRenderPass();
	}
	
	void SSR::createRenderPass(std::map<std::string, Image>& renderTargets)
	{
		Attachment attachment{};
		attachment.format = renderTargets["ssr"].format;
		renderPass.Create(attachment);
	}
	
	void SSR::createFrameBuffers(std::map<std::string, Image>& renderTargets)
	{
		auto vulkan = VulkanContext::Get();
		framebuffers.resize(vulkan->swapchain.images.size());
		for (size_t i = 0; i < vulkan->swapchain.images.size(); ++i)
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
		pipeline.info.colorBlendAttachments = make_sptr(
				std::vector<vk::PipelineColorBlendAttachmentState> {renderTargets["ssr"].blendAttachment}
		);
		pipeline.info.descriptorSetLayouts = make_sptr(
				std::vector<vk::DescriptorSetLayout> {(vk::DescriptorSetLayout)Pipeline::getDescriptorSetLayoutSSR()}
		);
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
			vkDestroyDescriptorSetLayout(*VULKAN.device, Pipeline::getDescriptorSetLayoutSSR(), nullptr);
			Pipeline::getDescriptorSetLayoutSSR() = {};
		}
		UBReflection->Destroy();
		pipeline.destroy();
	}
}