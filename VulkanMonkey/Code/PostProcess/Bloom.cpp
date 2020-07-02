#include "vulkanPCH.h"
#include "Bloom.h"
#include "../GUI/GUI.h"
#include "../Swapchain/Swapchain.h"
#include "../Core/Surface.h"
#include "../Shader/Shader.h"
#include "../VulkanContext/VulkanContext.h"
#include <deque>

namespace vm
{
	Bloom::Bloom()
	{
		DSBrightFilter = make_ref(vk::DescriptorSet());
		DSGaussianBlurHorizontal = make_ref(vk::DescriptorSet());
		DSGaussianBlurVertical = make_ref(vk::DescriptorSet());
		DSCombine = make_ref(vk::DescriptorSet());
	}

	Bloom::~Bloom()
	{
	}

	void Bloom::Init()
	{
		frameImage.format = make_ref(VulkanContext::get()->surface.formatKHR->format);
		frameImage.initialLayout = make_ref(vk::ImageLayout::eUndefined);
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
		renderPassBrightFilter.Create(*renderTargets["brightFilter"].format, vk::Format::eUndefined);
		renderPassGaussianBlur.Create(*renderTargets["gaussianBlurHorizontal"].format, vk::Format::eUndefined);
		renderPassCombine.Create(*renderTargets["viewport"].format, vk::Format::eUndefined);
	}

	void vm::Bloom::createFrameBuffers(std::map<std::string, Image>& renderTargets)
	{
		auto vulkan = VulkanContext::get();
		framebuffers.resize(vulkan->swapchain.images.size() * 4);
		for (size_t i = 0; i < vulkan->swapchain.images.size(); ++i)
		{
			uint32_t width = renderTargets["brightFilter"].width;
			uint32_t height = renderTargets["brightFilter"].height;
			vk::ImageView view = *renderTargets["brightFilter"].view;
			framebuffers[i].Create(width, height, view, renderPassBrightFilter);
		}

		for (size_t i = vulkan->swapchain.images.size(); i < vulkan->swapchain.images.size() * 2; ++i)
		{
			uint32_t width = renderTargets["gaussianBlurHorizontal"].width;
			uint32_t height = renderTargets["gaussianBlurHorizontal"].height;
			vk::ImageView view = *renderTargets["gaussianBlurHorizontal"].view;
			framebuffers[i].Create(width, height, view, renderPassGaussianBlur);
		}

		for (size_t i = vulkan->swapchain.images.size() * 2; i < vulkan->swapchain.images.size() * 3; ++i)
		{
			uint32_t width = renderTargets["gaussianBlurVertical"].width;
			uint32_t height = renderTargets["gaussianBlurVertical"].height;
			vk::ImageView view = *renderTargets["gaussianBlurVertical"].view;
			framebuffers[i].Create(width, height, view, renderPassGaussianBlur);
		}

		for (size_t i = vulkan->swapchain.images.size() * 3; i < vulkan->swapchain.images.size() * 4; ++i)
		{
			uint32_t width = renderTargets["viewport"].width;
			uint32_t height = renderTargets["viewport"].height;
			vk::ImageView view = *renderTargets["viewport"].view;
			framebuffers[i].Create(width, height, view, renderPassCombine);
		}
	}

	void Bloom::createUniforms(std::map<std::string, Image>& renderTargets)
	{
		auto vulkan = VulkanContext::get();
		vk::DescriptorSetAllocateInfo allocateInfo;
		allocateInfo.descriptorPool = *vulkan->descriptorPool;
		allocateInfo.descriptorSetCount = 1;

		// Composition image to Bright Filter shader
		allocateInfo.pSetLayouts = &Pipeline::getDescriptorSetLayoutBrightFilter();
		DSBrightFilter = make_ref(vulkan->device->allocateDescriptorSets(allocateInfo).at(0));

		// Bright Filter image to Gaussian Blur Horizontal shader
		allocateInfo.pSetLayouts = &Pipeline::getDescriptorSetLayoutGaussianBlurH();
		DSGaussianBlurHorizontal = make_ref(vulkan->device->allocateDescriptorSets(allocateInfo).at(0));

		// Gaussian Blur Horizontal image to Gaussian Blur Vertical shader
		allocateInfo.pSetLayouts = &Pipeline::getDescriptorSetLayoutGaussianBlurV();
		DSGaussianBlurVertical = make_ref(vulkan->device->allocateDescriptorSets(allocateInfo).at(0));

		// Gaussian Blur Vertical image to Combine shader
		allocateInfo.pSetLayouts = &Pipeline::getDescriptorSetLayoutCombine();
		DSCombine = make_ref(vulkan->device->allocateDescriptorSets(allocateInfo).at(0));

		updateDescriptorSets(renderTargets);
	}

