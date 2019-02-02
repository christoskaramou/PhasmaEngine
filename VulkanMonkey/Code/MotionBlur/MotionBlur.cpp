#include "MotionBlur.h"

using namespace vm;

void MotionBlur::createMotionBlurFrameBuffers()
{
	frameBuffers.resize(vulkan->swapchain->images.size());
	for (size_t i = 0; i < frameBuffers.size(); ++i) {
		std::vector<vk::ImageView> attachments = {
			vulkan->swapchain->images[i].view
		};

		auto const fbci = vk::FramebufferCreateInfo()
			.setRenderPass(renderPass)
			.setAttachmentCount(static_cast<uint32_t>(attachments.size()))
			.setPAttachments(attachments.data())
			.setWidth(WIDTH)
			.setHeight(HEIGHT)
			.setLayers(1);
		frameBuffers[i] = vulkan->device.createFramebuffer(fbci);
	}
}

void MotionBlur::createMotionBlurUniforms(std::map<std::string, Image>& renderTargets)
{
	UBmotionBlur.createBuffer(4 * sizeof(mat4), vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostCoherent);
	UBmotionBlur.data = vulkan->device.mapMemory(UBmotionBlur.memory, 0, UBmotionBlur.size);

	auto const allocateInfo = vk::DescriptorSetAllocateInfo()
		.setDescriptorPool(vulkan->descriptorPool)
		.setDescriptorSetCount(1)
		.setPSetLayouts(&DSLayoutMotionBlur);
	DSMotionBlur = vulkan->device.allocateDescriptorSets(allocateInfo).at(0);

	std::vector<vk::WriteDescriptorSet> textureWriteSets(3);
	// Composition image
	textureWriteSets[0] = vk::WriteDescriptorSet()
		.setDstSet(DSMotionBlur)									// DescriptorSet dstSet;
		.setDstBinding(0)												// uint32_t dstBinding;
		.setDstArrayElement(0)											// uint32_t dstArrayElement;
		.setDescriptorCount(1)											// uint32_t descriptorCount;
		.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)	// DescriptorType descriptorType;
		.setPImageInfo(&vk::DescriptorImageInfo()						// const DescriptorImageInfo* pImageInfo;
			.setSampler(renderTargets["composition"].sampler)					// Sampler sampler;
			.setImageView(renderTargets["composition"].view)					// ImageView imageView;
			.setImageLayout(vk::ImageLayout::eColorAttachmentOptimal));		// ImageLayout imageLayout;
	// Positions
	textureWriteSets[1] = vk::WriteDescriptorSet()
		.setDstSet(DSMotionBlur)									// DescriptorSet dstSet;
		.setDstBinding(1)												// uint32_t dstBinding;
		.setDstArrayElement(0)											// uint32_t dstArrayElement;
		.setDescriptorCount(1)											// uint32_t descriptorCount;
		.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)	// DescriptorType descriptorType;
		.setPImageInfo(&vk::DescriptorImageInfo()						// const DescriptorImageInfo* pImageInfo;
			.setSampler(renderTargets["depth"].sampler)				// Sampler sampler;
			.setImageView(renderTargets["depth"].view)				// ImageView imageView;
			.setImageLayout(vk::ImageLayout::eColorAttachmentOptimal));		// ImageLayout imageLayout;

		// Uniform variables
	textureWriteSets[2] = vk::WriteDescriptorSet()
		.setDstSet(DSMotionBlur)									// DescriptorSet dstSet;
		.setDstBinding(2)												// uint32_t dstBinding;
		.setDstArrayElement(0)											// uint32_t dstArrayElement;
		.setDescriptorCount(1)											// uint32_t descriptorCount;
		.setDescriptorType(vk::DescriptorType::eUniformBuffer)			// DescriptorType descriptorType;
		.setPBufferInfo(&vk::DescriptorBufferInfo()						// const DescriptorImageInfo* pImageInfo;
			.setBuffer(UBmotionBlur.buffer)								// Buffer buffer;
			.setOffset(0)													// DeviceSize offset;
			.setRange(UBmotionBlur.size));									// DeviceSize range;

	vulkan->device.updateDescriptorSets(textureWriteSets, nullptr);
}

