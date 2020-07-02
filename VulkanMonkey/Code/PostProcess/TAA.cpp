#include "vulkanPCH.h"
#include "TAA.h"
#include "../GUI/GUI.h"
#include "../Core/Surface.h"
#include "../Swapchain/Swapchain.h"
#include "../Shader/Shader.h"
#include "../Core/Queue.h"
#include "../VulkanContext/VulkanContext.h"
#include <deque>

namespace vm
{
	TAA::TAA()
	{
		DSet = make_ref(vk::DescriptorSet());
		DSetSharpen = make_ref(vk::DescriptorSet());
	}

	TAA::~TAA()
	{
	}

	void TAA::Init()
	{
		previous.format = make_ref(VulkanContext::get()->surface.formatKHR->format);
		previous.initialLayout = make_ref(vk::ImageLayout::eUndefined);
		previous.createImage(
			static_cast<uint32_t>(WIDTH_f * GUI::renderTargetsScale),
			static_cast<uint32_t>(HEIGHT_f * GUI::renderTargetsScale),
			vk::ImageTiling::eOptimal,
			vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled, vk::MemoryPropertyFlagBits::eDeviceLocal);
		previous.transitionImageLayout(vk::ImageLayout::eUndefined, vk::ImageLayout::eShaderReadOnlyOptimal);
		previous.createImageView(vk::ImageAspectFlagBits::eColor);
		previous.createSampler();

		frameImage.format = make_ref(VulkanContext::get()->surface.formatKHR->format);
		frameImage.initialLayout = make_ref(vk::ImageLayout::eUndefined);
		frameImage.createImage(
			static_cast<uint32_t>(WIDTH_f * GUI::renderTargetsScale),
			static_cast<uint32_t>(HEIGHT_f * GUI::renderTargetsScale),
			vk::ImageTiling::eOptimal,
			vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
			vk::MemoryPropertyFlagBits::eDeviceLocal);
		frameImage.transitionImageLayout(vk::ImageLayout::eUndefined, vk::ImageLayout::eShaderReadOnlyOptimal);
		frameImage.createImageView(vk::ImageAspectFlagBits::eColor);
		frameImage.createSampler();
	}

	void TAA::update(const Camera& camera)
	{
		if (GUI::use_TAA) {
			ubo.values = { camera.projOffset.x, camera.projOffset.y, GUI::TAA_feedback_min, GUI::TAA_feedback_max };
			ubo.sharpenValues = { GUI::TAA_sharp_strength, GUI::TAA_sharp_clamp, GUI::TAA_sharp_offset_bias , sin(static_cast<float>(ImGui::GetTime()) * 0.125f) };
			ubo.invProj = camera.invProjection;

			Queue::memcpyRequest(&uniform, { { &ubo, sizeof(ubo), 0 } });
			//uniform.map();
			//memcpy(uniform.data, &ubo, sizeof(ubo));
			//uniform.flush();
			//uniform.unmap();
		}
	}

	void TAA::createUniforms(std::map<std::string, Image>& renderTargets)
	{
		uniform.createBuffer(sizeof(UBO), vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostVisible);
		uniform.map();
		uniform.zero();
		uniform.flush();
		uniform.unmap();

		vk::DescriptorSetAllocateInfo allocateInfo2;
		allocateInfo2.descriptorPool = *VulkanContext::get()->descriptorPool;
		allocateInfo2.descriptorSetCount = 1;
		allocateInfo2.pSetLayouts = &Pipeline::getDescriptorSetLayoutTAA();
		DSet = make_ref(VulkanContext::get()->device->allocateDescriptorSets(allocateInfo2).at(0));

		allocateInfo2.pSetLayouts = &Pipeline::getDescriptorSetLayoutTAASharpen();
		DSetSharpen = make_ref(VulkanContext::get()->device->allocateDescriptorSets(allocateInfo2).at(0));

		updateDescriptorSets(renderTargets);
	}

	void TAA::updateDescriptorSets(std::map<std::string, Image>& renderTargets)
	{
		std::deque<vk::DescriptorImageInfo> dsii{};
		const auto wSetImage = [&dsii](const vk::DescriptorSet& dstSet, uint32_t dstBinding, Image& image) {
			dsii.emplace_back(*image.sampler, *image.view, vk::ImageLayout::eShaderReadOnlyOptimal);
			return vk::WriteDescriptorSet{ dstSet, dstBinding, 0, 1, vk::DescriptorType::eCombinedImageSampler, &dsii.back(), nullptr, nullptr };
		};
		std::deque<vk::DescriptorBufferInfo> dsbi{};
		const auto wSetBuffer = [&dsbi](const vk::DescriptorSet& dstSet, uint32_t dstBinding, Buffer& buffer) {
			dsbi.emplace_back(*buffer.buffer, 0, buffer.size);
			return vk::WriteDescriptorSet{ dstSet, dstBinding, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &dsbi.back(), nullptr };
		};

		std::vector<vk::WriteDescriptorSet> writeDescriptorSets = {
			wSetImage(*DSet, 0, previous),
			wSetImage(*DSet, 1, frameImage),
			wSetImage(*DSet, 2, renderTargets["depth"]),
			wSetImage(*DSet, 3, renderTargets["velocity"]),
			wSetBuffer(*DSet, 4, uniform),
			wSetImage(*DSetSharpen, 0, renderTargets["taa"]),
			wSetBuffer(*DSetSharpen, 1, uniform)
		};

		VulkanContext::get()->device->updateDescriptorSets(writeDescriptorSets, nullptr);
	}

