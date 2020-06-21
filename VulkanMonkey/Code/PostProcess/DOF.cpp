#include "vulkanPCH.h"
#include "DOF.h"
#include "../GUI/GUI.h"
#include "../Swapchain/Swapchain.h"
#include "../Core/Surface.h"
#include "../Shader/Shader.h"
#include "../VulkanContext/VulkanContext.h"
#include <deque>

namespace vm
{
	DOF::DOF()
	{
		DSet = vk::DescriptorSet();
	}

	DOF::~DOF()
	{
	}

	void DOF::Init()
	{
		frameImage.format = VulkanContext::get()->surface.formatKHR->format;
		frameImage.initialLayout = vk::ImageLayout::eUndefined;
		frameImage.createImage(
			static_cast<uint32_t>(WIDTH_f * GUI::renderTargetsScale),
			static_cast<uint32_t>(HEIGHT_f * GUI::renderTargetsScale),
			vk::ImageTiling::eOptimal,
			vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled, vk::MemoryPropertyFlagBits::eDeviceLocal);
		frameImage.transitionImageLayout(vk::ImageLayout::eUndefined, vk::ImageLayout::eShaderReadOnlyOptimal);
		frameImage.createImageView(vk::ImageAspectFlagBits::eColor);
		frameImage.createSampler();
	}

	void DOF::copyFrameImage(const vk::CommandBuffer& cmd, Image& renderedImage) const
	{
		frameImage.copyColorAttachment(cmd, renderedImage);
	}

	void DOF::createRenderPass(std::map<std::string, Image>& renderTargets)
	{
		renderPass.Create(renderTargets["viewport"].format.Value(), vk::Format::eUndefined);
	}

	void DOF::createFrameBuffers(std::map<std::string, Image>& renderTargets)
	{
		auto vulkan = VulkanContext::get();
		framebuffers.resize(vulkan->swapchain.images.size());
		for (size_t i = 0; i < vulkan->swapchain.images.size(); ++i)
		{
			uint32_t width = renderTargets["viewport"].width.Value();
			uint32_t height = renderTargets["viewport"].height.Value();
			vk::ImageView view = renderTargets["viewport"].view.Value();
			framebuffers[i].Create(width, height, view, renderPass);
		}
	}

	void DOF::createUniforms(std::map<std::string, Image>& renderTargets)
	{
		auto vulkan = VulkanContext::get();
		vk::DescriptorSetAllocateInfo allocateInfo;
		allocateInfo.descriptorPool = vulkan->descriptorPool.Value();
		allocateInfo.descriptorSetCount = 1;

		allocateInfo.pSetLayouts = &Pipeline::getDescriptorSetLayoutDOF();
		DSet = vulkan->device->allocateDescriptorSets(allocateInfo).at(0);

		updateDescriptorSets(renderTargets);
	}

	void DOF::updateDescriptorSets(std::map<std::string, Image>& renderTargets)
	{
		std::deque<vk::DescriptorImageInfo> dsii{};
		auto const wSetImage = [&dsii](const vk::DescriptorSet& dstSet, uint32_t dstBinding, Image& image) {
			dsii.emplace_back(image.sampler.Value(), image.view.Value(), vk::ImageLayout::eShaderReadOnlyOptimal);
			return vk::WriteDescriptorSet{ dstSet, dstBinding, 0, 1, vk::DescriptorType::eCombinedImageSampler, &dsii.back(), nullptr, nullptr };
		};

		std::vector<vk::WriteDescriptorSet> textureWriteSets{
			wSetImage(DSet.Value(), 0, frameImage),
			wSetImage(DSet.Value(), 1, renderTargets["depth"])
		};
		VulkanContext::get()->device->updateDescriptorSets(textureWriteSets, nullptr);
	}

	void DOF::draw(vk::CommandBuffer cmd, uint32_t imageIndex, std::map<std::string, Image>& renderTargets)
	{
		vk::ClearValue clearColor;
		memcpy(clearColor.color.float32, GUI::clearColor.data(), 4 * sizeof(float));

		std::vector<vk::ClearValue> clearValues = { clearColor };

		std::vector<float> values{ GUI::DOF_focus_scale, GUI::DOF_blur_range, 0.0f, 0.0f };

		vk::RenderPassBeginInfo rpi;
		rpi.renderPass = *renderPass;
		rpi.framebuffer = *framebuffers[imageIndex];
		rpi.renderArea.offset = vk::Offset2D{ 0, 0 };
		rpi.renderArea.extent = renderTargets["viewport"].extent.Value();
		rpi.clearValueCount = 1;
		rpi.pClearValues = clearValues.data();

		cmd.beginRenderPass(rpi, vk::SubpassContents::eInline);
		cmd.pushConstants<float>(pipeline.pipelineLayout.Value(), vk::ShaderStageFlagBits::eFragment, 0, values);
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline.pipeline.Value());
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline.pipelineLayout.Value(), 0, DSet.Value(), nullptr);
		cmd.draw(3, 1, 0, 0);
		cmd.endRenderPass();
	}

	void DOF::createPipeline(std::map<std::string, Image>& renderTargets)
	{
		Shader vert{ "shaders/Common/quad.vert", ShaderType::Vertex, true };
		Shader frag{ "shaders/DepthOfField/DOF.frag", ShaderType::Fragment, true };

		pipeline.info.pVertShader = &vert;
		pipeline.info.pFragShader = &frag;
		pipeline.info.width = renderTargets["viewport"].width_f.Value();
		pipeline.info.height = renderTargets["viewport"].height_f.Value();
		pipeline.info.cullMode = CullMode::Back;
		pipeline.info.colorBlendAttachments = { renderTargets["viewport"].blentAttachment.Value() };
		pipeline.info.pushConstantStage = PushConstantStage::Fragment;
		pipeline.info.pushConstantSize = 5 * sizeof(vec4);
		pipeline.info.descriptorSetLayouts = { Pipeline::getDescriptorSetLayoutDOF() };
		pipeline.info.renderPass = renderPass;

		pipeline.createGraphicsPipeline();

	}

	void DOF::destroy()
	{
		auto vulkan = VulkanContext::get();
		for (auto& framebuffer : framebuffers)
			framebuffer.Destroy();

		renderPass.Destroy();

		if (Pipeline::getDescriptorSetLayoutDOF()) {
			vulkan->device->destroyDescriptorSetLayout(Pipeline::getDescriptorSetLayoutDOF());
			Pipeline::getDescriptorSetLayoutDOF() = nullptr;
		}
		frameImage.destroy();
		pipeline.destroy();
	}
}
