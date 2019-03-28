#include "SSR.h"
#include <deque>

using namespace vm;

void SSR::createSSRUniforms(std::map<std::string, Image>& renderTargets)
{
	UBReflection.createBuffer(4 * sizeof(mat4), vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostCoherent);
	UBReflection.data = vulkan->device.mapMemory(UBReflection.memory, 0, UBReflection.size);

	auto const allocateInfo2 = vk::DescriptorSetAllocateInfo()
		.setDescriptorPool(vulkan->descriptorPool)
		.setDescriptorSetCount(1)
		.setPSetLayouts(&DSLayoutReflection);
	DSReflection = vulkan->device.allocateDescriptorSets(allocateInfo2).at(0);

	updateDescriptorSets(renderTargets);
}

void SSR::updateDescriptorSets(std::map<std::string, Image>& renderTargets)
{
	std::deque<vk::DescriptorImageInfo> dsii{};
	auto wSetImage = [&dsii](vk::DescriptorSet& dstSet, uint32_t dstBinding, Image& image) {
		dsii.push_back({ image.sampler, image.view, vk::ImageLayout::eColorAttachmentOptimal });
		return vk::WriteDescriptorSet{ dstSet, dstBinding, 0, 1, vk::DescriptorType::eCombinedImageSampler, &dsii.back(), nullptr, nullptr };
	};
	std::deque<vk::DescriptorBufferInfo> dsbi{};
	auto wSetBuffer = [&dsbi](vk::DescriptorSet& dstSet, uint32_t dstBinding, Buffer& buffer) {
		dsbi.push_back({ buffer.buffer, 0, buffer.size });
		return vk::WriteDescriptorSet{ dstSet, dstBinding, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &dsbi.back(), nullptr };
	};

	std::vector<vk::WriteDescriptorSet> textureWriteSets{
		wSetImage(DSReflection, 0, renderTargets["albedo"]),
		wSetImage(DSReflection, 1, renderTargets["depth"]),
		wSetImage(DSReflection, 2, renderTargets["normal"]),
		wSetImage(DSReflection, 3, renderTargets["srm"]),
		wSetBuffer(DSReflection, 4, UBReflection)
	};
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

void SSR::update(Camera& camera)
{
	if (GUI::show_ssr) {
		mat4 reflectionInput[4];
		reflectionInput[0][0] = vec4(camera.position, 1.0f);
		reflectionInput[0][1] = vec4(camera.front, 1.0f);
		reflectionInput[0][2] = vec4(WIDTH_f, HEIGHT_f, 0.f, 0.f);
		reflectionInput[0][3] = vec4();
		reflectionInput[1] = camera.projection;
		reflectionInput[2] = camera.view;
		reflectionInput[3] = camera.invProjection;
		memcpy(UBReflection.data, &reflectionInput, sizeof(reflectionInput));
	}
}
