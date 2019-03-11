#include "Bloom.h"
#include "../GUI/GUI.h"

using namespace vm;

void Bloom::update()
{
}

void Bloom::createUniforms(std::map<std::string, Image>& renderTargets)
{
	// Composition image to Bright Filter shader
	auto allocateInfo = vk::DescriptorSetAllocateInfo()
		.setDescriptorPool(vulkan->descriptorPool)
		.setDescriptorSetCount(1)
		.setPSetLayouts(&DSLayoutBrightFilter);
	DSBrightFilter = vulkan->device.allocateDescriptorSets(allocateInfo).at(0);

	std::vector<vk::WriteDescriptorSet> textureWriteSets(1);
	textureWriteSets[0] = vk::WriteDescriptorSet()
		.setDstSet(DSBrightFilter)										// DescriptorSet dstSet;
		.setDstBinding(0)												// uint32_t dstBinding;
		.setDstArrayElement(0)											// uint32_t dstArrayElement;
		.setDescriptorCount(1)											// uint32_t descriptorCount;
		.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)	// DescriptorType descriptorType;
		.setPImageInfo(&vk::DescriptorImageInfo()						// const DescriptorImageInfo* pImageInfo;
			.setSampler(renderTargets["composition"].sampler)				// Sampler sampler;
			.setImageView(renderTargets["composition"].view)				// ImageView imageView;
			.setImageLayout(vk::ImageLayout::eColorAttachmentOptimal));		// ImageLayout imageLayout;

	vulkan->device.updateDescriptorSets(textureWriteSets, nullptr);

	// Bright Filter image to Gaussian Blur Horizontal shader
	allocateInfo = vk::DescriptorSetAllocateInfo()
		.setDescriptorPool(vulkan->descriptorPool)
		.setDescriptorSetCount(1)
		.setPSetLayouts(&DSLayoutGaussianBlurHorizontal);
	DSGaussianBlurHorizontal = vulkan->device.allocateDescriptorSets(allocateInfo).at(0);

	textureWriteSets[0] = vk::WriteDescriptorSet()
		.setDstSet(DSGaussianBlurHorizontal)							// DescriptorSet dstSet;
		.setDstBinding(0)												// uint32_t dstBinding;
		.setDstArrayElement(0)											// uint32_t dstArrayElement;
		.setDescriptorCount(1)											// uint32_t descriptorCount;
		.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)	// DescriptorType descriptorType;
		.setPImageInfo(&vk::DescriptorImageInfo()						// const DescriptorImageInfo* pImageInfo;
			.setSampler(renderTargets["brightFilter"].sampler)				// Sampler sampler;
			.setImageView(renderTargets["brightFilter"].view)				// ImageView imageView;
			.setImageLayout(vk::ImageLayout::eColorAttachmentOptimal));		// ImageLayout imageLayout;

	vulkan->device.updateDescriptorSets(textureWriteSets, nullptr);

	// Gaussian Blur Horizontal image to Gaussian Blur Vertical shader
	allocateInfo = vk::DescriptorSetAllocateInfo()
		.setDescriptorPool(vulkan->descriptorPool)
		.setDescriptorSetCount(1)
		.setPSetLayouts(&DSLayoutGaussianBlurVertical);
	DSGaussianBlurVertical = vulkan->device.allocateDescriptorSets(allocateInfo).at(0);

	textureWriteSets[0] = vk::WriteDescriptorSet()
		.setDstSet(DSGaussianBlurVertical)								// DescriptorSet dstSet;
		.setDstBinding(0)												// uint32_t dstBinding;
		.setDstArrayElement(0)											// uint32_t dstArrayElement;
		.setDescriptorCount(1)											// uint32_t descriptorCount;
		.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)	// DescriptorType descriptorType;
		.setPImageInfo(&vk::DescriptorImageInfo()						// const DescriptorImageInfo* pImageInfo;
			.setSampler(renderTargets["gaussianBlurHorizontal"].sampler)	// Sampler sampler;
			.setImageView(renderTargets["gaussianBlurHorizontal"].view)		// ImageView imageView;
			.setImageLayout(vk::ImageLayout::eColorAttachmentOptimal));		// ImageLayout imageLayout;

	vulkan->device.updateDescriptorSets(textureWriteSets, nullptr);

	// Gaussian Blur Vertical image to Combine shader
	allocateInfo = vk::DescriptorSetAllocateInfo()
		.setDescriptorPool(vulkan->descriptorPool)
		.setDescriptorSetCount(1)
		.setPSetLayouts(&DSLayoutCombine);
	DSCombine = vulkan->device.allocateDescriptorSets(allocateInfo).at(0);

	std::vector<vk::WriteDescriptorSet> textureWriteSets2(2);
	textureWriteSets2[0] = vk::WriteDescriptorSet()
		.setDstSet(DSCombine)											// DescriptorSet dstSet;
		.setDstBinding(0)												// uint32_t dstBinding;
		.setDstArrayElement(0)											// uint32_t dstArrayElement;
		.setDescriptorCount(1)											// uint32_t descriptorCount;
		.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)	// DescriptorType descriptorType;
		.setPImageInfo(&vk::DescriptorImageInfo()						// const DescriptorImageInfo* pImageInfo;
			.setSampler(renderTargets["composition"].sampler)				// Sampler sampler;
			.setImageView(renderTargets["composition"].view)				// ImageView imageView;
			.setImageLayout(vk::ImageLayout::eColorAttachmentOptimal));		// ImageLayout imageLayout;

	textureWriteSets2[1] = vk::WriteDescriptorSet()
		.setDstSet(DSCombine)											// DescriptorSet dstSet;
		.setDstBinding(1)												// uint32_t dstBinding;
		.setDstArrayElement(0)											// uint32_t dstArrayElement;
		.setDescriptorCount(1)											// uint32_t descriptorCount;
		.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)	// DescriptorType descriptorType;
		.setPImageInfo(&vk::DescriptorImageInfo()						// const DescriptorImageInfo* pImageInfo;
			.setSampler(renderTargets["gaussianBlurVertical"].sampler)		// Sampler sampler;
			.setImageView(renderTargets["gaussianBlurVertical"].view)		// ImageView imageView;
			.setImageLayout(vk::ImageLayout::eColorAttachmentOptimal));		// ImageLayout imageLayout;

	vulkan->device.updateDescriptorSets(textureWriteSets2, nullptr);
}

