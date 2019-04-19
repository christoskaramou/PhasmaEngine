#include "FXAA.h"

using namespace vm;

void FXAA::createFXAAUniforms(std::map<std::string, Image>& renderTargets)
{

	vk::DescriptorSetAllocateInfo allocateInfo2;
	allocateInfo2.descriptorPool = vulkan->descriptorPool;
	allocateInfo2.descriptorSetCount = 1;
	allocateInfo2.pSetLayouts = &DSLayout;
	DSet = vulkan->device.allocateDescriptorSets(allocateInfo2).at(0);

	updateDescriptorSets(renderTargets);
}

void FXAA::updateDescriptorSets(std::map<std::string, Image>& renderTargets)
{
	// Composition sampler
	vk::DescriptorImageInfo dii;
	dii.sampler = renderTargets["composition"].sampler;
	dii.imageView = renderTargets["composition"].view;
	dii.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

	vk::WriteDescriptorSet textureWriteSet;
	textureWriteSet.dstSet = DSet;
	textureWriteSet.dstBinding = 0;
	textureWriteSet.dstArrayElement = 0;
	textureWriteSet.descriptorCount = 1;
	textureWriteSet.descriptorType = vk::DescriptorType::eCombinedImageSampler;
	textureWriteSet.pImageInfo = &dii;

	vulkan->device.updateDescriptorSets(textureWriteSet, nullptr);
}

void FXAA::draw(uint32_t imageIndex)
{
	vk::ClearColorValue clearColor;
	memcpy(clearColor.float32, GUI::clearColor.data(), 4 * sizeof(float));

	std::vector<vk::ClearValue> clearValues = { clearColor, clearColor };

	vk::RenderPassBeginInfo rpi;
	rpi.renderPass = renderPass;
	rpi.framebuffer = frameBuffers[imageIndex];
	rpi.renderArea = { { 0, 0 }, vulkan->surface->actualExtent };
	rpi.clearValueCount = 2;
	rpi.pClearValues = clearValues.data();

	vulkan->dynamicCmdBuffer.beginRenderPass(rpi, vk::SubpassContents::eInline);
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
