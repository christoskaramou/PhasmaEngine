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
#include "Renderer/RHI.h"
#include "Renderer/Command.h"
#include "Renderer/Descriptor.h"
#include "Renderer/Framebuffer.h"
#include "Renderer/Image.h"
#include "Renderer/RenderPass.h"
#include "Renderer/Pipeline.h"
#include "Renderer/Buffer.h"

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
		ImageCreateInfo info{};
		info.format = RHII.surface->format;
		info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		info.width = static_cast<uint32_t>(WIDTH_f * GUI::renderTargetsScale);
		info.height = static_cast<uint32_t>(HEIGHT_f * GUI::renderTargetsScale);
		info.tiling = VK_IMAGE_TILING_OPTIMAL;
		info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		info.properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		frameImage = Image::Create(info);

		ImageViewCreateInfo viewInfo{};
		viewInfo.image = frameImage;
		frameImage->CreateImageView(viewInfo);

		SamplerCreateInfo samplerInfo{};
		frameImage->CreateSampler(samplerInfo);

		frameImage->TransitionImageLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	}
	
	void MotionBlur::createMotionBlurUniforms(std::map<std::string, Image*>& renderTargets)
	{
		auto size = 4 * sizeof(mat4);
		UBmotionBlur = Buffer::Create(size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
		UBmotionBlur->Map();
		UBmotionBlur->Zero();
		UBmotionBlur->Flush();
		UBmotionBlur->Unmap();

		DSet = Descriptor::Create(Pipeline::getDescriptorSetLayoutMotionBlur());
		updateDescriptorSets(renderTargets);
	}
	
	void MotionBlur::updateDescriptorSets(std::map<std::string, Image*>& renderTargets)
	{
		std::array<DescriptorUpdateInfo, 4> infos{};

		infos[0].binding = 0;
		infos[0].pImage = frameImage;

		infos[1].binding = 1;
		infos[1].pImage = RHII.depth;
		infos[1].imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

		infos[2].binding = 2;
		infos[2].pImage = renderTargets["velocity"];

		infos[3].binding = 3;
		infos[3].pBuffer = UBmotionBlur;

		DSet->UpdateDescriptor(4, infos.data());
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
	
	void MotionBlur::createRenderPass(std::map<std::string, Image*>& renderTargets)
	{
		Attachment attachment{};
		attachment.format = renderTargets["viewport"]->imageInfo.format;
		renderPass = RenderPass::Create(attachment);
	}
	
	void MotionBlur::createFrameBuffers(std::map<std::string, Image*>& renderTargets)
	{
		framebuffers.resize(RHII.swapchain->images.size());
		for (size_t i = 0; i < RHII.swapchain->images.size(); ++i)
		{
			uint32_t width = renderTargets["viewport"]->imageInfo.width;
			uint32_t height = renderTargets["viewport"]->imageInfo.height;
			ImageViewHandle view = renderTargets["viewport"]->view;
			framebuffers[i] = FrameBuffer::Create(width, height, view, renderPass);
		}
	}
	
	void MotionBlur::createPipeline(std::map<std::string, Image*>& renderTargets)
	{
		// Shader stages
		Shader vert {"Shaders/Common/quad.vert", ShaderType::Vertex, true};
		Shader frag {"Shaders/MotionBlur/motionBlur.frag", ShaderType::Fragment, true};
		
		PipelineCreateInfo info{};
		info.pVertShader = &vert;
		info.pFragShader = &frag;
		info.width = renderTargets["viewport"]->width_f;
		info.height = renderTargets["viewport"]->height_f;
		info.cullMode = CullMode::Back;
		info.colorBlendAttachments = { renderTargets["viewport"]->blendAttachment };
		info.pushConstantStage = PushConstantStage::Fragment;
		info.pushConstantSize = sizeof(vec4);
		info.descriptorSetLayouts = { Pipeline::getDescriptorSetLayoutMotionBlur() };
		info.renderPass = renderPass;
		
		pipeline = Pipeline::Create(info);
	}

	void MotionBlur::destroy()
	{
		for (auto frameBuffer : framebuffers)
			frameBuffer->Destroy();

		renderPass->Destroy();

		Pipeline::getDescriptorSetLayoutMotionBlur()->Destroy();
		frameImage->Destroy();
		UBmotionBlur->Destroy();
		pipeline->Destroy();
	}
}