void Bloom::updateDescriptorSets(std::map<std::string, Image>& renderTargets)
{
	std::string comp = "composition";
	if (GUI::show_FXAA) comp = "composition2";

	// Composition image to Bright Filter shader
	std::vector<vk::WriteDescriptorSet> textureWriteSets(1);
	textureWriteSets[0] = vk::WriteDescriptorSet()
		.setDstSet(DSBrightFilter)										// DescriptorSet dstSet;
		.setDstBinding(0)												// uint32_t dstBinding;
		.setDstArrayElement(0)											// uint32_t dstArrayElement;
		.setDescriptorCount(1)											// uint32_t descriptorCount;
		.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)	// DescriptorType descriptorType;
		.setPImageInfo(&vk::DescriptorImageInfo()						// const DescriptorImageInfo* pImageInfo;
			.setSampler(renderTargets[comp].sampler)						// Sampler sampler;
			.setImageView(renderTargets[comp].view)							// ImageView imageView;
			.setImageLayout(vk::ImageLayout::eColorAttachmentOptimal));		// ImageLayout imageLayout;
	vulkan->device.updateDescriptorSets(textureWriteSets, nullptr);

	// Bright Filter image to Gaussian Blur Horizontal shader
	textureWriteSets[0] = vk::WriteDescriptorSet()
		.setDstSet(DSGaussianBlurHorizontal)							// DescriptorSet dstSet;
		.setDstBinding(0)												// uint32_t dstBinding;
		.setDstArrayElement(0)											// uint32_t dstArrayElement;
		.setDescriptorCount(1)											// uint32_t descriptorCount;
		.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)	// DescriptorType descriptorType;
		.setPImageInfo(&vk::DescriptorImageInfo()						// const DescriptorImageInfo* pImageInfo;
			.setSampler(renderTargets["brightFilter"].sampler)				// Sampler sampler;
			.setImageView(renderTargets["brightFilter"].view)				// ImageView imageView;
			.setImageLayout(vk::ImageLayout::eColorAttachmentOptimal));		// ImageLayout imageLayout;
	vulkan->device.updateDescriptorSets(textureWriteSets, nullptr);

	// Gaussian Blur Horizontal image to Gaussian Blur Vertical shader
	textureWriteSets[0] = vk::WriteDescriptorSet()
		.setDstSet(DSGaussianBlurVertical)								// DescriptorSet dstSet;
		.setDstBinding(0)												// uint32_t dstBinding;
		.setDstArrayElement(0)											// uint32_t dstArrayElement;
		.setDescriptorCount(1)											// uint32_t descriptorCount;
		.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)	// DescriptorType descriptorType;
		.setPImageInfo(&vk::DescriptorImageInfo()						// const DescriptorImageInfo* pImageInfo;
			.setSampler(renderTargets["gaussianBlurHorizontal"].sampler)	// Sampler sampler;
			.setImageView(renderTargets["gaussianBlurHorizontal"].view)		// ImageView imageView;
			.setImageLayout(vk::ImageLayout::eColorAttachmentOptimal));		// ImageLayout imageLayout;
	vulkan->device.updateDescriptorSets(textureWriteSets, nullptr);

	// Gaussian Blur Vertical image to Combine shader
	std::vector<vk::WriteDescriptorSet> textureWriteSets2(2);
	textureWriteSets2[0] = vk::WriteDescriptorSet()
		.setDstSet(DSCombine)											// DescriptorSet dstSet;
		.setDstBinding(0)												// uint32_t dstBinding;
		.setDstArrayElement(0)											// uint32_t dstArrayElement;
		.setDescriptorCount(1)											// uint32_t descriptorCount;
		.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)	// DescriptorType descriptorType;
		.setPImageInfo(&vk::DescriptorImageInfo()						// const DescriptorImageInfo* pImageInfo;
			.setSampler(renderTargets[comp].sampler)						// Sampler sampler;
			.setImageView(renderTargets[comp].view)							// ImageView imageView;
			.setImageLayout(vk::ImageLayout::eColorAttachmentOptimal));		// ImageLayout imageLayout;
	textureWriteSets2[1] = vk::WriteDescriptorSet()
		.setDstSet(DSCombine)											// DescriptorSet dstSet;
		.setDstBinding(1)												// uint32_t dstBinding;
		.setDstArrayElement(0)											// uint32_t dstArrayElement;
		.setDescriptorCount(1)											// uint32_t descriptorCount;
		.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)	// DescriptorType descriptorType;
		.setPImageInfo(&vk::DescriptorImageInfo()						// const DescriptorImageInfo* pImageInfo;
			.setSampler(renderTargets["gaussianBlurVertical"].sampler)		// Sampler sampler;
			.setImageView(renderTargets["gaussianBlurVertical"].view)		// ImageView imageView;
			.setImageLayout(vk::ImageLayout::eColorAttachmentOptimal));		// ImageLayout imageLayout;
	vulkan->device.updateDescriptorSets(textureWriteSets2, nullptr);
}

