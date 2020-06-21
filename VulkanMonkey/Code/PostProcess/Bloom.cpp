#include "Bloom.h"
#include "../GUI/GUI.h"
#include "../Swapchain/Swapchain.h"
#include "../Core/Surface.h"
#include "../Shader/Shader.h"
#include "../VulkanContext/VulkanContext.h"
#include <vulkan/vulkan.hpp>
#include <deque>

namespace vm
{
	Bloom::Bloom()
	{
		DSBrightFilter = vk::DescriptorSet();
		DSGaussianBlurHorizontal = vk::DescriptorSet();
		DSGaussianBlurVertical = vk::DescriptorSet();
		DSCombine = vk::DescriptorSet();
	}

	Bloom::~Bloom()
	{
	}

	void Bloom::Init()
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

	void Bloom::copyFrameImage(const vk::CommandBuffer& cmd, Image& renderedImage) const
	{
		frameImage.copyColorAttachment(cmd, renderedImage);
	}

	void vm::Bloom::createRenderPasses(std::map<std::string, Image>& renderTargets)
	{
		renderPassBrightFilter.Create(renderTargets["brightFilter"].format.Value(), vk::Format::eUndefined);
		renderPassGaussianBlur.Create(renderTargets["gaussianBlurHorizontal"].format.Value(), vk::Format::eUndefined);
		renderPassCombine.Create(renderTargets["viewport"].format.Value(), vk::Format::eUndefined);
	}

	void vm::Bloom::createFrameBuffers(std::map<std::string, Image>& renderTargets)
	{
		auto vulkan = VulkanContext::get();
		framebuffers.resize(vulkan->swapchain.images.size() * 4);
		for (size_t i = 0; i < vulkan->swapchain.images.size(); ++i)
		{
			uint32_t width = renderTargets["brightFilter"].width.Value();
			uint32_t height = renderTargets["brightFilter"].height.Value();
			vk::ImageView view = renderTargets["brightFilter"].view.Value();
			framebuffers[i].Create(width, height, view, renderPassBrightFilter);
		}

		for (size_t i = vulkan->swapchain.images.size(); i < vulkan->swapchain.images.size() * 2; ++i)
		{
			uint32_t width = renderTargets["gaussianBlurHorizontal"].width.Value();
			uint32_t height = renderTargets["gaussianBlurHorizontal"].height.Value();
			vk::ImageView view = renderTargets["gaussianBlurHorizontal"].view.Value();
			framebuffers[i].Create(width, height, view, renderPassGaussianBlur);
		}

		for (size_t i = vulkan->swapchain.images.size() * 2; i < vulkan->swapchain.images.size() * 3; ++i)
		{
			uint32_t width = renderTargets["gaussianBlurVertical"].width.Value();
			uint32_t height = renderTargets["gaussianBlurVertical"].height.Value();
			vk::ImageView view = renderTargets["gaussianBlurVertical"].view.Value();
			framebuffers[i].Create(width, height, view, renderPassGaussianBlur);
		}

		for (size_t i = vulkan->swapchain.images.size() * 3; i < vulkan->swapchain.images.size() * 4; ++i)
		{
			uint32_t width = renderTargets["viewport"].width.Value();
			uint32_t height = renderTargets["viewport"].height.Value();
			vk::ImageView view = renderTargets["viewport"].view.Value();
			framebuffers[i].Create(width, height, view, renderPassCombine);
		}
	}

	void Bloom::createUniforms(std::map<std::string, Image>& renderTargets)
	{
		auto vulkan = VulkanContext::get();
		vk::DescriptorSetAllocateInfo allocateInfo;
		allocateInfo.descriptorPool = vulkan->descriptorPool.Value();
		allocateInfo.descriptorSetCount = 1;

		// Composition image to Bright Filter shader
		allocateInfo.pSetLayouts = &Pipeline::getDescriptorSetLayoutBrightFilter();
		DSBrightFilter = vulkan->device->allocateDescriptorSets(allocateInfo).at(0);

		// Bright Filter image to Gaussian Blur Horizontal shader
		allocateInfo.pSetLayouts = &Pipeline::getDescriptorSetLayoutGaussianBlurH();
		DSGaussianBlurHorizontal = vulkan->device->allocateDescriptorSets(allocateInfo).at(0);

		// Gaussian Blur Horizontal image to Gaussian Blur Vertical shader
		allocateInfo.pSetLayouts = &Pipeline::getDescriptorSetLayoutGaussianBlurV();
		DSGaussianBlurVertical = vulkan->device->allocateDescriptorSets(allocateInfo).at(0);

		// Gaussian Blur Vertical image to Combine shader
		allocateInfo.pSetLayouts = &Pipeline::getDescriptorSetLayoutCombine();
		DSCombine = vulkan->device->allocateDescriptorSets(allocateInfo).at(0);

		updateDescriptorSets(renderTargets);
	}