	void Bloom::updateDescriptorSets(std::map<std::string, Image>& renderTargets)
	{
		std::deque<vk::DescriptorImageInfo> dsii{};
		auto const wSetImage = [&dsii](const vk::DescriptorSet& dstSet, uint32_t dstBinding, Image& image) {
			dsii.emplace_back(*image.sampler, *image.view, vk::ImageLayout::eShaderReadOnlyOptimal);
			return vk::WriteDescriptorSet{ dstSet, dstBinding, 0, 1, vk::DescriptorType::eCombinedImageSampler, &dsii.back(), nullptr, nullptr };
		};

		std::vector<vk::WriteDescriptorSet> textureWriteSets{
			wSetImage(*DSBrightFilter, 0, frameImage),
			wSetImage(*DSGaussianBlurHorizontal, 0, renderTargets["brightFilter"]),
			wSetImage(*DSGaussianBlurVertical, 0, renderTargets["gaussianBlurHorizontal"]),
			wSetImage(*DSCombine, 0, frameImage),
			wSetImage(*DSCombine, 1, renderTargets["gaussianBlurVertical"])
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
		rpi.renderPass = *renderPassBrightFilter.handle;
		rpi.framebuffer = *framebuffers[imageIndex].handle;
		rpi.renderArea.offset = vk::Offset2D{ 0, 0 };
		rpi.renderArea.extent = *renderTargets["brightFilter"].extent;
		rpi.clearValueCount = 1;
		rpi.pClearValues = clearValues.data();

		renderTargets["brightFilter"].changeLayout(cmd, LayoutState::ColorWrite);
		cmd.beginRenderPass(rpi, vk::SubpassContents::eInline);
		cmd.pushConstants<float>(*pipelineBrightFilter.layout, vk::ShaderStageFlagBits::eFragment, 0, values);
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipelineBrightFilter.handle);
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipelineBrightFilter.layout, 0, *DSBrightFilter, nullptr);
		cmd.draw(3, 1, 0, 0);
		cmd.endRenderPass();
		renderTargets["brightFilter"].changeLayout(cmd, LayoutState::ColorRead);

		rpi.renderPass = *renderPassGaussianBlur.handle;
		rpi.framebuffer = *framebuffers[static_cast<size_t>(totalImages) + static_cast<size_t>(imageIndex)].handle;

