#include "../include/MotionBlur.h"
#include "../include/Errors.h"

void vm::MotionBlur::createMotionBlurFrameBuffers()
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
			.setWidth(vulkan->surface->actualExtent.width)
			.setHeight(vulkan->surface->actualExtent.height)
			.setLayers(1);
		VkCheck(vulkan->device.createFramebuffer(&fbci, nullptr, &frameBuffers[i]));
		std::cout << "Framebuffer created\n";
	}
}

void vm::MotionBlur::createMotionBlurUniforms(std::map<std::string, Image>& renderTargets)
{
	UBmotionBlur.createBuffer(256, vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostCoherent);
	VkCheck(vulkan->device.mapMemory(UBmotionBlur.memory, 0, UBmotionBlur.size, vk::MemoryMapFlags(), &UBmotionBlur.data));

	auto const allocateInfo = vk::DescriptorSetAllocateInfo()
		.setDescriptorPool(vulkan->descriptorPool)
		.setDescriptorSetCount(1)
		.setPSetLayouts(&DSLayoutMotionBlur);
	VkCheck(vulkan->device.allocateDescriptorSets(&allocateInfo, &DSMotionBlur));

	vk::WriteDescriptorSet textureWriteSets[3];
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
			.setSampler(renderTargets["position"].sampler)				// Sampler sampler;
			.setImageView(renderTargets["position"].view)				// ImageView imageView;
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
			.setRange(3 * 64));									// DeviceSize range;

	vulkan->device.updateDescriptorSets(3, textureWriteSets, 0, nullptr);
	std::cout << "DescriptorSet allocated and updated\n";
}

void vm::MotionBlur::updateDescriptorSets(std::map<std::string, Image>& renderTargets)
{
	vk::WriteDescriptorSet textureWriteSets[3];
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
			.setSampler(renderTargets["position"].sampler)				// Sampler sampler;
			.setImageView(renderTargets["position"].view)				// ImageView imageView;
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
			.setRange(3 * 64));									// DeviceSize range;

	vulkan->device.updateDescriptorSets(3, textureWriteSets, 0, nullptr);
	std::cout << "DescriptorSet allocated and updated\n";
}

void vm::MotionBlur::draw(uint32_t imageIndex)
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
	vm::vec4 fps(1.f / Timer::delta);
	vulkan->dynamicCmdBuffer.pushConstants(pipeline.pipeinfo.layout, vk::ShaderStageFlagBits::eFragment, 0, sizeof(vm::vec4), &fps);
	vulkan->dynamicCmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline.pipeline);
	vulkan->dynamicCmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline.pipeinfo.layout, 0, 1, &DSMotionBlur, 0, nullptr);
	vulkan->dynamicCmdBuffer.draw(3, 1, 0, 0);
	vulkan->dynamicCmdBuffer.endRenderPass();
}

void vm::MotionBlur::destroy()
{
	for (auto &frameBuffer : frameBuffers) {
		if (frameBuffer) {
			vulkan->device.destroyFramebuffer(frameBuffer);
			std::cout << "Frame Buffer destroyed\n";
		}
	}
	if (renderPass) {
		vulkan->device.destroyRenderPass(renderPass);
		renderPass = nullptr;
		std::cout << "RenderPass destroyed\n";
	}
	if (DSLayoutMotionBlur) {
		vulkan->device.destroyDescriptorSetLayout(DSLayoutMotionBlur);
		DSLayoutMotionBlur = nullptr;
		std::cout << "Descriptor Set Layout destroyed\n";
	}
	UBmotionBlur.destroy();
	pipeline.destroy();
}
