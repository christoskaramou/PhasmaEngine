#include "Bloom.h"
#include "../GUI/GUI.h"
#include <deque>

using namespace vm;

void Bloom::update()
{
}

void Bloom::createUniforms(std::map<std::string, Image>& renderTargets)
{
	vk::DescriptorSetAllocateInfo allocateInfo;
	allocateInfo.descriptorPool = vulkan->descriptorPool;
	allocateInfo.descriptorSetCount = 1;

	// Composition image to Bright Filter shader
	allocateInfo.pSetLayouts = &DSLayoutBrightFilter;
	DSBrightFilter = vulkan->device.allocateDescriptorSets(allocateInfo).at(0);

	// Bright Filter image to Gaussian Blur Horizontal shader
	allocateInfo.pSetLayouts = &DSLayoutGaussianBlurHorizontal;
	DSGaussianBlurHorizontal = vulkan->device.allocateDescriptorSets(allocateInfo).at(0);

	// Gaussian Blur Horizontal image to Gaussian Blur Vertical shader
	allocateInfo.pSetLayouts = &DSLayoutGaussianBlurVertical;
	DSGaussianBlurVertical = vulkan->device.allocateDescriptorSets(allocateInfo).at(0);

	// Gaussian Blur Vertical image to Combine shader
	allocateInfo.pSetLayouts = &DSLayoutCombine;
	DSCombine = vulkan->device.allocateDescriptorSets(allocateInfo).at(0);

	updateDescriptorSets(renderTargets);
}

void Bloom::updateDescriptorSets(std::map<std::string, Image>& renderTargets)
{
	std::deque<vk::DescriptorImageInfo> dsii{};
	auto wSetImage = [&dsii](vk::DescriptorSet& dstSet, uint32_t dstBinding, Image& image) {
		dsii.push_back({ image.sampler, image.view, vk::ImageLayout::eShaderReadOnlyOptimal });
		return vk::WriteDescriptorSet{ dstSet, dstBinding, 0, 1, vk::DescriptorType::eCombinedImageSampler, &dsii.back(), nullptr, nullptr };
	};
	std::deque<vk::DescriptorBufferInfo> dsbi{};
	auto wSetBuffer = [&dsbi](vk::DescriptorSet& dstSet, uint32_t dstBinding, Buffer& buffer) {
		dsbi.push_back({ buffer.buffer, 0, buffer.size });
		return vk::WriteDescriptorSet{ dstSet, dstBinding, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &dsbi.back(), nullptr };
	};

	std::string comp = "composition";
	if (GUI::show_FXAA) comp = "composition2";

	std::vector<vk::WriteDescriptorSet> textureWriteSets{
		wSetImage(DSBrightFilter, 0, renderTargets[comp]),
		wSetImage(DSGaussianBlurHorizontal, 0, renderTargets["brightFilter"]),
		wSetImage(DSGaussianBlurVertical, 0, renderTargets["gaussianBlurHorizontal"]),
		wSetImage(DSCombine, 0, renderTargets[comp]),
		wSetImage(DSCombine, 1, renderTargets["gaussianBlurVertical"])
	};
	vulkan->device.updateDescriptorSets(textureWriteSets, nullptr);
}