		renderTargets["gaussianBlurHorizontal"].changeLayout(cmd, LayoutState::ColorWrite);
		cmd.beginRenderPass(rpi, vk::SubpassContents::eInline);
		cmd.pushConstants<float>(*pipelineGaussianBlurHorizontal.layout, vk::ShaderStageFlagBits::eFragment, 0, values);
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipelineGaussianBlurHorizontal.handle);
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipelineBrightFilter.layout, 0, *DSGaussianBlurHorizontal, nullptr);
		cmd.draw(3, 1, 0, 0);
		cmd.endRenderPass();
		renderTargets["gaussianBlurHorizontal"].changeLayout(cmd, LayoutState::ColorRead);

		rpi.framebuffer = *framebuffers[static_cast<size_t>(totalImages) * 2 + static_cast<size_t>(imageIndex)].handle;

		renderTargets["gaussianBlurVertical"].changeLayout(cmd, LayoutState::ColorWrite);
		cmd.beginRenderPass(rpi, vk::SubpassContents::eInline);
		cmd.pushConstants<float>(*pipelineGaussianBlurVertical.layout, vk::ShaderStageFlagBits::eFragment, 0, values);
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipelineGaussianBlurVertical.handle);
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipelineGaussianBlurVertical.layout, 0, *DSGaussianBlurVertical, nullptr);
		cmd.draw(3, 1, 0, 0);
		cmd.endRenderPass();
		renderTargets["gaussianBlurVertical"].changeLayout(cmd, LayoutState::ColorRead);

		rpi.renderPass = *renderPassCombine.handle;
		rpi.framebuffer = *framebuffers[static_cast<size_t>(totalImages) * 3 + static_cast<size_t>(imageIndex)].handle;

		cmd.beginRenderPass(rpi, vk::SubpassContents::eInline);
		cmd.pushConstants<float>(*pipelineCombine.layout, vk::ShaderStageFlagBits::eFragment, 0, values);
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipelineCombine.handle);
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipelineCombine.layout, 0, *DSCombine, nullptr);
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
		pipelineBrightFilter.info.width = renderTargets["brightFilter"].width_f;
		pipelineBrightFilter.info.height = renderTargets["brightFilter"].height_f;
		pipelineBrightFilter.info.cullMode = CullMode::Back;
		pipelineBrightFilter.info.colorBlendAttachments = make_ref(std::vector<vk::PipelineColorBlendAttachmentState>{ *renderTargets["brightFilter"].blentAttachment });
		pipelineBrightFilter.info.pushConstantStage = PushConstantStage::Fragment;
		pipelineBrightFilter.info.pushConstantSize = 5 * sizeof(vec4);
		pipelineBrightFilter.info.descriptorSetLayouts = make_ref(std::vector<vk::DescriptorSetLayout>{ Pipeline::getDescriptorSetLayoutBrightFilter() });
		pipelineBrightFilter.info.renderPass = renderPassBrightFilter;

		pipelineBrightFilter.createGraphicsPipeline();
	}

	void Bloom::createGaussianBlurHorizontaPipeline(std::map<std::string, Image>& renderTargets)
	{
		Shader vert{ "shaders/Common/quad.vert", ShaderType::Vertex, true };
		Shader frag{ "shaders/Bloom/gaussianBlurHorizontal.frag", ShaderType::Fragment, true };

		pipelineGaussianBlurHorizontal.info.pVertShader = &vert;
		pipelineGaussianBlurHorizontal.info.pFragShader = &frag;
		pipelineGaussianBlurHorizontal.info.width = renderTargets["gaussianBlurHorizontal"].width_f;
		pipelineGaussianBlurHorizontal.info.height = renderTargets["gaussianBlurHorizontal"].height_f;
		pipelineGaussianBlurHorizontal.info.cullMode = CullMode::Back;
		pipelineGaussianBlurHorizontal.info.colorBlendAttachments = make_ref(std::vector<vk::PipelineColorBlendAttachmentState>{ *renderTargets["gaussianBlurHorizontal"].blentAttachment });
		pipelineGaussianBlurHorizontal.info.pushConstantStage = PushConstantStage::Fragment;
		pipelineGaussianBlurHorizontal.info.pushConstantSize = 5 * sizeof(vec4);
		pipelineGaussianBlurHorizontal.info.descriptorSetLayouts = make_ref(std::vector<vk::DescriptorSetLayout>{ Pipeline::getDescriptorSetLayoutGaussianBlurH() });
		pipelineGaussianBlurHorizontal.info.renderPass = renderPassGaussianBlur;

		pipelineGaussianBlurHorizontal.createGraphicsPipeline();
	}

	void Bloom::createGaussianBlurVerticalPipeline(std::map<std::string, Image>& renderTargets)
	{
		Shader vert{ "shaders/Common/quad.vert", ShaderType::Vertex, true };
		Shader frag{ "shaders/Bloom/gaussianBlurVertical.frag", ShaderType::Fragment, true };

		pipelineGaussianBlurVertical.info.pVertShader = &vert;
		pipelineGaussianBlurVertical.info.pFragShader = &frag;
		pipelineGaussianBlurVertical.info.width = renderTargets["gaussianBlurVertical"].width_f;
		pipelineGaussianBlurVertical.info.height = renderTargets["gaussianBlurVertical"].height_f;
		pipelineGaussianBlurVertical.info.cullMode = CullMode::Back;
		pipelineGaussianBlurVertical.info.colorBlendAttachments = make_ref(std::vector<vk::PipelineColorBlendAttachmentState>{ *renderTargets["gaussianBlurVertical"].blentAttachment });
		pipelineGaussianBlurVertical.info.pushConstantStage = PushConstantStage::Fragment;
		pipelineGaussianBlurVertical.info.pushConstantSize = 5 * sizeof(vec4);
		pipelineGaussianBlurVertical.info.descriptorSetLayouts = make_ref(std::vector<vk::DescriptorSetLayout>{ Pipeline::getDescriptorSetLayoutGaussianBlurV() });
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
		pipelineCombine.info.width = renderTargets["viewport"].width_f;
		pipelineCombine.info.height = renderTargets["viewport"].height_f;
		pipelineCombine.info.cullMode = CullMode::Back;
		pipelineCombine.info.colorBlendAttachments = make_ref(std::vector<vk::PipelineColorBlendAttachmentState>{ *renderTargets["viewport"].blentAttachment });
		pipelineCombine.info.pushConstantStage = PushConstantStage::Fragment;
		pipelineCombine.info.pushConstantSize = 5 * sizeof(vec4);
		pipelineCombine.info.descriptorSetLayouts = make_ref(std::vector<vk::DescriptorSetLayout>{ Pipeline::getDescriptorSetLayoutCombine() });
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
