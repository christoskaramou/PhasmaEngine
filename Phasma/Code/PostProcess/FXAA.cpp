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

#include "FXAA.h"
#include "GUI/GUI.h"
#include "Renderer/Swapchain.h"
#include "Renderer/Surface.h"
#include "Shader/Shader.h"
#include "Renderer/RHI.h"
#include "Renderer/Command.h"
#include "Renderer/Descriptor.h"

namespace pe
{
	FXAA::FXAA()
	{
		DSet = {};
	}
	
	FXAA::~FXAA()
	{
	}
	
	void FXAA::Init()
	{
		frameImage.format = RHII.surface.format;
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
	
	void FXAA::createUniforms(std::map<std::string, Image>& renderTargets)
	{
		DSet = Descriptor::Create(Pipeline::getDescriptorSetLayoutFXAA());
		updateDescriptorSets(renderTargets);
	}
	
	void FXAA::updateDescriptorSets(std::map<std::string, Image>& renderTargets)
	{
		DescriptorUpdateInfo info{};
		info.binding = 0;
		info.pImage = &frameImage;
		DSet->UpdateDescriptor(1, &info);
	}
	
	void FXAA::draw(CommandBuffer* cmd, uint32_t imageIndex)
	{
		cmd->BeginPass(&renderPass, &framebuffers[imageIndex]);
		cmd->BindPipeline(&pipeline);
		cmd->BindDescriptors(&pipeline, 1, &DSet);
		cmd->Draw(3, 1, 0, 0);
		cmd->EndPass();
	}
	
	void FXAA::createRenderPass(std::map<std::string, Image>& renderTargets)
	{
		Attachment attachment{};
		attachment.format = renderTargets["viewport"].format;
		renderPass.Create(attachment);
	}
	
	void FXAA::createFrameBuffers(std::map<std::string, Image>& renderTargets)
	{
		framebuffers.resize(RHII.swapchain.images.size());
		for (size_t i = 0; i < RHII.swapchain.images.size(); ++i)
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
		pipeline.info.colorBlendAttachments = { renderTargets["viewport"].blendAttachment };
		pipeline.info.descriptorSetLayouts = { Pipeline::getDescriptorSetLayoutFXAA() };
		pipeline.info.renderPass = renderPass;
		
		pipeline.createGraphicsPipeline();
	}
	
	void FXAA::destroy()
	{
		for (auto& frameBuffer : framebuffers)
			frameBuffer.Destroy();

		Pipeline::getDescriptorSetLayoutFXAA()->Destroy();
		renderPass.Destroy();
		frameImage.Destroy();
		pipeline.destroy();
	}
}