void Bloom::draw(uint32_t imageIndex, uint32_t totalImages, const vec2 UVOffset[2])
{
	std::vector<vk::ClearValue> clearValues = {
	vk::ClearColorValue().setFloat32(GUI::clearColor) };
	float values[]{ GUI::Bloom_Inv_brightness, GUI::Bloom_intensity, GUI::Bloom_range, GUI::Bloom_exposure, (float)GUI::use_tonemap };

	auto renderPassInfo = vk::RenderPassBeginInfo()
		.setRenderPass(renderPassBrightFilter)
		.setFramebuffer(frameBuffers[imageIndex])// <----------------------------------------------------------------------------
		.setRenderArea({ { 0, 0 }, vulkan->surface->actualExtent })
		.setClearValueCount(static_cast<uint32_t>(clearValues.size()))
		.setPClearValues(clearValues.data());
	vulkan->dynamicCmdBuffer.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);
	vulkan->dynamicCmdBuffer.pushConstants(pipelineBrightFilter.pipeinfo.layout, vk::ShaderStageFlagBits::eFragment, 0, sizeof(values), values);
	vulkan->dynamicCmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelineBrightFilter.pipeline);
	vulkan->dynamicCmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineBrightFilter.pipeinfo.layout, 0, DSBrightFilter, nullptr);
	vulkan->dynamicCmdBuffer.draw(3, 1, 0, 0);
	vulkan->dynamicCmdBuffer.endRenderPass();

	renderPassInfo = vk::RenderPassBeginInfo()
		.setRenderPass(renderPassGaussianBlur)
		.setFramebuffer(frameBuffers[totalImages + imageIndex])
		.setRenderArea({ { 0, 0 }, vulkan->surface->actualExtent })
		.setClearValueCount(static_cast<uint32_t>(clearValues.size()))
		.setPClearValues(clearValues.data());
	vulkan->dynamicCmdBuffer.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);
	vulkan->dynamicCmdBuffer.pushConstants(pipelineGaussianBlurHorizontal.pipeinfo.layout, vk::ShaderStageFlagBits::eFragment, 0, sizeof(values), values);
	vulkan->dynamicCmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelineGaussianBlurHorizontal.pipeline);
	vulkan->dynamicCmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineBrightFilter.pipeinfo.layout, 0, DSGaussianBlurHorizontal, nullptr);
	vulkan->dynamicCmdBuffer.draw(3, 1, 0, 0);
	vulkan->dynamicCmdBuffer.endRenderPass();

	renderPassInfo = vk::RenderPassBeginInfo()
		.setRenderPass(renderPassGaussianBlur)
		.setFramebuffer(frameBuffers[totalImages * 2 + imageIndex])
		.setRenderArea({ { 0, 0 }, vulkan->surface->actualExtent })
		.setClearValueCount(static_cast<uint32_t>(clearValues.size()))
		.setPClearValues(clearValues.data());
	vulkan->dynamicCmdBuffer.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);
	vulkan->dynamicCmdBuffer.pushConstants(pipelineGaussianBlurVertical.pipeinfo.layout, vk::ShaderStageFlagBits::eFragment, 0, sizeof(values), values);
	vulkan->dynamicCmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelineGaussianBlurVertical.pipeline);
	vulkan->dynamicCmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineGaussianBlurVertical.pipeinfo.layout, 0, DSGaussianBlurVertical, nullptr);
	vulkan->dynamicCmdBuffer.draw(3, 1, 0, 0);
	vulkan->dynamicCmdBuffer.endRenderPass();

	std::vector<vk::ClearValue> clearValues1 = {
		vk::ClearColorValue().setFloat32(GUI::clearColor),
		vk::ClearColorValue().setFloat32(GUI::clearColor) };
	renderPassInfo = vk::RenderPassBeginInfo()
		.setRenderPass(renderPassCombine)
		.setFramebuffer(frameBuffers[totalImages * 3 + imageIndex])
		.setRenderArea({ { 0, 0 }, vulkan->surface->actualExtent })
		.setClearValueCount(static_cast<uint32_t>(clearValues1.size()))
		.setPClearValues(clearValues1.data());
	vulkan->dynamicCmdBuffer.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);
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
