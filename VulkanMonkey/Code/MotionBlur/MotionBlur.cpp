#include "MotionBlur.h"
#include <deque>

using namespace vm;

void MotionBlur::createMotionBlurUniforms(std::map<std::string, Image>& renderTargets)
{
	UBmotionBlur.createBuffer(4 * sizeof(mat4), vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostCoherent);
	UBmotionBlur.data = vulkan->device.mapMemory(UBmotionBlur.memory, 0, UBmotionBlur.size);

	vk::DescriptorSetAllocateInfo allocateInfo;
	allocateInfo.descriptorPool = vulkan->descriptorPool;
	allocateInfo.descriptorSetCount = 1;
	allocateInfo.pSetLayouts = &DSLayoutMotionBlur;
	DSMotionBlur = vulkan->device.allocateDescriptorSets(allocateInfo).at(0);

	updateDescriptorSets(renderTargets);
}

void MotionBlur::updateDescriptorSets(std::map<std::string, Image>& renderTargets)
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
	if ((GUI::show_FXAA && !GUI::show_Bloom) || (!GUI::show_FXAA && GUI::show_Bloom))
		comp = "composition2";

	std::vector<vk::WriteDescriptorSet> textureWriteSets{
		wSetImage(DSMotionBlur, 0, renderTargets[comp]),
		wSetImage(DSMotionBlur, 1, renderTargets["depth"]),
		wSetImage(DSMotionBlur, 2, renderTargets["velocity"]),
		wSetBuffer(DSMotionBlur, 3, UBmotionBlur)
	};
	vulkan->device.updateDescriptorSets(textureWriteSets, nullptr);
}

void MotionBlur::draw(uint32_t imageIndex, const vec2 UVOffset[2])
{
	vk::ClearColorValue clearColor;
	memcpy(clearColor.float32, GUI::clearColor.data(), 4 * sizeof(float));

	std::vector<vk::ClearValue> clearValues = { clearColor };

	vk::RenderPassBeginInfo rpi;
	rpi.renderPass = renderPass;
	rpi.framebuffer = frameBuffers[imageIndex];
	rpi.renderArea = { { 0, 0 }, vulkan->surface->actualExtent };
	rpi.clearValueCount = static_cast<uint32_t>(clearValues.size());
	rpi.pClearValues = clearValues.data();
	vulkan->dynamicCmdBuffer.beginRenderPass(rpi, vk::SubpassContents::eInline);

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
