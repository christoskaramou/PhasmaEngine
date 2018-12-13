#include "SSR.h"

using namespace vm;

void SSR::createSSRUniforms(std::map<std::string, Image>& renderTargets)
{
	UBReflection.createBuffer(256, vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostCoherent);
	UBReflection.data = vulkan->device.mapMemory(UBReflection.memory, 0, UBReflection.size);

	auto const allocateInfo2 = vk::DescriptorSetAllocateInfo()
		.setDescriptorPool(vulkan->descriptorPool)
		.setDescriptorSetCount(1)
		.setPSetLayouts(&DSLayoutReflection);
	DSReflection = vulkan->device.allocateDescriptorSets(allocateInfo2)[0];

	std::vector<vk::WriteDescriptorSet> textureWriteSets(5);
	// Albedo
	textureWriteSets[0] = vk::WriteDescriptorSet()
		.setDstSet(DSReflection)									// DescriptorSet dstSet;
		.setDstBinding(0)												// uint32_t dstBinding;
		.setDstArrayElement(0)											// uint32_t dstArrayElement;
		.setDescriptorCount(1)											// uint32_t descriptorCount;
		.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)	// DescriptorType descriptorType;
		.setPImageInfo(&vk::DescriptorImageInfo()						// const DescriptorImageInfo* pImageInfo;
			.setSampler(renderTargets["albedo"].sampler)					// Sampler sampler;
			.setImageView(renderTargets["albedo"].view)					// ImageView imageView;
			.setImageLayout(vk::ImageLayout::eColorAttachmentOptimal));		// ImageLayout imageLayout;
	// Positions
	textureWriteSets[1] = vk::WriteDescriptorSet()
		.setDstSet(DSReflection)									// DescriptorSet dstSet;
		.setDstBinding(1)												// uint32_t dstBinding;
		.setDstArrayElement(0)											// uint32_t dstArrayElement;
		.setDescriptorCount(1)											// uint32_t descriptorCount;
		.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)	// DescriptorType descriptorType;
		.setPImageInfo(&vk::DescriptorImageInfo()						// const DescriptorImageInfo* pImageInfo;
			.setSampler(renderTargets["depth"].sampler)				// Sampler sampler;
			.setImageView(renderTargets["depth"].view)				// ImageView imageView;
			.setImageLayout(vk::ImageLayout::eColorAttachmentOptimal));		// ImageLayout imageLayout;
	// Normals
	textureWriteSets[2] = vk::WriteDescriptorSet()
		.setDstSet(DSReflection)									// DescriptorSet dstSet;
		.setDstBinding(2)												// uint32_t dstBinding;
		.setDstArrayElement(0)											// uint32_t dstArrayElement;
		.setDescriptorCount(1)											// uint32_t descriptorCount;
		.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)	// DescriptorType descriptorType;
		.setPImageInfo(&vk::DescriptorImageInfo()						// const DescriptorImageInfo* pImageInfo;
			.setSampler(renderTargets["normal"].sampler)					// Sampler sampler;
			.setImageView(renderTargets["normal"].view)					// ImageView imageView;
			.setImageLayout(vk::ImageLayout::eColorAttachmentOptimal));		// ImageLayout imageLayout;
	// Specular
	textureWriteSets[3] = vk::WriteDescriptorSet()
		.setDstSet(DSReflection)									// DescriptorSet dstSet;
		.setDstBinding(3)												// uint32_t dstBinding;
		.setDstArrayElement(0)											// uint32_t dstArrayElement;
		.setDescriptorCount(1)											// uint32_t descriptorCount;
		.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)	// DescriptorType descriptorType;
		.setPImageInfo(&vk::DescriptorImageInfo()						// const DescriptorImageInfo* pImageInfo;
			.setSampler(renderTargets["srm"].sampler)				// Sampler sampler;
			.setImageView(renderTargets["srm"].view)				// ImageView imageView;
			.setImageLayout(vk::ImageLayout::eColorAttachmentOptimal));		// ImageLayout imageLayout;
	// Uniform variables
	textureWriteSets[4] = vk::WriteDescriptorSet()
		.setDstSet(DSReflection)									// DescriptorSet dstSet;
		.setDstBinding(4)												// uint32_t dstBinding;
		.setDstArrayElement(0)											// uint32_t dstArrayElement;
		.setDescriptorCount(1)											// uint32_t descriptorCount;
		.setDescriptorType(vk::DescriptorType::eUniformBuffer)			// DescriptorType descriptorType;
		.setPBufferInfo(&vk::DescriptorBufferInfo()						// const DescriptorImageInfo* pImageInfo;
			.setBuffer(UBReflection.buffer)								// Buffer buffer;
			.setOffset(0)													// DeviceSize offset;
			.setRange(3 * 64));									// DeviceSize range;

	vulkan->device.updateDescriptorSets(textureWriteSets, nullptr);
}

