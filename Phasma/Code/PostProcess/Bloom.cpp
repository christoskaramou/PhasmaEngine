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

#include "Bloom.h"
#include "GUI/GUI.h"
#include "Renderer/Swapchain.h"
#include "Renderer/Surface.h"
#include "Shader/Shader.h"
#include "Renderer/RHI.h"
#include "Renderer/Command.h"
#include "Renderer/Framebuffer.h"
#include "Renderer/Descriptor.h"

namespace pe
{
	Bloom::Bloom()
	{
	}
	
	Bloom::~Bloom()
	{
	}
	
	void Bloom::Init()
	{
		ImageCreateInfo info{};
		info.format = RHII.surface.format;
		info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		info.width = static_cast<uint32_t>(WIDTH_f * GUI::renderTargetsScale);
		info.height = static_cast<uint32_t>(HEIGHT_f * GUI::renderTargetsScale);
		info.tiling = VK_IMAGE_TILING_OPTIMAL;
		info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		info.properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		frameImage.CreateImage(info);

		ImageViewCreateInfo viewInfo{};
		viewInfo.image = &frameImage;
		frameImage.CreateImageView(viewInfo);

		SamplerCreateInfo samplerInfo{};
		frameImage.CreateSampler(samplerInfo);

		frameImage.TransitionImageLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	}
	
	void Bloom::createRenderPasses(std::map<std::string, Image>& renderTargets)
	{
		Attachment attachment{};
		attachment.format = renderTargets["brightFilter"].imageInfo.format;
		renderPassBrightFilter.Create(attachment);

		attachment.format = renderTargets["gaussianBlurHorizontal"].imageInfo.format;
		renderPassGaussianBlur.Create(attachment);

		attachment.format = renderTargets["viewport"].imageInfo.format;
		renderPassCombine.Create(attachment);
	}
	
	void Bloom::createFrameBuffers(std::map<std::string, Image>& renderTargets)
	{
		framebuffers.resize(RHII.swapchain.images.size() * 4);
		for (size_t i = 0; i < RHII.swapchain.images.size(); ++i)
		{
			uint32_t width = renderTargets["brightFilter"].imageInfo.width;
			uint32_t height = renderTargets["brightFilter"].imageInfo.height;
			ImageViewHandle view = renderTargets["brightFilter"].view;
			framebuffers[i] = FrameBuffer::Create(width, height, view, renderPassBrightFilter);
		}
		
		for (size_t i = RHII.swapchain.images.size(); i < RHII.swapchain.images.size() * 2; ++i)
		{
			uint32_t width = renderTargets["gaussianBlurHorizontal"].imageInfo.width;
			uint32_t height = renderTargets["gaussianBlurHorizontal"].imageInfo.height;
			ImageViewHandle view = renderTargets["gaussianBlurHorizontal"].view;
			framebuffers[i] = FrameBuffer::Create(width, height, view, renderPassGaussianBlur);
		}
		
		for (size_t i = RHII.swapchain.images.size() * 2; i < RHII.swapchain.images.size() * 3; ++i)
		{
			uint32_t width = renderTargets["gaussianBlurVertical"].imageInfo.width;
			uint32_t height = renderTargets["gaussianBlurVertical"].imageInfo.height;
			ImageViewHandle view = renderTargets["gaussianBlurVertical"].view;
			framebuffers[i] = FrameBuffer::Create(width, height, view, renderPassGaussianBlur);
		}
		
		for (size_t i = RHII.swapchain.images.size() * 3; i < RHII.swapchain.images.size() * 4; ++i)
		{
			uint32_t width = renderTargets["viewport"].imageInfo.width;
			uint32_t height = renderTargets["viewport"].imageInfo.height;
			ImageViewHandle view = renderTargets["viewport"].view;
			framebuffers[i] = FrameBuffer::Create(width, height, view, renderPassCombine);
		}
	}
	
	void Bloom::createUniforms(std::map<std::string, Image>& renderTargets)
	{
		DSBrightFilter = Descriptor::Create(Pipeline::getDescriptorSetLayoutBrightFilter());
		DSGaussianBlurHorizontal = Descriptor::Create(Pipeline::getDescriptorSetLayoutGaussianBlurH());
		DSGaussianBlurVertical = Descriptor::Create(Pipeline::getDescriptorSetLayoutGaussianBlurV());
		DSCombine = Descriptor::Create(Pipeline::getDescriptorSetLayoutCombine());
		
		updateDescriptorSets(renderTargets);
	}
	
