#include "SSAO.h"
#include <deque>

using namespace vm;

void SSAO::createUniforms(std::map<std::string, Image>& renderTargets)
{
	// kernel buffer
	std::vector<vec4> kernel{};
	for (unsigned i = 0; i < 32; i++) {
		vec3 sample(rand(-1.f, 1.f), rand(-1.f, 1.f), rand(0.f, 1.f));
		sample = normalize(sample);
		sample *= rand(0.f, 1.f);
		float scale = float(i) / 32.f;
		scale = lerp(.1f, 1.f, scale * scale);
		kernel.push_back(vec4(sample * scale, 0.f));
	}
	UB_Kernel.createBuffer(sizeof(vec4) * 32, vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostCoherent);
	UB_Kernel.data = vulkan->device.mapMemory(UB_Kernel.memory, 0, UB_Kernel.size);
	memcpy(UB_Kernel.data, kernel.data(), UB_Kernel.size);
	// noise image
	std::vector<vec4> noise{};
	for (unsigned int i = 0; i < 16; i++)
		noise.push_back(vec4(rand(-1.f, 1.f), rand(-1.f, 1.f), 0.f, 1.f));

	Buffer staging;
	uint64_t bufSize = sizeof(vec4) * 16;
	staging.createBuffer(bufSize, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
	void* data = vulkan->device.mapMemory(staging.memory, vk::DeviceSize(), staging.size);
	memcpy(data, noise.data(), staging.size);
	vulkan->device.unmapMemory(staging.memory);

	noiseTex.filter = vk::Filter::eNearest;
	noiseTex.minLod = 0.0f;
	noiseTex.maxLod = 0.0f;
	noiseTex.maxAnisotropy = 1.0f;
	noiseTex.format = vk::Format::eR16G16B16A16Sfloat;
	noiseTex.createImage(4, 4, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled, vk::MemoryPropertyFlagBits::eDeviceLocal);
	noiseTex.transitionImageLayout(vk::ImageLayout::ePreinitialized, vk::ImageLayout::eTransferDstOptimal);
	noiseTex.copyBufferToImage(staging.buffer, 0, 0, 4, 4);
	noiseTex.transitionImageLayout(vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal);
	noiseTex.createImageView(vk::ImageAspectFlagBits::eColor);
	noiseTex.createSampler();
	staging.destroy();
	// pvm uniform
	UB_PVM.createBuffer(3 * sizeof(mat4), vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostCoherent);
	UB_PVM.data = vulkan->device.mapMemory(UB_PVM.memory, 0, UB_PVM.size);

	// DESCRIPTOR SET FOR SSAO
	vk::DescriptorSetAllocateInfo allocInfo = vk::DescriptorSetAllocateInfo{
		vulkan->descriptorPool,					//DescriptorPool descriptorPool;
		1,										//uint32_t descriptorSetCount;
		&DSLayout								//const DescriptorSetLayout* pSetLayouts;
	};
	DSet = vulkan->device.allocateDescriptorSets(allocInfo).at(0);

	// DESCRIPTOR SET FOR SSAO BLUR
	vk::DescriptorSetAllocateInfo allocInfoBlur = vk::DescriptorSetAllocateInfo{
		vulkan->descriptorPool,					//DescriptorPool descriptorPool;
		1,										//uint32_t descriptorSetCount;
		&DSLayoutBlur							//const DescriptorSetLayout* pSetLayouts;
	};
	DSBlur = vulkan->device.allocateDescriptorSets(allocInfoBlur).at(0);

	updateDescriptorSets(renderTargets);
}

void SSAO::updateDescriptorSets(std::map<std::string, Image>& renderTargets)
{
	std::deque<vk::DescriptorImageInfo> dsii{};
	auto wSetImage = [&dsii](vk::DescriptorSet& dstSet, uint32_t dstBinding, Image& image, vk::ImageLayout imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal) {
		dsii.push_back({ image.sampler, image.view, imageLayout });
		return vk::WriteDescriptorSet{ dstSet, dstBinding, 0, 1, vk::DescriptorType::eCombinedImageSampler, &dsii.back(), nullptr, nullptr };
	};
	std::deque<vk::DescriptorBufferInfo> dsbi{};
	auto wSetBuffer = [&dsbi](vk::DescriptorSet& dstSet, uint32_t dstBinding, Buffer& buffer) {
		dsbi.push_back({ buffer.buffer, 0, buffer.size });
		return vk::WriteDescriptorSet{ dstSet, dstBinding, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &dsbi.back(), nullptr };
	};

	std::vector<vk::WriteDescriptorSet> writeDescriptorSets{
		wSetImage(DSet, 0, renderTargets["depth"]),
		wSetImage(DSet, 1, renderTargets["normal"]),
		wSetImage(DSet, 2, noiseTex),
		wSetBuffer(DSet, 3, UB_Kernel),
		wSetBuffer(DSet, 4, UB_PVM),
		wSetImage(DSBlur, 0, renderTargets["ssao"])
	};
	vulkan->device.updateDescriptorSets(writeDescriptorSets, nullptr);
}

void SSAO::draw(uint32_t imageIndex, const vec2 UVOffset[2])
{
	// SSAO image
	vk::ClearColorValue clearColor;
	memcpy(clearColor.float32, GUI::clearColor.data(), 4 * sizeof(float));

	std::vector<vk::ClearValue> clearValues = { clearColor };

	vk::RenderPassBeginInfo rpi;
	rpi.renderPass = renderPass;
	rpi.framebuffer = frameBuffers[imageIndex];
	rpi.renderArea = { { 0, 0 }, vulkan->surface->actualExtent };
	rpi.clearValueCount = 1;
	rpi.pClearValues = clearValues.data();

	vulkan->dynamicCmdBuffer.beginRenderPass(rpi, vk::SubpassContents::eInline);
	vulkan->dynamicCmdBuffer.pushConstants(pipeline.pipeinfo.layout, vk::ShaderStageFlagBits::eFragment, 0, 2 * sizeof(vec2), UVOffset);
	vulkan->dynamicCmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline.pipeline);
	const vk::DescriptorSet descriptorSets = { DSet };
	vulkan->dynamicCmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline.pipeinfo.layout, 0, descriptorSets, nullptr);
	vulkan->dynamicCmdBuffer.draw(3, 1, 0, 0);
	vulkan->dynamicCmdBuffer.endRenderPass();

	// new blurry SSAO image
	rpi.renderPass = blurRenderPass;
	rpi.framebuffer = blurFrameBuffers[imageIndex];

	vulkan->dynamicCmdBuffer.beginRenderPass(rpi, vk::SubpassContents::eInline);
	vulkan->dynamicCmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelineBlur.pipeline);
	const vk::DescriptorSet descriptorSetsBlur = { DSBlur };
	vulkan->dynamicCmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineBlur.pipeinfo.layout, 0, descriptorSetsBlur, nullptr);
	vulkan->dynamicCmdBuffer.draw(3, 1, 0, 0);
	vulkan->dynamicCmdBuffer.endRenderPass();
}

