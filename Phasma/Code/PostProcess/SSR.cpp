#include "PhasmaPch.h"
#include "SSR.h"
#include <deque>
#include "../GUI/GUI.h"
#include "../Renderer/Swapchain.h"
#include "../Renderer/Surface.h"
#include "../Shader/Shader.h"
#include "../Core/Queue.h"
#include "../Renderer/RenderApi.h"

namespace pe
{
	SSR::SSR()
	{
		DSet = make_ref(vk::DescriptorSet());
	}
	
	SSR::~SSR()
	{
	}
	
	void SSR::createSSRUniforms(std::map<std::string, Image>& renderTargets)
	{
		UBReflection.CreateBuffer(4 * sizeof(mat4), BufferUsage::UniformBuffer, MemoryProperty::HostVisible
		);
		UBReflection.Map();
		UBReflection.Zero();
		UBReflection.Flush();
		UBReflection.Unmap();
		
		vk::DescriptorSetAllocateInfo allocateInfo2;
		allocateInfo2.descriptorPool = *VulkanContext::Get()->descriptorPool;
		allocateInfo2.descriptorSetCount = 1;
		allocateInfo2.pSetLayouts = &Pipeline::getDescriptorSetLayoutSSR();
		DSet = make_ref(VulkanContext::Get()->device->allocateDescriptorSets(allocateInfo2).at(0));
		
		updateDescriptorSets(renderTargets);
	}
	
	void SSR::updateDescriptorSets(std::map<std::string, Image>& renderTargets)
	{
		std::deque<vk::DescriptorImageInfo> dsii {};
		const auto wSetImage = [&dsii](const vk::DescriptorSet& dstSet, uint32_t dstBinding, Image& image)
		{
			dsii.emplace_back(*image.sampler, *image.view, vk::ImageLayout::eShaderReadOnlyOptimal);
			return vk::WriteDescriptorSet {
					dstSet, dstBinding, 0, 1, vk::DescriptorType::eCombinedImageSampler, &dsii.back(), nullptr, nullptr
			};
		};
		std::deque<vk::DescriptorBufferInfo> dsbi {};
		const auto wSetBuffer = [&dsbi](const vk::DescriptorSet& dstSet, uint32_t dstBinding, Buffer& buffer)
		{
			dsbi.emplace_back(*buffer.GetBufferVK(), 0, buffer.Size());
			return vk::WriteDescriptorSet {
					dstSet, dstBinding, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &dsbi.back(), nullptr
			};
		};
		
		std::vector<vk::WriteDescriptorSet> textureWriteSets {
				wSetImage(*DSet, 0, renderTargets["albedo"]),
				wSetImage(*DSet, 1, renderTargets["depth"]),
				wSetImage(*DSet, 2, renderTargets["normal"]),
				wSetImage(*DSet, 3, renderTargets["srm"]),
				wSetBuffer(*DSet, 4, UBReflection)
		};
		VulkanContext::Get()->device->updateDescriptorSets(textureWriteSets, nullptr);
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
			
			Queue::memcpyRequest(&UBReflection, {{&reflectionInput, sizeof(reflectionInput), 0}});
			//UBReflection.map();
			//memcpy(UBReflection.data, &reflectionInput, sizeof(reflectionInput));
			//UBReflection.flush();
			//UBReflection.unmap();
		}
	}
	
	
	void SSR::draw(vk::CommandBuffer cmd, uint32_t imageIndex, const vk::Extent2D& extent)
	{
		vk::ClearValue clearColor;
		memcpy(clearColor.color.float32, GUI::clearColor.data(), 4 * sizeof(float));
		
		std::vector<vk::ClearValue> clearValues = {clearColor};
		
		vk::RenderPassBeginInfo renderPassInfo;
		renderPassInfo.renderPass = *renderPass.handle;
		renderPassInfo.framebuffer = *framebuffers[imageIndex].handle;
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
		renderPass.Create(*renderTargets["ssr"].format, vk::Format::eUndefined);
	}
	
	void SSR::createFrameBuffers(std::map<std::string, Image>& renderTargets)
	{
		auto vulkan = VulkanContext::Get();
		framebuffers.resize(vulkan->swapchain.images.size());
		for (size_t i = 0; i < vulkan->swapchain.images.size(); ++i)
		{
			uint32_t width = renderTargets["ssr"].width;
			uint32_t height = renderTargets["ssr"].height;
			vk::ImageView view = *renderTargets["ssr"].view;
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
		pipeline.info.colorBlendAttachments = make_ref(
				std::vector<vk::PipelineColorBlendAttachmentState> {*renderTargets["ssr"].blentAttachment}
		);
		pipeline.info.descriptorSetLayouts = make_ref(
				std::vector<vk::DescriptorSetLayout> {Pipeline::getDescriptorSetLayoutSSR()}
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
			VulkanContext::Get()->device->destroyDescriptorSetLayout(Pipeline::getDescriptorSetLayoutSSR());
			Pipeline::getDescriptorSetLayoutSSR() = nullptr;
		}
		UBReflection.Destroy();
		pipeline.destroy();
	}
}