	void Bloom::updateDescriptorSets(std::map<std::string, Image>& renderTargets)
	{
		std::deque<vk::DescriptorImageInfo> dsii{};
		auto const wSetImage = [&dsii](const vk::DescriptorSet& dstSet, uint32_t dstBinding, Image& image) {
			dsii.emplace_back(image.sampler.Value(), image.view.Value(), vk::ImageLayout::eShaderReadOnlyOptimal);
			return vk::WriteDescriptorSet{ dstSet, dstBinding, 0, 1, vk::DescriptorType::eCombinedImageSampler, &dsii.back(), nullptr, nullptr };
		};

		std::vector<vk::WriteDescriptorSet> textureWriteSets{
			wSetImage(DSBrightFilter.Value(), 0, frameImage),
			wSetImage(DSGaussianBlurHorizontal.Value(), 0, renderTargets["brightFilter"]),
			wSetImage(DSGaussianBlurVertical.Value(), 0, renderTargets["gaussianBlurHorizontal"]),
			wSetImage(DSCombine.Value(), 0, frameImage),
			wSetImage(DSCombine.Value(), 1, renderTargets["gaussianBlurVertical"])
		};
		VulkanContext::get()->device->updateDescriptorSets(textureWriteSets, nullptr);
	}

	void Bloom::draw(vk::CommandBuffer cmd, uint32_t imageIndex, std::map<std::string, Image>& renderTargets)
	{
		uint32_t totalImages = static_cast<uint32_t>(VulkanContext::get()->swapchain.images.size());

		vk::ClearValue clearColor;
		memcpy(clearColor.color.float32, GUI::clearColor.data(), 4 * sizeof(float));

		std::vector<vk::ClearValue> clearValues = { clearColor };

		std::vector<float> values{ GUI::Bloom_Inv_brightness, GUI::Bloom_intensity, GUI::Bloom_range, GUI::Bloom_exposure, static_cast<float>(GUI::use_tonemap) };

		vk::RenderPassBeginInfo rpi;
		rpi.renderPass = *renderPassBrightFilter;
		rpi.framebuffer = *framebuffers[imageIndex];
		rpi.renderArea.offset = vk::Offset2D{ 0, 0 };
		rpi.renderArea.extent = renderTargets["brightFilter"].extent.Value();
		rpi.clearValueCount = 1;
		rpi.pClearValues = clearValues.data();

		renderTargets["brightFilter"].changeLayout(cmd, LayoutState::ColorWrite);
		cmd.beginRenderPass(rpi, vk::SubpassContents::eInline);
		cmd.pushConstants<float>(pipelineBrightFilter.pipelineLayout.Value(), vk::ShaderStageFlagBits::eFragment, 0, values);
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelineBrightFilter.pipeline.Value());
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineBrightFilter.pipelineLayout.Value(), 0, DSBrightFilter.Value(), nullptr);
		cmd.draw(3, 1, 0, 0);
		cmd.endRenderPass();
		renderTargets["brightFilter"].changeLayout(cmd, LayoutState::ColorRead);

		rpi.renderPass = *renderPassGaussianBlur;
		rpi.framebuffer = *framebuffers[static_cast<size_t>(totalImages) + static_cast<size_t>(imageIndex)];

		renderTargets["gaussianBlurHorizontal"].changeLayout(cmd, LayoutState::ColorWrite);
		cmd.beginRenderPass(rpi, vk::SubpassContents::eInline);
		cmd.pushConstants<float>(pipelineGaussianBlurHorizontal.pipelineLayout.Value(), vk::ShaderStageFlagBits::eFragment, 0, values);
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelineGaussianBlurHorizontal.pipeline.Value());
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineBrightFilter.pipelineLayout.Value(), 0, DSGaussianBlurHorizontal.Value(), nullptr);
		cmd.draw(3, 1, 0, 0);
		cmd.endRenderPass();
		renderTargets["gaussianBlurHorizontal"].changeLayout(cmd, LayoutState::ColorRead);

		rpi.framebuffer = *framebuffers[static_cast<size_t>(totalImages) * 2 + static_cast<size_t>(imageIndex)];

		renderTargets["gaussianBlurVertical"].changeLayout(cmd, LayoutState::ColorWrite);
		cmd.beginRenderPass(rpi, vk::SubpassContents::eInline);
		cmd.pushConstants<float>(pipelineGaussianBlurVertical.pipelineLayout.Value(), vk::ShaderStageFlagBits::eFragment, 0, values);
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelineGaussianBlurVertical.pipeline.Value());
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineGaussianBlurVertical.pipelineLayout.Value(), 0, DSGaussianBlurVertical.Value(), nullptr);
		cmd.draw(3, 1, 0, 0);
		cmd.endRenderPass();
		renderTargets["gaussianBlurVertical"].changeLayout(cmd, LayoutState::ColorRead);

		rpi.renderPass = *renderPassCombine;
		rpi.framebuffer = *framebuffers[static_cast<size_t>(totalImages) * 3 + static_cast<size_t>(imageIndex)];

		cmd.beginRenderPass(rpi, vk::SubpassContents::eInline);
		cmd.pushConstants<float>(pipelineCombine.pipelineLayout.Value(), vk::ShaderStageFlagBits::eFragment, 0, values);
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelineCombine.pipeline.Value());
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineCombine.pipelineLayout.Value(), 0, DSCombine.Value(), nullptr);
		cmd.draw(3, 1, 0, 0);
		cmd.endRenderPass();
	}

	void vm::Bloom::createPipelines(std::map<std::string, Image>& renderTargets)
	{
		createBrightFilterPipeline(renderTargets);
		createGaussianBlurHorizontaPipeline(renderTargets);
		createGaussianBlurVerticalPipeline(renderTargets);
		createCombinePipeline(renderTargets);
	}

	void Bloom::createBrightFilterPipeline(std::map<std::string, Image>& renderTargets)
	{
		Shader vert{ "shaders/Common/quad.vert", ShaderType::Vertex, true };
		Shader frag{ "shaders/Bloom/brightFilter.frag", ShaderType::Fragment, true };

		pipelineBrightFilter.info.pVertShader = &vert;
		pipelineBrightFilter.info.pFragShader = &frag;
		pipelineBrightFilter.info.width = renderTargets["brightFilter"].width_f.Value();
		pipelineBrightFilter.info.height = renderTargets["brightFilter"].height_f.Value();
		pipelineBrightFilter.info.cullMode = CullMode::Back;
		pipelineBrightFilter.info.colorBlendAttachments = { renderTargets["brightFilter"].blentAttachment.Value() };
		pipelineBrightFilter.info.pushConstantStage = PushConstantStage::Fragment;
		pipelineBrightFilter.info.pushConstantSize = 5 * sizeof(vec4);
		pipelineBrightFilter.info.descriptorSetLayouts = { Pipeline::getDescriptorSetLayoutBrightFilter() };
		pipelineBrightFilter.info.renderPass = renderPassBrightFilter;

		pipelineBrightFilter.createGraphicsPipeline();
	}

	void Bloom::createGaussianBlurHorizontaPipeline(std::map<std::string, Image>& renderTargets)
	{
		Shader vert{ "shaders/Common/quad.vert", ShaderType::Vertex, true };
		Shader frag{ "shaders/Bloom/gaussianBlurHorizontal.frag", ShaderType::Fragment, true };

		pipelineGaussianBlurHorizontal.info.pVertShader = &vert;
		pipelineGaussianBlurHorizontal.info.pFragShader = &frag;
		pipelineGaussianBlurHorizontal.info.width = renderTargets["gaussianBlurHorizontal"].width_f.Value();
		pipelineGaussianBlurHorizontal.info.height = renderTargets["gaussianBlurHorizontal"].height_f.Value();
		pipelineGaussianBlurHorizontal.info.cullMode = CullMode::Back;
		pipelineGaussianBlurHorizontal.info.colorBlendAttachments = { renderTargets["gaussianBlurHorizontal"].blentAttachment.Value() };
		pipelineGaussianBlurHorizontal.info.pushConstantStage = PushConstantStage::Fragment;
		pipelineGaussianBlurHorizontal.info.pushConstantSize = 5 * sizeof(vec4);
		pipelineGaussianBlurHorizontal.info.descriptorSetLayouts = { Pipeline::getDescriptorSetLayoutGaussianBlurH() };
		pipelineGaussianBlurHorizontal.info.renderPass = renderPassGaussianBlur;

		pipelineGaussianBlurHorizontal.createGraphicsPipeline();
	}

	void Bloom::createGaussianBlurVerticalPipeline(std::map<std::string, Image>& renderTargets)
	{
		Shader vert{ "shaders/Common/quad.vert", ShaderType::Vertex, true };
		Shader frag{ "shaders/Bloom/gaussianBlurVertical.frag", ShaderType::Fragment, true };

		pipelineGaussianBlurVertical.info.pVertShader = &vert;
		pipelineGaussianBlurVertical.info.pFragShader = &frag;
		pipelineGaussianBlurVertical.info.width = renderTargets["gaussianBlurVertical"].width_f.Value();
		pipelineGaussianBlurVertical.info.height = renderTargets["gaussianBlurVertical"].height_f.Value();
		pipelineGaussianBlurVertical.info.cullMode = CullMode::Back;
		pipelineGaussianBlurVertical.info.colorBlendAttachments = { renderTargets["gaussianBlurVertical"].blentAttachment.Value() };
		pipelineGaussianBlurVertical.info.pushConstantStage = PushConstantStage::Fragment;
		pipelineGaussianBlurVertical.info.pushConstantSize = 5 * sizeof(vec4);
		pipelineGaussianBlurVertical.info.descriptorSetLayouts = { Pipeline::getDescriptorSetLayoutGaussianBlurV() };
		pipelineGaussianBlurVertical.info.renderPass = renderPassGaussianBlur;

		pipelineGaussianBlurVertical.createGraphicsPipeline();
	}

	void Bloom::createCombinePipeline(std::map<std::string, Image>& renderTargets)
	{
		// Shader stages
		Shader vert{ "shaders/Common/quad.vert", ShaderType::Vertex, true };
		Shader frag{ "shaders/Bloom/combine.frag", ShaderType::Fragment, true };

		pipelineCombine.info.pVertShader = &vert;
		pipelineCombine.info.pFragShader = &frag;
		pipelineCombine.info.width = renderTargets["viewport"].width_f.Value();
		pipelineCombine.info.height = renderTargets["viewport"].height_f.Value();
		pipelineCombine.info.cullMode = CullMode::Back;
		pipelineCombine.info.colorBlendAttachments = { renderTargets["viewport"].blentAttachment.Value() };
		pipelineCombine.info.pushConstantStage = PushConstantStage::Fragment;
		pipelineCombine.info.pushConstantSize = 5 * sizeof(vec4);
		pipelineCombine.info.descriptorSetLayouts = { Pipeline::getDescriptorSetLayoutCombine() };
		pipelineCombine.info.renderPass = renderPassCombine;

		pipelineCombine.createGraphicsPipeline();
	}

	void Bloom::destroy()
	{
		auto vulkan = VulkanContext::get();
		for (auto& frameBuffer : framebuffers)
			frameBuffer.Destroy();

		renderPassBrightFilter.Destroy();
		renderPassGaussianBlur.Destroy();
		renderPassCombine.Destroy();

		if (Pipeline::getDescriptorSetLayoutBrightFilter()) {
			vulkan->device->destroyDescriptorSetLayout(Pipeline::getDescriptorSetLayoutBrightFilter());
			Pipeline::getDescriptorSetLayoutBrightFilter() = nullptr;
		}
		if (Pipeline::getDescriptorSetLayoutGaussianBlurH()) {
			vulkan->device->destroyDescriptorSetLayout(Pipeline::getDescriptorSetLayoutGaussianBlurH());
			Pipeline::getDescriptorSetLayoutGaussianBlurH() = nullptr;
		}
		if (Pipeline::getDescriptorSetLayoutGaussianBlurV()) {
			vulkan->device->destroyDescriptorSetLayout(Pipeline::getDescriptorSetLayoutGaussianBlurV());
			Pipeline::getDescriptorSetLayoutGaussianBlurV() = nullptr;
		}
		if (Pipeline::getDescriptorSetLayoutCombine()) {
			vulkan->device->destroyDescriptorSetLayout(Pipeline::getDescriptorSetLayoutCombine());
			Pipeline::getDescriptorSetLayoutCombine() = nullptr;
		}
		frameImage.destroy();
		pipelineBrightFilter.destroy();
		pipelineGaussianBlurHorizontal.destroy();
		pipelineGaussianBlurVertical.destroy();
		pipelineCombine.destroy();
	}
}