void Bloom::draw(uint32_t imageIndex, uint32_t totalImages, const vec2 UVOffset[2])
{
	vk::ClearColorValue clearColor;
	memcpy(clearColor.float32, GUI::clearColor.data(), 4 * sizeof(float));

	std::vector<vk::ClearValue> clearValues = { clearColor, clearColor };

	float values[]{ GUI::Bloom_Inv_brightness, GUI::Bloom_intensity, GUI::Bloom_range, GUI::Bloom_exposure, (float)GUI::use_tonemap };

	vk::RenderPassBeginInfo rpi;
	rpi.renderPass = renderPassBrightFilter;
	rpi.framebuffer = frameBuffers[imageIndex];
	rpi.renderArea = { { 0, 0 }, vulkan->surface->actualExtent };
	rpi.clearValueCount = 1;
	rpi.pClearValues = clearValues.data();

	vulkan->dynamicCmdBuffer.beginRenderPass(rpi, vk::SubpassContents::eInline);
	vulkan->dynamicCmdBuffer.pushConstants(pipelineBrightFilter.pipeinfo.layout, vk::ShaderStageFlagBits::eFragment, 0, sizeof(values), values);
	vulkan->dynamicCmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelineBrightFilter.pipeline);
	vulkan->dynamicCmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineBrightFilter.pipeinfo.layout, 0, DSBrightFilter, nullptr);
	vulkan->dynamicCmdBuffer.draw(3, 1, 0, 0);
	vulkan->dynamicCmdBuffer.endRenderPass();

	rpi.renderPass = renderPassGaussianBlur;
	rpi.framebuffer = frameBuffers[totalImages + imageIndex];

	vulkan->dynamicCmdBuffer.beginRenderPass(rpi, vk::SubpassContents::eInline);
	vulkan->dynamicCmdBuffer.pushConstants(pipelineGaussianBlurHorizontal.pipeinfo.layout, vk::ShaderStageFlagBits::eFragment, 0, sizeof(values), values);
	vulkan->dynamicCmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelineGaussianBlurHorizontal.pipeline);
	vulkan->dynamicCmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineBrightFilter.pipeinfo.layout, 0, DSGaussianBlurHorizontal, nullptr);
	vulkan->dynamicCmdBuffer.draw(3, 1, 0, 0);
	vulkan->dynamicCmdBuffer.endRenderPass();

	rpi.framebuffer = frameBuffers[totalImages * 2 + imageIndex];

	vulkan->dynamicCmdBuffer.beginRenderPass(rpi, vk::SubpassContents::eInline);
	vulkan->dynamicCmdBuffer.pushConstants(pipelineGaussianBlurVertical.pipeinfo.layout, vk::ShaderStageFlagBits::eFragment, 0, sizeof(values), values);
	vulkan->dynamicCmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelineGaussianBlurVertical.pipeline);
	vulkan->dynamicCmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineGaussianBlurVertical.pipeinfo.layout, 0, DSGaussianBlurVertical, nullptr);
	vulkan->dynamicCmdBuffer.draw(3, 1, 0, 0);
	vulkan->dynamicCmdBuffer.endRenderPass();

	rpi.renderPass = renderPassCombine;
	rpi.framebuffer = frameBuffers[totalImages * 3 + imageIndex];
	rpi.clearValueCount = 2;

	vulkan->dynamicCmdBuffer.beginRenderPass(rpi, vk::SubpassContents::eInline);
	vulkan->dynamicCmdBuffer.pushConstants(pipelineCombine.pipeinfo.layout, vk::ShaderStageFlagBits::eFragment, 0, sizeof(values), values);
	vulkan->dynamicCmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelineCombine.pipeline);
	vulkan->dynamicCmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineCombine.pipeinfo.layout, 0, DSCombine, nullptr);
	vulkan->dynamicCmdBuffer.draw(3, 1, 0, 0);
	vulkan->dynamicCmdBuffer.endRenderPass();
}

void Bloom::destroy()
{
	for (auto &frameBuffer : frameBuffers) {
		if (frameBuffer) {
			vulkan->device.destroyFramebuffer(frameBuffer);
		}
	}
	if (renderPassBrightFilter) {
		vulkan->device.destroyRenderPass(renderPassBrightFilter);
		renderPassBrightFilter = nullptr;
	}
	if (renderPassGaussianBlur) {
		vulkan->device.destroyRenderPass(renderPassGaussianBlur);
		renderPassGaussianBlur = nullptr;
	}
	if (renderPassCombine) {
		vulkan->device.destroyRenderPass(renderPassCombine);
		renderPassCombine = nullptr;
	}
	if (DSLayoutBrightFilter) {
		vulkan->device.destroyDescriptorSetLayout(DSLayoutBrightFilter);
		DSLayoutBrightFilter = nullptr;
	}
	if (DSLayoutGaussianBlurHorizontal) {
		vulkan->device.destroyDescriptorSetLayout(DSLayoutGaussianBlurHorizontal);
		DSLayoutGaussianBlurHorizontal = nullptr;
	}
	if (DSLayoutGaussianBlurVertical) {
		vulkan->device.destroyDescriptorSetLayout(DSLayoutGaussianBlurVertical);
		DSLayoutGaussianBlurVertical = nullptr;
	}
	if (DSLayoutCombine) {
		vulkan->device.destroyDescriptorSetLayout(DSLayoutCombine);
		DSLayoutCombine = nullptr;
	}
	pipelineBrightFilter.destroy();
	pipelineGaussianBlurHorizontal.destroy();
	pipelineGaussianBlurVertical.destroy();
	pipelineCombine.destroy();
}
