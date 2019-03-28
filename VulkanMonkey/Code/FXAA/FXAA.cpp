#include "FXAA.h"

using namespace vm;

void FXAA::createFXAAUniforms(std::map<std::string, Image>& renderTargets)
{

	auto const allocateInfo2 = vk::DescriptorSetAllocateInfo()
		.setDescriptorPool(vulkan->descriptorPool)
		.setDescriptorSetCount(1)
		.setPSetLayouts(&DSLayout);
	DSet = vulkan->device.allocateDescriptorSets(allocateInfo2).at(0);

	updateDescriptorSets(renderTargets);
}

void FXAA::updateDescriptorSets(std::map<std::string, Image>& renderTargets)
{
	std::vector<vk::WriteDescriptorSet> textureWriteSets(1);
	// Composition sampler
	textureWriteSets[0] = vk::WriteDescriptorSet()
		.setDstSet(DSet)												// DescriptorSet dstSet;
		.setDstBinding(0)												// uint32_t dstBinding;
		.setDstArrayElement(0)											// uint32_t dstArrayElement;
		.setDescriptorCount(1)											// uint32_t descriptorCount;
		.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)	// DescriptorType descriptorType;
		.setPImageInfo(&vk::DescriptorImageInfo()						// const DescriptorImageInfo* pImageInfo;
			.setSampler(renderTargets["composition"].sampler)				// Sampler sampler;
			.setImageView(renderTargets["composition"].view)				// ImageView imageView;
			.setImageLayout(vk::ImageLayout::eColorAttachmentOptimal));		// ImageLayout imageLayout;

	vulkan->device.updateDescriptorSets(textureWriteSets, nullptr);
}

void FXAA::draw(uint32_t imageIndex)
{
	std::vector<vk::ClearValue> clearValues1 = {
		vk::ClearColorValue().setFloat32(GUI::clearColor),
		vk::ClearColorValue().setFloat32(GUI::clearColor) };

	auto renderPassInfo1 = vk::RenderPassBeginInfo()
		.setRenderPass(renderPass)
		.setFramebuffer(frameBuffers[imageIndex])
		.setRenderArea({ { 0, 0 }, vulkan->surface->actualExtent })
		.setClearValueCount(static_cast<uint32_t>(clearValues1.size()))
		.setPClearValues(clearValues1.data());
	vulkan->dynamicCmdBuffer.beginRenderPass(&renderPassInfo1, vk::SubpassContents::eInline);
	vulkan->dynamicCmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline.pipeline);
	vulkan->dynamicCmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline.pipeinfo.layout, 0, DSet, nullptr);
	vulkan->dynamicCmdBuffer.draw(3, 1, 0, 0);
	vulkan->dynamicCmdBuffer.endRenderPass();
}

void FXAA::destroy()
{
	for (auto &frameBuffer : frameBuffers) {
		if (frameBuffer) {
			vulkan->device.destroyFramebuffer(frameBuffer);
		}
	}
	if (renderPass) {
		vulkan->device.destroyRenderPass(renderPass);
		renderPass = nullptr;
	}
	if (DSLayout) {
		vulkan->device.destroyDescriptorSetLayout(DSLayout);
		DSLayout = nullptr;
	}
	pipeline.destroy();
}
