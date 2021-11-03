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

#include "SSR.h"
#include "GUI/GUI.h"
#include "Renderer/Swapchain.h"
#include "Renderer/Surface.h"
#include "Shader/Shader.h"
#include "Core/Queue.h"
#include "Renderer/RHI.h"
#include "Renderer/Command.h"
#include "Renderer/Descriptor.h"
#include "Renderer/Framebuffer.h"
#include "Renderer/Image.h"
#include "Renderer/RenderPass.h"
#include "Renderer/Pipeline.h"

namespace pe
{
	SSR::SSR()
	{
		DSet = {};
	}
	
	SSR::~SSR()
	{
	}
	
	void SSR::createSSRUniforms(std::map<std::string, Image*>& renderTargets)
	{
		UBReflection = Buffer::Create(
			4 * sizeof(mat4),
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
		UBReflection->Map();
		UBReflection->Zero();
		UBReflection->Flush();
		UBReflection->Unmap();

		DSet = Descriptor::Create(Pipeline::getDescriptorSetLayoutSSR());

		updateDescriptorSets(renderTargets);
	}
	
	void SSR::updateDescriptorSets(std::map<std::string, Image*>& renderTargets)
	{
		std::array<DescriptorUpdateInfo, 5> infos{};

		infos[0].binding = 0;
		infos[0].pImage = renderTargets["albedo"];

		infos[1].binding = 1;
		infos[1].pImage = RHII.depth;
		infos[1].imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

		infos[2].binding = 2;
		infos[2].pImage = renderTargets["normal"];

		infos[3].binding = 3;
		infos[3].pImage = renderTargets["srm"];

		infos[4].binding = 4;
		infos[4].pBuffer = UBReflection;

		DSet->UpdateDescriptor(5, infos.data());
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
	
	
	void SSR::draw(CommandBuffer* cmd, uint32_t imageIndex)
	{
		cmd->BeginPass(renderPass, framebuffers[imageIndex]);
		cmd->BindPipeline(pipeline);
		cmd->BindDescriptors(pipeline, 1, &DSet);
		cmd->Draw(3, 1, 0, 0);
		cmd->EndPass();
	}
	
	void SSR::createRenderPass(std::map<std::string, Image*>& renderTargets)
	{
		Attachment attachment{};
		attachment.format = renderTargets["ssr"]->imageInfo.format;
		renderPass = RenderPass::Create(attachment);
	}
	
	void SSR::createFrameBuffers(std::map<std::string, Image*>& renderTargets)
	{
		framebuffers.resize(RHII.swapchain->images.size());
		for (size_t i = 0; i < RHII.swapchain->images.size(); ++i)
		{
			uint32_t width = renderTargets["ssr"]->imageInfo.width;
			uint32_t height = renderTargets["ssr"]->imageInfo.height;
			ImageViewHandle view = renderTargets["ssr"]->view;
			framebuffers[i] = FrameBuffer::Create(width, height, view, renderPass);
		}
	}
	
	void SSR::createPipeline(std::map<std::string, Image*>& renderTargets)
	{
		Shader vert {"Shaders/Common/quad.vert", ShaderType::Vertex, true};
		Shader frag {"Shaders/SSR/ssr.frag", ShaderType::Fragment, true};
		
		PipelineCreateInfo info{};
		info.pVertShader = &vert;
		info.pFragShader = &frag;
		info.width = renderTargets["ssr"]->width_f;
		info.height = renderTargets["ssr"]->height_f;
		info.cullMode = CullMode::Back;
		info.colorBlendAttachments = { renderTargets["ssr"]->blendAttachment };
		info.descriptorSetLayouts = { Pipeline::getDescriptorSetLayoutSSR() };
		info.renderPass = renderPass;
		
		pipeline = Pipeline::Create(info);
	}
	
	void SSR::destroy()
	{
		for (auto framebuffer : framebuffers)
			framebuffer->Destroy();
		
		renderPass->Destroy();
		
		Pipeline::getDescriptorSetLayoutSSR()->Destroy();
		UBReflection->Destroy();
		pipeline->Destroy();
	}
}