#include "Deferred.h"
#include <deque>

using namespace vm;

void Deferred::createDeferredUniforms(std::map<std::string, Image>& renderTargets, LightUniforms& lightUniforms)
{
	vk::DescriptorSetAllocateInfo allocInfo = vk::DescriptorSetAllocateInfo{
		vulkan->descriptorPool,					//DescriptorPool descriptorPool;
		1,										//uint32_t descriptorSetCount;
		&DSLayoutComposition					//const DescriptorSetLayout* pSetLayouts;
	};
	DSComposition = vulkan->device.allocateDescriptorSets(allocInfo).at(0);

	updateDescriptorSets(renderTargets, lightUniforms);
}

void Deferred::updateDescriptorSets(std::map<std::string, Image>& renderTargets, LightUniforms& lightUniforms)
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

	std::vector<vk::WriteDescriptorSet> writeDescriptorSets = {
		wSetImage(DSComposition, 0, renderTargets["depth"]),
		wSetImage(DSComposition, 1, renderTargets["normal"]),
		wSetImage(DSComposition, 2, renderTargets["albedo"]),
		wSetImage(DSComposition, 3, renderTargets["srm"]),
		wSetBuffer(DSComposition, 4, lightUniforms.uniform),
		wSetImage(DSComposition, 5, renderTargets["ssaoBlur"]),
		wSetImage(DSComposition, 6, renderTargets["ssr"]),
		wSetImage(DSComposition, 7, renderTargets["emissive"])
	};

	vulkan->device.updateDescriptorSets(writeDescriptorSets, nullptr);
}

void Deferred::draw(uint32_t imageIndex, Shadows& shadows, SkyBox& skybox, mat4& invViewProj, vec2 UVOffset[2])
{
	// Begin Composition
	std::vector<vk::ClearValue> clearValues0 = {
		vk::ClearColorValue().setFloat32(GUI::clearColor),
		vk::ClearColorValue().setFloat32(GUI::clearColor) };

	auto renderPassInfo0 = vk::RenderPassBeginInfo()
		.setRenderPass(compositionRenderPass)
		.setFramebuffer(compositionFrameBuffers[imageIndex])
		.setRenderArea({ { 0, 0 }, vulkan->surface->actualExtent })
		.setClearValueCount(static_cast<uint32_t>(clearValues0.size()))
		.setPClearValues(clearValues0.data());
	vulkan->dynamicCmdBuffer.beginRenderPass(renderPassInfo0, vk::SubpassContents::eInline);

	vec4 screenSpace[7];
	screenSpace[0] = { GUI::show_ssao ? 1.f : 0.f, GUI::show_ssr ? 1.f : 0.f, GUI::show_tonemapping ? 1.f : 0.f, GUI::show_FXAA ? 1.f : 0.f };
	screenSpace[1] = { UVOffset[0].x, UVOffset[0].y, UVOffset[1].x, UVOffset[1].y };
	screenSpace[2] = { invViewProj[0] };
	screenSpace[3] = { invViewProj[1] };
	screenSpace[4] = { invViewProj[2] };
	screenSpace[5] = { invViewProj[3] };
	screenSpace[6] = { GUI::exposure, GUI::lights_intensity, GUI::lights_range, 0.f };

	vulkan->dynamicCmdBuffer.pushConstants(pipelineComposition.pipeinfo.layout, vk::ShaderStageFlagBits::eFragment, 0, sizeof(screenSpace), &screenSpace);
	vulkan->dynamicCmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelineComposition.pipeline);
	vulkan->dynamicCmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineComposition.pipeinfo.layout, 0, { DSComposition, shadows.descriptorSets[0], shadows.descriptorSets[1], shadows.descriptorSets[2], skybox.descriptorSet }, nullptr);
	vulkan->dynamicCmdBuffer.draw(3, 1, 0, 0);
	vulkan->dynamicCmdBuffer.endRenderPass();
	// End Composition
}

void Deferred::destroy()
{
	if (renderPass) {
		vulkan->device.destroyRenderPass(renderPass);
		renderPass = nullptr;
	}
	if (compositionRenderPass) {
		vulkan->device.destroyRenderPass(compositionRenderPass);
		compositionRenderPass = nullptr;
	}
	for (auto &frameBuffer : frameBuffers) {
		if (frameBuffer) {
			vulkan->device.destroyFramebuffer(frameBuffer);
		}
	}
	for (auto &frameBuffer : compositionFrameBuffers) {
		if (frameBuffer) {
			vulkan->device.destroyFramebuffer(frameBuffer);
		}
	}
	if (DSLayoutComposition) {
		vulkan->device.destroyDescriptorSetLayout(DSLayoutComposition);
		DSLayoutComposition = nullptr;
	}
	pipeline.destroy();
	pipelineComposition.destroy();
}