void MotionBlur::updateDescriptorSets(std::map<std::string, Image>& renderTargets)
{
	std::vector<vk::WriteDescriptorSet> textureWriteSets(3);
	// Composition
	textureWriteSets[0] = vk::WriteDescriptorSet()
		.setDstSet(DSMotionBlur)									// DescriptorSet dstSet;
		.setDstBinding(0)												// uint32_t dstBinding;
		.setDstArrayElement(0)											// uint32_t dstArrayElement;
		.setDescriptorCount(1)											// uint32_t descriptorCount;
		.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)	// DescriptorType descriptorType;
		.setPImageInfo(&vk::DescriptorImageInfo()						// const DescriptorImageInfo* pImageInfo;
			.setSampler(renderTargets["composition"].sampler)					// Sampler sampler;
			.setImageView(renderTargets["composition"].view)					// ImageView imageView;
			.setImageLayout(vk::ImageLayout::eColorAttachmentOptimal));		// ImageLayout imageLayout;
	// Positions
	textureWriteSets[1] = vk::WriteDescriptorSet()
		.setDstSet(DSMotionBlur)									// DescriptorSet dstSet;
		.setDstBinding(1)												// uint32_t dstBinding;
		.setDstArrayElement(0)											// uint32_t dstArrayElement;
		.setDescriptorCount(1)											// uint32_t descriptorCount;
		.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)	// DescriptorType descriptorType;
		.setPImageInfo(&vk::DescriptorImageInfo()						// const DescriptorImageInfo* pImageInfo;
			.setSampler(renderTargets["depth"].sampler)				// Sampler sampler;
			.setImageView(renderTargets["depth"].view)				// ImageView imageView;
			.setImageLayout(vk::ImageLayout::eColorAttachmentOptimal));		// ImageLayout imageLayout;

		// Uniform variables
	textureWriteSets[2] = vk::WriteDescriptorSet()
		.setDstSet(DSMotionBlur)									// DescriptorSet dstSet;
		.setDstBinding(2)												// uint32_t dstBinding;
		.setDstArrayElement(0)											// uint32_t dstArrayElement;
		.setDescriptorCount(1)											// uint32_t descriptorCount;
		.setDescriptorType(vk::DescriptorType::eUniformBuffer)			// DescriptorType descriptorType;
		.setPBufferInfo(&vk::DescriptorBufferInfo()						// const DescriptorImageInfo* pImageInfo;
			.setBuffer(UBmotionBlur.buffer)								// Buffer buffer;
			.setOffset(0)													// DeviceSize offset;
			.setRange(UBmotionBlur.size));									// DeviceSize range;

	vulkan->device.updateDescriptorSets(textureWriteSets, nullptr);
}

void MotionBlur::draw(uint32_t imageIndex, const vec2 UVOffset[2])
{
	std::vector<vk::ClearValue> clearValues1 = {
	vk::ClearColorValue().setFloat32(GUI::clearColor) };

	auto renderPassInfo1 = vk::RenderPassBeginInfo()
		.setRenderPass(renderPass)
		.setFramebuffer(frameBuffers[imageIndex])
		.setRenderArea({ { 0, 0 }, vulkan->surface->actualExtent })
		.setClearValueCount(static_cast<uint32_t>(clearValues1.size()))
		.setPClearValues(clearValues1.data());
	vulkan->dynamicCmdBuffer.beginRenderPass(renderPassInfo1, vk::SubpassContents::eInline);
	vec4 fps[2]{ {1.f / Timer::delta}, {UVOffset[0].x, UVOffset[0].y, UVOffset[1].x, UVOffset[1].y} };
	vulkan->dynamicCmdBuffer.pushConstants(pipeline.pipeinfo.layout, vk::ShaderStageFlagBits::eFragment, 0, 2 * sizeof(vec4), &fps);
	vulkan->dynamicCmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline.pipeline);
	vulkan->dynamicCmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline.pipeinfo.layout, 0, DSMotionBlur, nullptr);
	vulkan->dynamicCmdBuffer.draw(3, 1, 0, 0);
	vulkan->dynamicCmdBuffer.endRenderPass();
}

void MotionBlur::destroy()
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
	if (DSLayoutMotionBlur) {
		vulkan->device.destroyDescriptorSetLayout(DSLayoutMotionBlur);
		DSLayoutMotionBlur = nullptr;
	}
	UBmotionBlur.destroy();
	pipeline.destroy();
}

void vm::MotionBlur::update(Camera& camera)
{
	if (GUI::show_motionBlur) {
		static mat4 previousView = camera.view;
		mat4 motionBlurInput[4]{ camera.projection, camera.view, previousView, camera.invViewProjection };
		memcpy(UBmotionBlur.data, &motionBlurInput, sizeof(motionBlurInput));
		previousView = camera.view;
	}
}
