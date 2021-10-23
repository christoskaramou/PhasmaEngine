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
#include "Bloom.h"
#include "GUI/GUI.h"
#include "Renderer/Swapchain.h"
#include "Renderer/Surface.h"
#include "Shader/Shader.h"
#include "Renderer/Vulkan/Vulkan.h"
#include "Renderer/CommandBuffer.h"
#include <deque>

namespace pe
{
	Bloom::Bloom()
	{
		DSBrightFilter = {};
		DSGaussianBlurHorizontal = {};
		DSGaussianBlurVertical = {};
		DSCombine = {};
	}
	
	Bloom::~Bloom()
	{
	}
	
	void Bloom::Init()
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
	
	void Bloom::createRenderPasses(std::map<std::string, Image>& renderTargets)
	{
		Attachment attachment{};
		attachment.format = renderTargets["brightFilter"].format;
		renderPassBrightFilter.Create(attachment);

		attachment.format = renderTargets["gaussianBlurHorizontal"].format;
		renderPassGaussianBlur.Create(attachment);

		attachment.format = renderTargets["viewport"].format;
		renderPassCombine.Create(attachment);
	}
	
	void Bloom::createFrameBuffers(std::map<std::string, Image>& renderTargets)
	{
		auto vulkan = VulkanContext::Get();
		framebuffers.resize(vulkan->swapchain.images.size() * 4);
		for (size_t i = 0; i < vulkan->swapchain.images.size(); ++i)
		{
			uint32_t width = renderTargets["brightFilter"].width;
			uint32_t height = renderTargets["brightFilter"].height;
			ImageViewHandle view = renderTargets["brightFilter"].view;
			framebuffers[i].Create(width, height, view, renderPassBrightFilter);
		}
		
		for (size_t i = vulkan->swapchain.images.size(); i < vulkan->swapchain.images.size() * 2; ++i)
		{
			uint32_t width = renderTargets["gaussianBlurHorizontal"].width;
			uint32_t height = renderTargets["gaussianBlurHorizontal"].height;
			ImageViewHandle view = renderTargets["gaussianBlurHorizontal"].view;
			framebuffers[i].Create(width, height, view, renderPassGaussianBlur);
		}
		
		for (size_t i = vulkan->swapchain.images.size() * 2; i < vulkan->swapchain.images.size() * 3; ++i)
		{
			uint32_t width = renderTargets["gaussianBlurVertical"].width;
			uint32_t height = renderTargets["gaussianBlurVertical"].height;
			ImageViewHandle view = renderTargets["gaussianBlurVertical"].view;
			framebuffers[i].Create(width, height, view, renderPassGaussianBlur);
		}
		
		for (size_t i = vulkan->swapchain.images.size() * 3; i < vulkan->swapchain.images.size() * 4; ++i)
		{
			uint32_t width = renderTargets["viewport"].width;
			uint32_t height = renderTargets["viewport"].height;
			ImageViewHandle view = renderTargets["viewport"].view;
			framebuffers[i].Create(width, height, view, renderPassCombine);
		}
	}
	
	void Bloom::createUniforms(std::map<std::string, Image>& renderTargets)
	{
		VkDescriptorSetAllocateInfo allocateInfo{};
		allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocateInfo.descriptorPool = VULKAN.descriptorPool;
		allocateInfo.descriptorSetCount = 1;

		VkDescriptorSet dset;

		// Composition image to Bright Filter shader
		VkDescriptorSetLayout dsetLayout = Pipeline::getDescriptorSetLayoutBrightFilter();
		allocateInfo.pSetLayouts = &dsetLayout;
		vkAllocateDescriptorSets(*VULKAN.device, &allocateInfo, &dset);
		DSBrightFilter = dset;
		
		// Bright Filter image to Gaussian Blur Horizontal shader
		dsetLayout = Pipeline::getDescriptorSetLayoutGaussianBlurH();
		allocateInfo.pSetLayouts = &dsetLayout;
		vkAllocateDescriptorSets(*VULKAN.device, &allocateInfo, &dset);
		DSGaussianBlurHorizontal = dset;
		
		// Gaussian Blur Horizontal image to Gaussian Blur Vertical shader
		dsetLayout = Pipeline::getDescriptorSetLayoutGaussianBlurV();
		allocateInfo.pSetLayouts = &dsetLayout;
		vkAllocateDescriptorSets(*VULKAN.device, &allocateInfo, &dset);
		DSGaussianBlurVertical = dset;
		
		// Gaussian Blur Vertical image to Combine shader
		dsetLayout = Pipeline::getDescriptorSetLayoutCombine();
		allocateInfo.pSetLayouts = &dsetLayout;
		vkAllocateDescriptorSets(*VULKAN.device, &allocateInfo, &dset);
		DSCombine = dset;
		
		updateDescriptorSets(renderTargets);
	}
	