void SSAO::destroy()
{
	UB_Kernel.destroy();
	UB_PVM.destroy();
	noiseTex.destroy();
	if (renderPass) {
		vulkan->device.destroyRenderPass(renderPass);
		renderPass = nullptr;
	}
	if (blurRenderPass) {
		vulkan->device.destroyRenderPass(blurRenderPass);
		blurRenderPass = nullptr;
	}
	for (auto &frameBuffer : frameBuffers) {
		if (frameBuffer) {
			vulkan->device.destroyFramebuffer(frameBuffer);
		}
	}
	for (auto &frameBuffer : blurFrameBuffers) {
		if (frameBuffer) {
			vulkan->device.destroyFramebuffer(frameBuffer);
		}
	}
	pipeline.destroy();
	pipelineBlur.destroy();
	if (DSLayout) {
		vulkan->device.destroyDescriptorSetLayout(DSLayout);
		DSLayout = nullptr;
	}
	if (DSLayoutBlur) {
		vulkan->device.destroyDescriptorSetLayout(DSLayoutBlur);
		DSLayoutBlur = nullptr;
	}
}

void SSAO::update(Camera& camera)
{
	if (GUI::show_ssao) {
		mat4 pvm[3]{ camera.projection, camera.view, camera.invProjection };
		memcpy(UB_PVM.data, pvm, sizeof(pvm));
	}
}