void SSR::updateDescriptorSets(std::map<std::string, Image>& renderTargets)
{

	std::vector<vk::WriteDescriptorSet> textureWriteSets(5);
	// Albedo
	textureWriteSets[0] = vk::WriteDescriptorSet()
		.setDstSet(DSReflection)									// DescriptorSet dstSet;
		.setDstBinding(0)												// uint32_t dstBinding;
		.setDstArrayElement(0)											// uint32_t dstArrayElement;
		.setDescriptorCount(1)											// uint32_t descriptorCount;
		.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)	// DescriptorType descriptorType;
		.setPImageInfo(&vk::DescriptorImageInfo()						// const DescriptorImageInfo* pImageInfo;
			.setSampler(renderTargets["albedo"].sampler)					// Sampler sampler;
			.setImageView(renderTargets["albedo"].view)					// ImageView imageView;
			.setImageLayout(vk::ImageLayout::eColorAttachmentOptimal));		// ImageLayout imageLayout;
	// Positions
	textureWriteSets[1] = vk::WriteDescriptorSet()
		.setDstSet(DSReflection)									// DescriptorSet dstSet;
		.setDstBinding(1)												// uint32_t dstBinding;
		.setDstArrayElement(0)											// uint32_t dstArrayElement;
		.setDescriptorCount(1)											// uint32_t descriptorCount;
		.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)	// DescriptorType descriptorType;
		.setPImageInfo(&vk::DescriptorImageInfo()						// const DescriptorImageInfo* pImageInfo;
			.setSampler(renderTargets["depth"].sampler)				// Sampler sampler;
			.setImageView(renderTargets["depth"].view)				// ImageView imageView;
			.setImageLayout(vk::ImageLayout::eColorAttachmentOptimal));		// ImageLayout imageLayout;
	// Normals
	textureWriteSets[2] = vk::WriteDescriptorSet()
		.setDstSet(DSReflection)									// DescriptorSet dstSet;
		.setDstBinding(2)												// uint32_t dstBinding;
		.setDstArrayElement(0)											// uint32_t dstArrayElement;
		.setDescriptorCount(1)											// uint32_t descriptorCount;
		.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)	// DescriptorType descriptorType;
		.setPImageInfo(&vk::DescriptorImageInfo()						// const DescriptorImageInfo* pImageInfo;
			.setSampler(renderTargets["normal"].sampler)					// Sampler sampler;
			.setImageView(renderTargets["normal"].view)					// ImageView imageView;
			.setImageLayout(vk::ImageLayout::eColorAttachmentOptimal));		// ImageLayout imageLayout;
	// Specular
	textureWriteSets[3] = vk::WriteDescriptorSet()
		.setDstSet(DSReflection)									// DescriptorSet dstSet;
		.setDstBinding(3)												// uint32_t dstBinding;
		.setDstArrayElement(0)											// uint32_t dstArrayElement;
		.setDescriptorCount(1)											// uint32_t descriptorCount;
		.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)	// DescriptorType descriptorType;
		.setPImageInfo(&vk::DescriptorImageInfo()						// const DescriptorImageInfo* pImageInfo;
			.setSampler(renderTargets["srm"].sampler)				// Sampler sampler;
			.setImageView(renderTargets["srm"].view)				// ImageView imageView;
			.setImageLayout(vk::ImageLayout::eColorAttachmentOptimal));		// ImageLayout imageLayout;
	// Uniform variables
	textureWriteSets[4] = vk::WriteDescriptorSet()
		.setDstSet(DSReflection)									// DescriptorSet dstSet;
		.setDstBinding(4)												// uint32_t dstBinding;
		.setDstArrayElement(0)											// uint32_t dstArrayElement;
		.setDescriptorCount(1)											// uint32_t descriptorCount;
		.setDescriptorType(vk::DescriptorType::eUniformBuffer)			// DescriptorType descriptorType;
		.setPBufferInfo(&vk::DescriptorBufferInfo()						// const DescriptorImageInfo* pImageInfo;
			.setBuffer(UBReflection.buffer)								// Buffer buffer;
			.setOffset(0)													// DeviceSize offset;
			.setRange(3 * 64));									// DeviceSize range;

	vulkan->device.updateDescriptorSets(textureWriteSets, nullptr);
}

void SSR::draw(uint32_t imageIndex, const vec2 UVOffset[2])
{
	std::vector<vk::ClearValue> clearValues1 = {
	vk::ClearColorValue().setFloat32(GUI::clearColor) };
	auto renderPassInfo1 = vk::RenderPassBeginInfo()
		.setRenderPass(renderPass)
		.setFramebuffer(frameBuffers[imageIndex])
		.setRenderArea({ { 0, 0 }, vulkan->surface->actualExtent })
		.setClearValueCount(static_cast<uint32_t>(clearValues1.size()))
		.setPClearValues(clearValues1.data());
	vulkan->dynamicCmdBuffer.beginRenderPass(&renderPassInfo1, vk::SubpassContents::eInline);
	vulkan->dynamicCmdBuffer.pushConstants(pipeline.pipeinfo.layout, vk::ShaderStageFlagBits::eFragment, 0, 2 * sizeof(vec2), UVOffset);
	vulkan->dynamicCmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline.pipeline);
	vulkan->dynamicCmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline.pipeinfo.layout, 0, DSReflection, nullptr);
	vulkan->dynamicCmdBuffer.draw(3, 1, 0, 0);
	vulkan->dynamicCmdBuffer.endRenderPass();
}

void SSR::destroy()
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
	if (DSLayoutReflection) {
		vulkan->device.destroyDescriptorSetLayout(DSLayoutReflection);
		DSLayoutReflection = nullptr;
	}
	UBReflection.destroy();
	pipeline.destroy();
}