	void Bloom::updateDescriptorSets(std::map<std::string, Image>& renderTargets)
	{
		std::deque<VkDescriptorImageInfo> dsii {};
		auto const wSetImage = [&dsii](DescriptorSetHandle dstSet, uint32_t dstBinding, Image& image)
		{
			VkDescriptorImageInfo info{ image.sampler, image.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
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
		
		std::vector<VkWriteDescriptorSet> textureWriteSets
		{
			wSetImage(DSBrightFilter, 0, frameImage),
			wSetImage(DSGaussianBlurHorizontal, 0, renderTargets["brightFilter"]),
			wSetImage(DSGaussianBlurVertical, 0, renderTargets["gaussianBlurHorizontal"]),
			wSetImage(DSCombine, 0, frameImage),
			wSetImage(DSCombine, 1, renderTargets["gaussianBlurVertical"])
		};

		vkUpdateDescriptorSets(*VULKAN.device, (uint32_t)textureWriteSets.size(), textureWriteSets.data(), 0, nullptr);
	}
	
	void Bloom::draw(CommandBuffer* cmd, uint32_t imageIndex, std::map<std::string, Image>& renderTargets)
	{
		uint32_t totalImages = static_cast<uint32_t>(VULKAN.swapchain.images.size());
		
		std::vector<float> values
		{
			GUI::Bloom_Inv_brightness,
			GUI::Bloom_intensity,
			GUI::Bloom_range,
			GUI::Bloom_exposure,
			static_cast<float>(GUI::use_tonemap)
		};
		
		renderTargets["brightFilter"].ChangeLayout(*cmd, LayoutState::ColorWrite);
		cmd->BeginPass(renderPassBrightFilter, framebuffers[imageIndex]);
		cmd->PushConstants(pipelineBrightFilter, VK_SHADER_STAGE_FRAGMENT_BIT, 0, uint32_t(sizeof(float) * values.size()), values.data());
		cmd->BindPipeline(pipelineBrightFilter);
		cmd->BindDescriptors(pipelineBrightFilter, 1, &DSBrightFilter);
		cmd->Draw(3, 1, 0, 0);
		cmd->EndPass();
		renderTargets["brightFilter"].ChangeLayout(*cmd, LayoutState::ColorRead);
		
		renderTargets["gaussianBlurHorizontal"].ChangeLayout(*cmd, LayoutState::ColorWrite);
		cmd->BeginPass(renderPassGaussianBlur, framebuffers[static_cast<size_t>(totalImages) + static_cast<size_t>(imageIndex)]);
		cmd->PushConstants(pipelineGaussianBlurHorizontal, VK_SHADER_STAGE_FRAGMENT_BIT, 0, uint32_t(sizeof(float) * values.size()), values.data());
		cmd->BindPipeline(pipelineGaussianBlurHorizontal);
		cmd->BindDescriptors(pipelineGaussianBlurHorizontal, 1, &DSGaussianBlurHorizontal);
		cmd->Draw(3, 1, 0, 0);
		cmd->EndPass();
		renderTargets["gaussianBlurHorizontal"].ChangeLayout(*cmd, LayoutState::ColorRead);
		
		renderTargets["gaussianBlurVertical"].ChangeLayout(*cmd, LayoutState::ColorWrite);
		cmd->BeginPass(renderPassGaussianBlur, framebuffers[static_cast<size_t>(totalImages) * 2 + static_cast<size_t>(imageIndex)]);
		cmd->PushConstants(pipelineGaussianBlurVertical, VK_SHADER_STAGE_FRAGMENT_BIT, 0, uint32_t(sizeof(float) * values.size()), values.data());
		cmd->BindPipeline(pipelineGaussianBlurVertical);
		cmd->BindDescriptors(pipelineGaussianBlurVertical, 1, &DSGaussianBlurVertical);
		cmd->Draw(3, 1, 0, 0);
		cmd->EndPass();
		renderTargets["gaussianBlurVertical"].ChangeLayout(*cmd, LayoutState::ColorRead);
		
		cmd->BeginPass(renderPassCombine, framebuffers[static_cast<size_t>(totalImages) * 3 + static_cast<size_t>(imageIndex)]);
		cmd->PushConstants(pipelineCombine, VK_SHADER_STAGE_FRAGMENT_BIT, 0, uint32_t(sizeof(float) * values.size()), values.data());
		cmd->BindPipeline(pipelineCombine);
		cmd->BindDescriptors(pipelineCombine, 1, &DSCombine);
		cmd->Draw(3, 1, 0, 0);
		cmd->EndPass();
	}
	
	void Bloom::createPipelines(std::map<std::string, Image>& renderTargets)
	{
		createBrightFilterPipeline(renderTargets);
		createGaussianBlurHorizontaPipeline(renderTargets);
		createGaussianBlurVerticalPipeline(renderTargets);
		createCombinePipeline(renderTargets);
	}
	
	void Bloom::createBrightFilterPipeline(std::map<std::string, Image>& renderTargets)
	{
		Shader vert {"Shaders/Common/quad.vert", ShaderType::Vertex, true};
		Shader frag {"Shaders/Bloom/brightFilter.frag", ShaderType::Fragment, true};
		
		pipelineBrightFilter.info.pVertShader = &vert;
		pipelineBrightFilter.info.pFragShader = &frag;
		pipelineBrightFilter.info.width = renderTargets["brightFilter"].width_f;
		pipelineBrightFilter.info.height = renderTargets["brightFilter"].height_f;
		pipelineBrightFilter.info.cullMode = CullMode::Back;
		pipelineBrightFilter.info.colorBlendAttachments = { renderTargets["brightFilter"].blendAttachment };
		pipelineBrightFilter.info.pushConstantStage = PushConstantStage::Fragment;
		pipelineBrightFilter.info.pushConstantSize = 5 * sizeof(float);
		pipelineBrightFilter.info.descriptorSetLayouts = { Pipeline::getDescriptorSetLayoutBrightFilter() };
		pipelineBrightFilter.info.renderPass = renderPassBrightFilter;
		
		pipelineBrightFilter.createGraphicsPipeline();
	}
	
	void Bloom::createGaussianBlurHorizontaPipeline(std::map<std::string, Image>& renderTargets)
	{
		Shader vert {"Shaders/Common/quad.vert", ShaderType::Vertex, true};
		Shader frag {"Shaders/Bloom/gaussianBlurHorizontal.frag", ShaderType::Fragment, true};
		
		pipelineGaussianBlurHorizontal.info.pVertShader = &vert;
		pipelineGaussianBlurHorizontal.info.pFragShader = &frag;
		pipelineGaussianBlurHorizontal.info.width = renderTargets["gaussianBlurHorizontal"].width_f;
		pipelineGaussianBlurHorizontal.info.height = renderTargets["gaussianBlurHorizontal"].height_f;
		pipelineGaussianBlurHorizontal.info.cullMode = CullMode::Back;
		pipelineGaussianBlurHorizontal.info.colorBlendAttachments = { renderTargets["gaussianBlurHorizontal"].blendAttachment };
		pipelineGaussianBlurHorizontal.info.pushConstantStage = PushConstantStage::Fragment;
		pipelineGaussianBlurHorizontal.info.pushConstantSize = 5 * sizeof(float);
		pipelineGaussianBlurHorizontal.info.descriptorSetLayouts = { Pipeline::getDescriptorSetLayoutGaussianBlurH() };
		pipelineGaussianBlurHorizontal.info.renderPass = renderPassGaussianBlur;
		
		pipelineGaussianBlurHorizontal.createGraphicsPipeline();
	}
	
	void Bloom::createGaussianBlurVerticalPipeline(std::map<std::string, Image>& renderTargets)
	{
		Shader vert {"Shaders/Common/quad.vert", ShaderType::Vertex, true};
		Shader frag {"Shaders/Bloom/gaussianBlurVertical.frag", ShaderType::Fragment, true};
		
		pipelineGaussianBlurVertical.info.pVertShader = &vert;
		pipelineGaussianBlurVertical.info.pFragShader = &frag;
		pipelineGaussianBlurVertical.info.width = renderTargets["gaussianBlurVertical"].width_f;
		pipelineGaussianBlurVertical.info.height = renderTargets["gaussianBlurVertical"].height_f;
		pipelineGaussianBlurVertical.info.cullMode = CullMode::Back;
		pipelineGaussianBlurVertical.info.colorBlendAttachments = { renderTargets["gaussianBlurVertical"].blendAttachment };
		pipelineGaussianBlurVertical.info.pushConstantStage = PushConstantStage::Fragment;
		pipelineGaussianBlurVertical.info.pushConstantSize = 5 * sizeof(float);
		pipelineGaussianBlurVertical.info.descriptorSetLayouts = { Pipeline::getDescriptorSetLayoutGaussianBlurV() };
		pipelineGaussianBlurVertical.info.renderPass = renderPassGaussianBlur;
		
		pipelineGaussianBlurVertical.createGraphicsPipeline();
	}
	
	void Bloom::createCombinePipeline(std::map<std::string, Image>& renderTargets)
	{
		// Shader stages
		Shader vert {"Shaders/Common/quad.vert", ShaderType::Vertex, true};
		Shader frag {"Shaders/Bloom/combine.frag", ShaderType::Fragment, true};
		
		pipelineCombine.info.pVertShader = &vert;
		pipelineCombine.info.pFragShader = &frag;
		pipelineCombine.info.width = renderTargets["viewport"].width_f;
		pipelineCombine.info.height = renderTargets["viewport"].height_f;
		pipelineCombine.info.cullMode = CullMode::Back;
		pipelineCombine.info.colorBlendAttachments = { renderTargets["viewport"].blendAttachment };
		pipelineCombine.info.pushConstantStage = PushConstantStage::Fragment;
		pipelineCombine.info.pushConstantSize = 5 * sizeof(float);
		pipelineCombine.info.descriptorSetLayouts = { Pipeline::getDescriptorSetLayoutCombine() };
		pipelineCombine.info.renderPass = renderPassCombine;
		
		pipelineCombine.createGraphicsPipeline();
	}
	
	void Bloom::destroy()
	{
		for (auto& frameBuffer : framebuffers)
			frameBuffer.Destroy();
		
		renderPassBrightFilter.Destroy();
		renderPassGaussianBlur.Destroy();
		renderPassCombine.Destroy();
		
		if (Pipeline::getDescriptorSetLayoutBrightFilter())
		{
			vkDestroyDescriptorSetLayout(*VULKAN.device, Pipeline::getDescriptorSetLayoutBrightFilter(), nullptr);
			Pipeline::getDescriptorSetLayoutBrightFilter() = {};
		}
		if (Pipeline::getDescriptorSetLayoutGaussianBlurH())
		{
			vkDestroyDescriptorSetLayout(*VULKAN.device, Pipeline::getDescriptorSetLayoutGaussianBlurH(), nullptr);
			Pipeline::getDescriptorSetLayoutGaussianBlurH() = {};
		}
		if (Pipeline::getDescriptorSetLayoutGaussianBlurV())
		{
			vkDestroyDescriptorSetLayout(*VULKAN.device, Pipeline::getDescriptorSetLayoutGaussianBlurV(), nullptr);
			Pipeline::getDescriptorSetLayoutGaussianBlurV() = {};
		}
		if (Pipeline::getDescriptorSetLayoutCombine())
		{
			vkDestroyDescriptorSetLayout(*VULKAN.device, Pipeline::getDescriptorSetLayoutCombine(), nullptr);
			Pipeline::getDescriptorSetLayoutCombine() = {};
		}
		frameImage.Destroy();
		pipelineBrightFilter.destroy();
		pipelineGaussianBlurHorizontal.destroy();
		pipelineGaussianBlurVertical.destroy();
		pipelineCombine.destroy();
	}
}
