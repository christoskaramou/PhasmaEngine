#include "vulkanPCH.h"
#include "MotionBlur.h"
#include <deque>
#include "../Core/Surface.h"
#include "../Swapchain/Swapchain.h"
#include "../GUI/GUI.h"
#include "../Shader/Shader.h"
#include "../Core/Queue.h"
#include "../Core/Timer.h"
#include "../VulkanContext/VulkanContext.h"

namespace vm
{
	MotionBlur::MotionBlur()
	{
		DSet = vk::DescriptorSet();
	}

	MotionBlur::~MotionBlur()
	{
	}

	void MotionBlur::Init()
	{
		frameImage.format = VulkanContext::get()->surface.formatKHR->format;
		frameImage.initialLayout = vk::ImageLayout::eUndefined;
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

	void MotionBlur::createMotionBlurUniforms(std::map<std::string, Image>& renderTargets)
	{
		auto size = 4 * sizeof(mat4);
		UBmotionBlur.createBuffer(size, vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostVisible);
		UBmotionBlur.map();
		UBmotionBlur.zero();
		UBmotionBlur.flush();
		UBmotionBlur.unmap();

		vk::DescriptorSetAllocateInfo allocateInfo;
		allocateInfo.descriptorPool = VulkanContext::get()->descriptorPool.Value();
		allocateInfo.descriptorSetCount = 1;
		allocateInfo.pSetLayouts = &Pipeline::getDescriptorSetLayoutMotionBlur();
		DSet = VulkanContext::get()->device->allocateDescriptorSets(allocateInfo).at(0);

		updateDescriptorSets(renderTargets);
	}

	void MotionBlur::updateDescriptorSets(std::map<std::string, Image>& renderTargets)
	{
		std::deque<vk::DescriptorImageInfo> dsii{};
		auto const wSetImage = [&dsii](const vk::DescriptorSet& dstSet, uint32_t dstBinding, Image& image) {
			dsii.emplace_back(image.sampler.Value(), image.view.Value(), vk::ImageLayout::eShaderReadOnlyOptimal);
			return vk::WriteDescriptorSet{ dstSet, dstBinding, 0, 1, vk::DescriptorType::eCombinedImageSampler, &dsii.back(), nullptr, nullptr };
		};
		std::deque<vk::DescriptorBufferInfo> dsbi{};
		auto const wSetBuffer = [&dsbi](const vk::DescriptorSet& dstSet, uint32_t dstBinding, Buffer& buffer) {
			dsbi.emplace_back(buffer.buffer.Value(), 0, buffer.size.Value());
			return vk::WriteDescriptorSet{ dstSet, dstBinding, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &dsbi.back(), nullptr };
		};

		std::vector<vk::WriteDescriptorSet> textureWriteSets{
			wSetImage(DSet.Value(), 0, frameImage),
			wSetImage(DSet.Value(), 1, renderTargets["depth"]),
			wSetImage(DSet.Value(), 2, renderTargets["velocity"]),
			wSetBuffer(DSet.Value(), 3, UBmotionBlur)
		};
		VulkanContext::get()->device->updateDescriptorSets(textureWriteSets, nullptr);
	}

	void MotionBlur::draw(vk::CommandBuffer cmd, uint32_t imageIndex, const vk::Extent2D& extent)
	{
		vk::ClearValue clearColor;
		memcpy(clearColor.color.float32, GUI::clearColor.data(), 4 * sizeof(float));

		std::vector<vk::ClearValue> clearValues = { clearColor };

		vk::RenderPassBeginInfo rpi;
		rpi.renderPass = *renderPass;
		rpi.framebuffer = *framebuffers[imageIndex];
		rpi.renderArea.offset = vk::Offset2D{ 0, 0 };
		rpi.renderArea.extent = extent;
		rpi.clearValueCount = static_cast<uint32_t>(clearValues.size());
		rpi.pClearValues = clearValues.data();
		cmd.beginRenderPass(rpi, vk::SubpassContents::eInline);

		const vec4 values{ 1.f / static_cast<float>(FrameTimer::Instance().delta), sin(static_cast<float>(FrameTimer::Instance().time) * 0.125f), GUI::motionBlur_strength, 0.f };
		cmd.pushConstants<vec4>(pipeline.pipelineLayout.Value(), vk::ShaderStageFlagBits::eFragment, 0, values);
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline.pipeline.Value());
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline.pipelineLayout.Value(), 0, DSet.Value(), nullptr);
		cmd.draw(3, 1, 0, 0);
		cmd.endRenderPass();
	}

	void MotionBlur::destroy()
	{
		for (auto& frameBuffer : framebuffers)
			frameBuffer.Destroy();

		renderPass.Destroy();

		if (Pipeline::getDescriptorSetLayoutMotionBlur()) {
			VulkanContext::get()->device->destroyDescriptorSetLayout(Pipeline::getDescriptorSetLayoutMotionBlur());
			Pipeline::getDescriptorSetLayoutMotionBlur() = nullptr;
		}
		frameImage.destroy();
		UBmotionBlur.destroy();
		pipeline.destroy();
	}

	void MotionBlur::update(Camera& camera)
	{
		if (GUI::show_motionBlur) {
			static mat4 previousView = camera.view;

			motionBlurInput[0] = camera.projection;
			motionBlurInput[1] = camera.view;
			motionBlurInput[2] = previousView;
			motionBlurInput[3] = camera.invViewProjection;

			previousView = camera.view;

			Queue::memcpyRequest(&UBmotionBlur, { { &motionBlurInput, sizeof(motionBlurInput), 0 } });
			//UBmotionBlur.map();
			//memcpy(UBmotionBlur.data, &motionBlurInput, sizeof(motionBlurInput));
			//UBmotionBlur.flush();
			//UBmotionBlur.unmap();
		}
	}

	void MotionBlur::createRenderPass(std::map<std::string, Image>& renderTargets)
	{
		renderPass.Create(renderTargets["viewport"].format.Value(), vk::Format::eUndefined);
	}

	void MotionBlur::createFrameBuffers(std::map<std::string, Image>& renderTargets)
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

	void MotionBlur::createPipeline(std::map<std::string, Image>& renderTargets)
	{
		// Shader stages
		Shader vert{ "shaders/Common/quad.vert", ShaderType::Vertex, true };
		Shader frag{ "shaders/MotionBlur/motionBlur.frag", ShaderType::Fragment, true };

		pipeline.info.pVertShader = &vert;
		pipeline.info.pFragShader = &frag;
		pipeline.info.width = renderTargets["viewport"].width_f.Value();
		pipeline.info.height = renderTargets["viewport"].height_f.Value();
		pipeline.info.cullMode = CullMode::Back;
		pipeline.info.colorBlendAttachments = { renderTargets["viewport"].blentAttachment.Value() };
		pipeline.info.pushConstantStage = PushConstantStage::Fragment;
		pipeline.info.pushConstantSize = sizeof(vec4);
		pipeline.info.descriptorSetLayouts = { Pipeline::getDescriptorSetLayoutMotionBlur() };
		pipeline.info.renderPass = renderPass;

		pipeline.createGraphicsPipeline();
	}

	void MotionBlur::copyFrameImage(const vk::CommandBuffer& cmd, Image& renderedImage) const
	{
		frameImage.copyColorAttachment(cmd, renderedImage);
	}
}