	void Bloom::updateDescriptorSets(std::map<std::string, Image>& renderTargets)
	{
		DescriptorUpdateInfo info{};
		std::array<DescriptorUpdateInfo, 2> infos{};

		info.binding = 0;
		info.pImage = &frameImage;
		DSBrightFilter->UpdateDescriptor(1, &info);

		info.binding = 0;
		info.pImage = &renderTargets["brightFilter"];
		DSGaussianBlurHorizontal->UpdateDescriptor(1, &info);

		info.binding = 0;
		info.pImage = &renderTargets["gaussianBlurHorizontal"];
		DSGaussianBlurVertical->UpdateDescriptor(1, &info);

		infos[0].binding = 0;
		infos[0].pImage = &frameImage;
		infos[1].binding = 1;
		infos[1].pImage = &renderTargets["gaussianBlurVertical"];
		DSCombine->UpdateDescriptor(2, infos.data());
	}
	
	void Bloom::draw(CommandBuffer* cmd, uint32_t imageIndex, std::map<std::string, Image>& renderTargets)
	{
		uint32_t totalImages = static_cast<uint32_t>(RHII.swapchain.images.size());
		
		std::vector<float> values
		{
			GUI::Bloom_Inv_brightness,
			GUI::Bloom_intensity,
			GUI::Bloom_range,
			GUI::Bloom_exposure,
			static_cast<float>(GUI::use_tonemap)
		};
		
		renderTargets["brightFilter"].ChangeLayout(cmd, LayoutState::ColorWrite);
		cmd->BeginPass(&renderPassBrightFilter, framebuffers[imageIndex]);
		cmd->PushConstants(&pipelineBrightFilter, VK_SHADER_STAGE_FRAGMENT_BIT, 0, uint32_t(sizeof(float) * values.size()), values.data());
		cmd->BindPipeline(&pipelineBrightFilter);
		cmd->BindDescriptors(&pipelineBrightFilter, 1, &DSBrightFilter);
		cmd->Draw(3, 1, 0, 0);
		cmd->EndPass();
		renderTargets["brightFilter"].ChangeLayout(cmd, LayoutState::ColorRead);
		
		renderTargets["gaussianBlurHorizontal"].ChangeLayout(cmd, LayoutState::ColorWrite);
		cmd->BeginPass(&renderPassGaussianBlur, framebuffers[static_cast<size_t>(totalImages) + static_cast<size_t>(imageIndex)]);
		cmd->PushConstants(&pipelineGaussianBlurHorizontal, VK_SHADER_STAGE_FRAGMENT_BIT, 0, uint32_t(sizeof(float) * values.size()), values.data());
		cmd->BindPipeline(&pipelineGaussianBlurHorizontal);
		cmd->BindDescriptors(&pipelineGaussianBlurHorizontal, 1, &DSGaussianBlurHorizontal);
		cmd->Draw(3, 1, 0, 0);
		cmd->EndPass();
		renderTargets["gaussianBlurHorizontal"].ChangeLayout(cmd, LayoutState::ColorRead);
		
		renderTargets["gaussianBlurVertical"].ChangeLayout(cmd, LayoutState::ColorWrite);
		cmd->BeginPass(&renderPassGaussianBlur, framebuffers[static_cast<size_t>(totalImages) * 2 + static_cast<size_t>(imageIndex)]);
		cmd->PushConstants(&pipelineGaussianBlurVertical, VK_SHADER_STAGE_FRAGMENT_BIT, 0, uint32_t(sizeof(float) * values.size()), values.data());
		cmd->BindPipeline(&pipelineGaussianBlurVertical);
		cmd->BindDescriptors(&pipelineGaussianBlurVertical, 1, &DSGaussianBlurVertical);
		cmd->Draw(3, 1, 0, 0);
		cmd->EndPass();
		renderTargets["gaussianBlurVertical"].ChangeLayout(cmd, LayoutState::ColorRead);
		
		cmd->BeginPass(&renderPassCombine, framebuffers[static_cast<size_t>(totalImages) * 3 + static_cast<size_t>(imageIndex)]);
		cmd->PushConstants(&pipelineCombine, VK_SHADER_STAGE_FRAGMENT_BIT, 0, uint32_t(sizeof(float) * values.size()), values.data());
		cmd->BindPipeline(&pipelineCombine);
		cmd->BindDescriptors(&pipelineCombine, 1, &DSCombine);
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
		for (auto frameBuffer : framebuffers)
			frameBuffer->Destroy();
		
		renderPassBrightFilter.Destroy();
		renderPassGaussianBlur.Destroy();
		renderPassCombine.Destroy();
		
		Pipeline::getDescriptorSetLayoutBrightFilter()->Destroy();
		Pipeline::getDescriptorSetLayoutGaussianBlurH()->Destroy();
		Pipeline::getDescriptorSetLayoutGaussianBlurV()->Destroy();
		Pipeline::getDescriptorSetLayoutCombine()->Destroy();
		frameImage.Destroy();
		pipelineBrightFilter.destroy();
		pipelineGaussianBlurHorizontal.destroy();
		pipelineGaussianBlurVertical.destroy();
		pipelineCombine.destroy();
	}
}