	void TAA::draw(vk::CommandBuffer cmd, uint32_t imageIndex, std::map<std::string, Image>& renderTargets)
	{
		vk::ClearValue clearColor;
		memcpy(clearColor.color.float32, GUI::clearColor.data(), 4 * sizeof(float));

		std::vector<vk::ClearValue> clearValues = { clearColor };

		// Main TAA pass
		vk::RenderPassBeginInfo rpi;
		rpi.renderPass = *renderPass.renderPass;
		rpi.framebuffer = *framebuffers[imageIndex].framebuffer;
		rpi.renderArea.offset = vk::Offset2D{ 0, 0 };
		rpi.renderArea.extent = *renderTargets["taa"].extent;
		rpi.clearValueCount = 1;
		rpi.pClearValues = clearValues.data();

		renderTargets["taa"].changeLayout(cmd, LayoutState::ColorWrite);
		cmd.beginRenderPass(rpi, vk::SubpassContents::eInline);
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline.pipeline);
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipeline.pipelineLayout, 0, *DSet, nullptr);
		cmd.draw(3, 1, 0, 0);
		cmd.endRenderPass();
		renderTargets["taa"].changeLayout(cmd, LayoutState::ColorRead);

		saveImage(cmd, renderTargets["taa"]);

		// TAA Sharpen pass
		vk::RenderPassBeginInfo rpi2;
		rpi2.renderPass = *renderPassSharpen.renderPass;
		rpi2.framebuffer = *framebuffersSharpen[imageIndex].framebuffer;
		rpi2.renderArea.offset = vk::Offset2D{ 0, 0 };
		rpi2.renderArea.extent = *renderTargets["viewport"].extent;
		rpi2.clearValueCount = 1;
		rpi2.pClearValues = clearValues.data();

		cmd.beginRenderPass(rpi2, vk::SubpassContents::eInline);
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipelineSharpen.pipeline);
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipelineSharpen.pipelineLayout, 0, *DSetSharpen, nullptr);
		cmd.draw(3, 1, 0, 0);
		cmd.endRenderPass();
	}

	void TAA::createRenderPasses(std::map<std::string, Image>& renderTargets)
	{
		renderPass.Create(*renderTargets["taa"].format, vk::Format::eUndefined);
		renderPassSharpen.Create(*renderTargets["viewport"].format, vk::Format::eUndefined);
	}

	void TAA::createFrameBuffers(std::map<std::string, Image>& renderTargets)
	{
		auto vulkan = VulkanContext::get();

		framebuffers.resize(vulkan->swapchain.images.size());
		for (size_t i = 0; i < vulkan->swapchain.images.size(); ++i)
		{
			uint32_t width = renderTargets["taa"].width;
			uint32_t height = renderTargets["taa"].height;
			vk::ImageView view = *renderTargets["taa"].view;
			framebuffers[i].Create(width, height, view, renderPass);
		}

		framebuffersSharpen.resize(vulkan->swapchain.images.size());
		for (size_t i = 0; i < vulkan->swapchain.images.size(); ++i)
		{
			uint32_t width = renderTargets["viewport"].width;
			uint32_t height = renderTargets["viewport"].height;
			vk::ImageView view = *renderTargets["viewport"].view;
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
		Shader vert{ "shaders/Common/quad.vert", ShaderType::Vertex, true };
		Shader frag{ "shaders/TAA/TAA.frag", ShaderType::Fragment, true };

		pipeline.info.pVertShader = &vert;
		pipeline.info.pFragShader = &frag;
		pipeline.info.width = renderTargets["taa"].width_f;
		pipeline.info.height = renderTargets["taa"].height_f;
		pipeline.info.cullMode = CullMode::Back;
		pipeline.info.colorBlendAttachments = make_ref(std::vector<vk::PipelineColorBlendAttachmentState>{ *renderTargets["taa"].blentAttachment });
		pipeline.info.descriptorSetLayouts = make_ref(std::vector<vk::DescriptorSetLayout>{ Pipeline::getDescriptorSetLayoutTAA() });
		pipeline.info.renderPass = renderPass;

		pipeline.createGraphicsPipeline();
	}

	void vm::TAA::createPipelineSharpen(std::map<std::string, Image>& renderTargets)
	{
		Shader vert{ "shaders/Common/quad.vert", ShaderType::Vertex, true };
		Shader frag{ "shaders/TAA/TAASharpen.frag", ShaderType::Fragment, true };

		pipelineSharpen.info.pVertShader = &vert;
		pipelineSharpen.info.pFragShader = &frag;
		pipelineSharpen.info.width = renderTargets["viewport"].width_f;
		pipelineSharpen.info.height = renderTargets["viewport"].height_f;
		pipelineSharpen.info.cullMode = CullMode::Back;
		pipelineSharpen.info.colorBlendAttachments = make_ref(std::vector<vk::PipelineColorBlendAttachmentState>{ *renderTargets["viewport"].blentAttachment });
		pipelineSharpen.info.descriptorSetLayouts = make_ref(std::vector<vk::DescriptorSetLayout>{ Pipeline::getDescriptorSetLayoutTAASharpen() });
		pipelineSharpen.info.renderPass = renderPassSharpen;

		pipelineSharpen.createGraphicsPipeline();
	}

	void TAA::copyFrameImage(const vk::CommandBuffer& cmd, Image& renderedImage) const
	{
		frameImage.copyColorAttachment(cmd, renderedImage);
	}

	void TAA::saveImage(const vk::CommandBuffer& cmd, Image& source) const
	{
		previous.transitionImageLayout(
			cmd,
			vk::ImageLayout::eShaderReadOnlyOptimal,
			vk::ImageLayout::eTransferDstOptimal,
			vk::PipelineStageFlagBits::eFragmentShader,
			vk::PipelineStageFlagBits::eTransfer,
			vk::AccessFlagBits::eShaderRead,
			vk::AccessFlagBits::eTransferWrite,
			vk::ImageAspectFlagBits::eColor);
		source.transitionImageLayout(
			cmd,
			source.layoutState == LayoutState::ColorRead ? vk::ImageLayout::eShaderReadOnlyOptimal : vk::ImageLayout::eColorAttachmentOptimal,
			vk::ImageLayout::eTransferSrcOptimal,
			source.layoutState == LayoutState::ColorRead ? vk::PipelineStageFlagBits::eFragmentShader : vk::PipelineStageFlagBits::eColorAttachmentOutput,
			vk::PipelineStageFlagBits::eTransfer,
			source.layoutState == LayoutState::ColorRead ? vk::AccessFlagBits::eShaderRead : vk::AccessFlagBits::eColorAttachmentWrite,
			vk::AccessFlagBits::eTransferRead,
			vk::ImageAspectFlagBits::eColor);

		// copy the image
		vk::ImageCopy region;
		region.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
		region.srcSubresource.layerCount = 1;
		region.dstSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
		region.dstSubresource.layerCount = 1;
		region.extent.width = source.width;
		region.extent.height = source.height;
		region.extent.depth = 1;

		cmd.copyImage(
			*source.image,
			vk::ImageLayout::eTransferSrcOptimal,
			*previous.image,
			vk::ImageLayout::eTransferDstOptimal,
			region
		);

		previous.transitionImageLayout(
			cmd,
			vk::ImageLayout::eTransferDstOptimal,
			vk::ImageLayout::eShaderReadOnlyOptimal,
			vk::PipelineStageFlagBits::eTransfer,
			vk::PipelineStageFlagBits::eFragmentShader,
			vk::AccessFlagBits::eTransferWrite,
			vk::AccessFlagBits::eShaderRead,
			vk::ImageAspectFlagBits::eColor);
		source.transitionImageLayout(
			cmd,
			vk::ImageLayout::eTransferSrcOptimal,
			vk::ImageLayout::eShaderReadOnlyOptimal,
			vk::PipelineStageFlagBits::eTransfer,
			vk::PipelineStageFlagBits::eFragmentShader,
			vk::AccessFlagBits::eTransferRead,
			vk::AccessFlagBits::eShaderRead,
			vk::ImageAspectFlagBits::eColor);
		source.layoutState = LayoutState::ColorRead;
	}

	void TAA::destroy()
	{
		uniform.destroy();
		previous.destroy();
		frameImage.destroy();

		for (auto& framebuffer : framebuffers)
			framebuffer.Destroy();
		for (auto& framebuffer : framebuffersSharpen)
			framebuffer.Destroy();

		renderPass.Destroy();
		renderPassSharpen.Destroy();

		if (Pipeline::getDescriptorSetLayoutTAA()) {
			VulkanContext::get()->device->destroyDescriptorSetLayout(Pipeline::getDescriptorSetLayoutTAA());
			Pipeline::getDescriptorSetLayoutTAA() = nullptr;
		}
		if (Pipeline::getDescriptorSetLayoutTAASharpen()) {
			VulkanContext::get()->device->destroyDescriptorSetLayout(Pipeline::getDescriptorSetLayoutTAASharpen());
			Pipeline::getDescriptorSetLayoutTAASharpen() = nullptr;
		}
		pipeline.destroy();
		pipelineSharpen.destroy();
	}
}
