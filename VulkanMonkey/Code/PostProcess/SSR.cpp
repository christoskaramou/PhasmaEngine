#include "vulkanPCH.h"
#include "SSR.h"
#include <deque>
#include "../GUI/GUI.h"
#include "../Swapchain/Swapchain.h"
#include "../Core/Surface.h"
#include "../Shader/Shader.h"
#include "../Core/Queue.h"
#include "../VulkanContext/VulkanContext.h"

namespace vm
{
	SSR::SSR()
	{
		DSet = vk::DescriptorSet();
	}

	SSR::~SSR()
	{
	}

	void SSR::createSSRUniforms(std::map<std::string, Image>& renderTargets)
	{
		UBReflection.createBuffer(4 * sizeof(mat4), vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostVisible);
		UBReflection.map();
		UBReflection.zero();
		UBReflection.flush();
		UBReflection.unmap();

		vk::DescriptorSetAllocateInfo allocateInfo2;
		allocateInfo2.descriptorPool = VulkanContext::get()->descriptorPool.Value();
		allocateInfo2.descriptorSetCount = 1;
		allocateInfo2.pSetLayouts = &Pipeline::getDescriptorSetLayoutSSR();
		DSet = VulkanContext::get()->device->allocateDescriptorSets(allocateInfo2).at(0);

		updateDescriptorSets(renderTargets);
	}

	void SSR::updateDescriptorSets(std::map<std::string, Image>& renderTargets)
	{
		std::deque<vk::DescriptorImageInfo> dsii{};
		const auto wSetImage = [&dsii](const vk::DescriptorSet& dstSet, uint32_t dstBinding, Image& image) {
			dsii.emplace_back(image.sampler.Value(), image.view.Value(), vk::ImageLayout::eShaderReadOnlyOptimal);
			return vk::WriteDescriptorSet{ dstSet, dstBinding, 0, 1, vk::DescriptorType::eCombinedImageSampler, &dsii.back(), nullptr, nullptr };
		};
		std::deque<vk::DescriptorBufferInfo> dsbi{};
		const auto wSetBuffer = [&dsbi](const vk::DescriptorSet& dstSet, uint32_t dstBinding, Buffer& buffer) {
			dsbi.emplace_back(buffer.buffer.Value(), 0, buffer.size.Value());
			return vk::WriteDescriptorSet{ dstSet, dstBinding, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &dsbi.back(), nullptr };
		};

		std::vector<vk::WriteDescriptorSet> textureWriteSets{
			wSetImage(DSet.Value(), 0, renderTargets["albedo"]),
			wSetImage(DSet.Value(), 1, renderTargets["depth"]),
			wSetImage(DSet.Value(), 2, renderTargets["normal"]),
			wSetImage(DSet.Value(), 3, renderTargets["srm"]),
			wSetBuffer(DSet.Value(), 4, UBReflection)
		};
		VulkanContext::get()->device->updateDescriptorSets(textureWriteSets, nullptr);
	}

	void SSR::update(Camera& camera)
	{
		if (GUI::show_ssr) {
			reflectionInput[0][0] = vec4(camera.position, 1.0f);
			reflectionInput[0][1] = vec4(camera.front, 1.0f);
			reflectionInput[0][2] = vec4();
			reflectionInput[0][3] = vec4();
			reflectionInput[1] = camera.projection;
			reflectionInput[2] = camera.view;
			reflectionInput[3] = camera.invProjection;

			Queue::memcpyRequest(&UBReflection, { { &reflectionInput, sizeof(reflectionInput), 0 } });
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

		std::vector<vk::ClearValue> clearValues = { clearColor };

		vk::RenderPassBeginInfo renderPassInfo;
		renderPassInfo.renderPass = *renderPass;
		renderPassInfo.framebuffer = *framebuffers[imageIndex];
		renderPassInfo.renderArea.offset = vk::Offset2D{ 0, 0 };
		renderPassInfo.renderArea.extent = extent;
		renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
		renderPassInfo.pClearValues = clearValues.data();

		cmd.beginRenderPass(&renderPassInfo, vk::SubpassContents::eInline);
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline.pipeline.Value());
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline.pipelineLayout.Value(), 0, DSet.Value(), nullptr);
		cmd.draw(3, 1, 0, 0);
		cmd.endRenderPass();
	}

	void SSR::createRenderPass(std::map<std::string, Image>& renderTargets)
	{
		renderPass.Create(renderTargets["ssr"].format.Value(), vk::Format::eUndefined);
	}

	void SSR::createFrameBuffers(std::map<std::string, Image>& renderTargets)
	{
		auto vulkan = VulkanContext::get();
		framebuffers.resize(vulkan->swapchain.images.size());
		for (size_t i = 0; i < vulkan->swapchain.images.size(); ++i)
		{
			uint32_t width = renderTargets["ssr"].width.Value();
			uint32_t height = renderTargets["ssr"].height.Value();
			vk::ImageView view = renderTargets["ssr"].view.Value();
			framebuffers[i].Create(width, height, view, renderPass);
		}
	}

	void SSR::createPipeline(std::map<std::string, Image>& renderTargets)
	{
		Shader vert{ "shaders/Common/quad.vert", ShaderType::Vertex, true };
		Shader frag{ "shaders/SSR/ssr.frag", ShaderType::Fragment, true };

		pipeline.info.pVertShader = &vert;
		pipeline.info.pFragShader = &frag;
		pipeline.info.width = renderTargets["ssr"].width_f.Value();
		pipeline.info.height = renderTargets["ssr"].height_f.Value();
		pipeline.info.cullMode = CullMode::Back;
		pipeline.info.colorBlendAttachments = { renderTargets["ssr"].blentAttachment.Value() };
		pipeline.info.descriptorSetLayouts = { Pipeline::getDescriptorSetLayoutSSR() };
		pipeline.info.renderPass = renderPass;

		pipeline.createGraphicsPipeline();
	}

	void SSR::destroy()
	{
		for (auto& framebuffer : framebuffers)
			framebuffer.Destroy();

		renderPass.Destroy();

		if (Pipeline::getDescriptorSetLayoutSSR()) {
			VulkanContext::get()->device->destroyDescriptorSetLayout(Pipeline::getDescriptorSetLayoutSSR());
			Pipeline::getDescriptorSetLayoutSSR() = nullptr;
		}
		UBReflection.destroy();
		pipeline.destroy();
	}
}