#include "../include/SSAO.h"
#include "../include/Errors.h"

using namespace vm;

void SSAO::createSSAOUniforms(std::map<std::string, Image>& renderTargets)
{
	// kernel buffer
	std::vector<vm::vec4> ssaoKernel{};
	for (unsigned i = 0; i < 32; i++) {
		vm::vec3 sample(vm::rand(-1.f, 1.f), vm::rand(-1.f, 1.f), vm::rand(0.f, 1.f));
		sample = vm::normalize(sample);
		sample *= vm::rand(0.f, 1.f);
		float scale = float(i) / 32.f;
		scale = vm::lerp(.1f, 1.f, scale * scale);
		ssaoKernel.push_back(vm::vec4(sample * scale, 0.f));
	}
	UBssaoKernel.createBuffer(sizeof(vm::vec4) * 32, vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostCoherent);
	VkCheck(vulkan->device.mapMemory(UBssaoKernel.memory, 0, UBssaoKernel.size, vk::MemoryMapFlags(), &UBssaoKernel.data));
	memcpy(UBssaoKernel.data, ssaoKernel.data(), UBssaoKernel.size);
	// noise image
	std::vector<vm::vec4> noise{};
	for (unsigned int i = 0; i < 16; i++)
		noise.push_back(vm::vec4(vm::rand(-1.f, 1.f), vm::rand(-1.f, 1.f), 0.f, 1.f));
	Buffer staging;
	void* data;
	uint64_t bufSize = sizeof(vm::vec4) * 16;
	staging.createBuffer(bufSize, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
	vulkan->device.mapMemory(staging.memory, vk::DeviceSize(), staging.size, vk::MemoryMapFlags(), &data);
	memcpy(data, noise.data(), staging.size);
	vulkan->device.unmapMemory(staging.memory);
	ssaoNoise.filter = vk::Filter::eNearest;
	ssaoNoise.minLod = 0.0f;
	ssaoNoise.maxLod = 0.0f;
	ssaoNoise.maxAnisotropy = 1.0f;
	ssaoNoise.format = vk::Format::eR16G16B16A16Sfloat;
	ssaoNoise.createImage(4, 4, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled, vk::MemoryPropertyFlagBits::eDeviceLocal);
	ssaoNoise.transitionImageLayout(vk::ImageLayout::ePreinitialized, vk::ImageLayout::eTransferDstOptimal);
	ssaoNoise.copyBufferToImage(staging.buffer, 0, 0, 4, 4);
	ssaoNoise.transitionImageLayout(vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal);
	ssaoNoise.createImageView(vk::ImageAspectFlagBits::eColor);
	ssaoNoise.createSampler();
	staging.destroy();
	// pvm uniform
	UBssaoPVM.createBuffer(2 * sizeof(vm::mat4), vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostCoherent);
	VkCheck(vulkan->device.mapMemory(UBssaoPVM.memory, 0, UBssaoPVM.size, vk::MemoryMapFlags(), &UBssaoPVM.data));

	vk::DescriptorSetAllocateInfo allocInfoSSAO = vk::DescriptorSetAllocateInfo{
		vulkan->descriptorPool,						//DescriptorPool descriptorPool;
		1,										//uint32_t descriptorSetCount;
		&DSLayoutSSAO						//const DescriptorSetLayout* pSetLayouts;
	};
	VkCheck(vulkan->device.allocateDescriptorSets(&allocInfoSSAO, &DSssao));

	vk::DescriptorImageInfo texDescriptorPosition = vk::DescriptorImageInfo{
		renderTargets["position"].sampler,		//Sampler sampler;
		renderTargets["position"].view,			//ImageView imageView;
		vk::ImageLayout::eColorAttachmentOptimal	//ImageLayout imageLayout;
	};
	vk::DescriptorImageInfo texDescriptorNormal = vk::DescriptorImageInfo{
		renderTargets["normal"].sampler,			//Sampler sampler;
		renderTargets["normal"].view,			//ImageView imageView;
		vk::ImageLayout::eColorAttachmentOptimal	//ImageLayout imageLayout;
	};
	vk::DescriptorImageInfo texDescriptorSSAO = vk::DescriptorImageInfo{
		renderTargets["ssao"].sampler,			//Sampler sampler;
		renderTargets["ssao"].view,				//ImageView imageView;
		vk::ImageLayout::eColorAttachmentOptimal	//ImageLayout imageLayout;
	};
	std::vector<vk::WriteDescriptorSet> writeDescriptorSetsSSAO = {
		// Binding 0: Position texture target
		vk::WriteDescriptorSet{
			DSssao,								//DescriptorSet dstSet;
			0,										//uint32_t dstBinding;
			0,										//uint32_t dstArrayElement;
			1,										//uint32_t descriptorCount_;
			vk::DescriptorType::eCombinedImageSampler,//DescriptorType descriptorType;
			&texDescriptorPosition,					//const DescriptorImageInfo* pImageInfo;
			nullptr,								//const DescriptorBufferInfo* pBufferInfo;
			nullptr									//const BufferView* pTexelBufferView;
		},
		// Binding 1: Normals texture target
		vk::WriteDescriptorSet{
			DSssao,								//DescriptorSet dstSet;
			1,										//uint32_t dstBinding;
			0,										//uint32_t dstArrayElement;
			1,										//uint32_t descriptorCount_;
			vk::DescriptorType::eCombinedImageSampler,//DescriptorType descriptorType;
			&texDescriptorNormal,					//const DescriptorImageInfo* pImageInfo;
			nullptr,								//const DescriptorBufferInfo* pBufferInfo;
			nullptr									//const BufferView* pTexelBufferView;
		},
		// Binding 2: SSAO Noise Image
		vk::WriteDescriptorSet{
			DSssao,								//DescriptorSet dstSet;
			2,										//uint32_t dstBinding;
			0,										//uint32_t dstArrayElement;
			1,										//uint32_t descriptorCount_;
			vk::DescriptorType::eCombinedImageSampler,//DescriptorType descriptorType;
			&vk::DescriptorImageInfo()				//const DescriptorImageInfo* pImageInfo;
				.setSampler(ssaoNoise.sampler)
				.setImageView(ssaoNoise.view)
				.setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal),
			nullptr,								//const DescriptorBufferInfo* pBufferInfo;
			nullptr									//const BufferView* pTexelBufferView;
		},
		// Binding 3: SSAO Kernel
		vk::WriteDescriptorSet{
			DSssao,								//DescriptorSet dstSet;
			3,										//uint32_t dstBinding;
			0,										//uint32_t dstArrayElement;
			1,										//uint32_t descriptorCount_;
			vk::DescriptorType::eUniformBuffer,		//DescriptorType descriptorType;
			nullptr,								//const DescriptorImageInfo* pImageInfo;
			&vk::DescriptorBufferInfo()				//const DescriptorBufferInfo* pBufferInfo;
				.setBuffer(UBssaoKernel.buffer)		// Buffer buffer;
				.setOffset(0)							// DeviceSize offset;
				.setRange(UBssaoKernel.size),		// DeviceSize range;
			nullptr									//const BufferView* pTexelBufferView;
		},
		// Binding 4: Projection View Size
		vk::WriteDescriptorSet{
			DSssao,								//DescriptorSet dstSet;
			4,										//uint32_t dstBinding;
			0,										//uint32_t dstArrayElement;
			1,										//uint32_t descriptorCount_;
			vk::DescriptorType::eUniformBuffer,		//DescriptorType descriptorType;
			nullptr,								//const DescriptorImageInfo* pImageInfo;
			&vk::DescriptorBufferInfo()				//const DescriptorBufferInfo* pBufferInfo;
				.setBuffer(UBssaoPVM.buffer)		// Buffer buffer;
				.setOffset(0)							// DeviceSize offset;
				.setRange(UBssaoPVM.size),			// DeviceSize range;
			nullptr									//const BufferView* pTexelBufferView;
		}
	};
	vulkan->device.updateDescriptorSets(static_cast<uint32_t>(writeDescriptorSetsSSAO.size()), writeDescriptorSetsSSAO.data(), 0, nullptr);
	std::cout << "DescriptorSet allocated and updated\n";

	// DESCRIPTOR SET FOR SSAO BLUR
	vk::DescriptorSetAllocateInfo allocInfoSSAOBlur = vk::DescriptorSetAllocateInfo{
		vulkan->descriptorPool,						//DescriptorPool descriptorPool;
		1,										//uint32_t descriptorSetCount;
		&DSLayoutSSAOBlur					//const DescriptorSetLayout* pSetLayouts;
	};

	VkCheck(vulkan->device.allocateDescriptorSets(&allocInfoSSAOBlur, &DSssaoBlur));

	std::vector<vk::WriteDescriptorSet> writeDescriptorSetsSSAOBlur = {
		// Binding 0: Position texture target
		vk::WriteDescriptorSet{
			DSssaoBlur,							//DescriptorSet dstSet;
			0,										//uint32_t dstBinding;
			0,										//uint32_t dstArrayElement;
			1,										//uint32_t descriptorCount_;
			vk::DescriptorType::eCombinedImageSampler,//DescriptorType descriptorType;
			&texDescriptorSSAO,					//const DescriptorImageInfo* pImageInfo;
			nullptr,								//const DescriptorBufferInfo* pBufferInfo;
			nullptr									//const BufferView* pTexelBufferView;
		}
	};
	vulkan->device.updateDescriptorSets(static_cast<uint32_t>(writeDescriptorSetsSSAOBlur.size()), writeDescriptorSetsSSAOBlur.data(), 0, nullptr);
	std::cout << "DescriptorSet allocated and updated\n";
}

void vm::SSAO::updateDescriptorSets(std::map<std::string, Image>& renderTargets)
{
	vk::DescriptorImageInfo texDescriptorPosition = vk::DescriptorImageInfo{
		renderTargets["position"].sampler,		//Sampler sampler;
		renderTargets["position"].view,			//ImageView imageView;
		vk::ImageLayout::eColorAttachmentOptimal	//ImageLayout imageLayout;
	};
	vk::DescriptorImageInfo texDescriptorNormal = vk::DescriptorImageInfo{
		renderTargets["normal"].sampler,			//Sampler sampler;
		renderTargets["normal"].view,			//ImageView imageView;
		vk::ImageLayout::eColorAttachmentOptimal	//ImageLayout imageLayout;
	};
	vk::DescriptorImageInfo texDescriptorSSAO = vk::DescriptorImageInfo{
		renderTargets["ssao"].sampler,			//Sampler sampler;
		renderTargets["ssao"].view,				//ImageView imageView;
		vk::ImageLayout::eColorAttachmentOptimal	//ImageLayout imageLayout;
	};
	std::vector<vk::WriteDescriptorSet> writeDescriptorSetsSSAO = {
		// Binding 0: Position texture target
		vk::WriteDescriptorSet{
			DSssao,								//DescriptorSet dstSet;
			0,										//uint32_t dstBinding;
			0,										//uint32_t dstArrayElement;
			1,										//uint32_t descriptorCount_;
			vk::DescriptorType::eCombinedImageSampler,//DescriptorType descriptorType;
			&texDescriptorPosition,					//const DescriptorImageInfo* pImageInfo;
			nullptr,								//const DescriptorBufferInfo* pBufferInfo;
			nullptr									//const BufferView* pTexelBufferView;
		},
		// Binding 1: Normals texture target
		vk::WriteDescriptorSet{
			DSssao,								//DescriptorSet dstSet;
			1,										//uint32_t dstBinding;
			0,										//uint32_t dstArrayElement;
			1,										//uint32_t descriptorCount_;
			vk::DescriptorType::eCombinedImageSampler,//DescriptorType descriptorType;
			&texDescriptorNormal,					//const DescriptorImageInfo* pImageInfo;
			nullptr,								//const DescriptorBufferInfo* pBufferInfo;
			nullptr									//const BufferView* pTexelBufferView;
		},
		// Binding 2: SSAO Noise Image
		vk::WriteDescriptorSet{
			DSssao,								//DescriptorSet dstSet;
			2,										//uint32_t dstBinding;
			0,										//uint32_t dstArrayElement;
			1,										//uint32_t descriptorCount_;
			vk::DescriptorType::eCombinedImageSampler,//DescriptorType descriptorType;
			&vk::DescriptorImageInfo()				//const DescriptorImageInfo* pImageInfo;
				.setSampler(ssaoNoise.sampler)
				.setImageView(ssaoNoise.view)
				.setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal),
			nullptr,								//const DescriptorBufferInfo* pBufferInfo;
			nullptr									//const BufferView* pTexelBufferView;
		},
		// Binding 3: SSAO Kernel
		vk::WriteDescriptorSet{
			DSssao,								//DescriptorSet dstSet;
			3,										//uint32_t dstBinding;
			0,										//uint32_t dstArrayElement;
			1,										//uint32_t descriptorCount_;
			vk::DescriptorType::eUniformBuffer,		//DescriptorType descriptorType;
			nullptr,								//const DescriptorImageInfo* pImageInfo;
			&vk::DescriptorBufferInfo()				//const DescriptorBufferInfo* pBufferInfo;
				.setBuffer(UBssaoKernel.buffer)		// Buffer buffer;
				.setOffset(0)							// DeviceSize offset;
				.setRange(UBssaoKernel.size),		// DeviceSize range;
			nullptr									//const BufferView* pTexelBufferView;
		},
		// Binding 4: Projection View Size
		vk::WriteDescriptorSet{
			DSssao,								//DescriptorSet dstSet;
			4,										//uint32_t dstBinding;
			0,										//uint32_t dstArrayElement;
			1,										//uint32_t descriptorCount_;
			vk::DescriptorType::eUniformBuffer,		//DescriptorType descriptorType;
			nullptr,								//const DescriptorImageInfo* pImageInfo;
			&vk::DescriptorBufferInfo()				//const DescriptorBufferInfo* pBufferInfo;
				.setBuffer(UBssaoPVM.buffer)		// Buffer buffer;
				.setOffset(0)							// DeviceSize offset;
				.setRange(UBssaoPVM.size),			// DeviceSize range;
			nullptr									//const BufferView* pTexelBufferView;
		}
	};
	vulkan->device.updateDescriptorSets(static_cast<uint32_t>(writeDescriptorSetsSSAO.size()), writeDescriptorSetsSSAO.data(), 0, nullptr);
	std::cout << "DescriptorSet updated\n";


	std::vector<vk::WriteDescriptorSet> writeDescriptorSetsSSAOBlur = {
		// Binding 0: Position texture target
		vk::WriteDescriptorSet{
			DSssaoBlur,							//DescriptorSet dstSet;
			0,										//uint32_t dstBinding;
			0,										//uint32_t dstArrayElement;
			1,										//uint32_t descriptorCount_;
			vk::DescriptorType::eCombinedImageSampler,//DescriptorType descriptorType;
			&texDescriptorSSAO,					//const DescriptorImageInfo* pImageInfo;
			nullptr,								//const DescriptorBufferInfo* pBufferInfo;
			nullptr									//const BufferView* pTexelBufferView;
		}
	};
	vulkan->device.updateDescriptorSets(static_cast<uint32_t>(writeDescriptorSetsSSAOBlur.size()), writeDescriptorSetsSSAOBlur.data(), 0, nullptr);
	std::cout << "DescriptorSet updated\n";
}

void SSAO::draw(uint32_t imageIndex, vk::Extent2D actualExtent, const vm::vec2 UVOffset[2])
{
	// SSAO image
	std::vector<vk::ClearValue> clearValuesSSAO = {
		vk::ClearColorValue().setFloat32({0.f, 0.f, 0.f, 0.f})
	};
	auto renderPassInfoSSAO = vk::RenderPassBeginInfo()
		.setRenderPass(renderPass)
		.setFramebuffer(frameBuffers[imageIndex])
		.setRenderArea({ { 0, 0 },actualExtent })
		.setClearValueCount(static_cast<uint32_t>(clearValuesSSAO.size()))
		.setPClearValues(clearValuesSSAO.data());
	vulkan->dynamicCmdBuffer.beginRenderPass(&renderPassInfoSSAO, vk::SubpassContents::eInline);
	vulkan->dynamicCmdBuffer.pushConstants(pipeline.pipeinfo.layout, vk::ShaderStageFlagBits::eFragment, 0, 2 * sizeof(vm::vec2), UVOffset);
	vulkan->dynamicCmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline.pipeline);
	const vk::DescriptorSet descriptorSets[] = { DSssao };
	const uint32_t dOffsets[] = { 0 };
	vulkan->dynamicCmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline.pipeinfo.layout, 0, 1, descriptorSets, 0, dOffsets);
	vulkan->dynamicCmdBuffer.draw(3, 1, 0, 0);
	vulkan->dynamicCmdBuffer.endRenderPass();

	// new blurry SSAO image
	std::vector<vk::ClearValue> clearValuesSSAOBlur = {
	vk::ClearColorValue().setFloat32({0.f, 0.f, 0.f, 0.f}) };
	auto renderPassInfoSSAOBlur = vk::RenderPassBeginInfo()
		.setRenderPass(blurRenderPass)
		.setFramebuffer(blurFrameBuffers[imageIndex])
		.setRenderArea({ { 0, 0 }, actualExtent })
		.setClearValueCount(static_cast<uint32_t>(clearValuesSSAOBlur.size()))
		.setPClearValues(clearValuesSSAOBlur.data());
	vulkan->dynamicCmdBuffer.beginRenderPass(&renderPassInfoSSAOBlur, vk::SubpassContents::eInline);
	vulkan->dynamicCmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelineBlur.pipeline);
	const vk::DescriptorSet descriptorSetsBlur[] = { DSssaoBlur };
	const uint32_t dOffsetsBlur[] = { 0 };
	vulkan->dynamicCmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineBlur.pipeinfo.layout, 0, 1, descriptorSetsBlur, 0, dOffsetsBlur);
	vulkan->dynamicCmdBuffer.draw(3, 1, 0, 0);
	vulkan->dynamicCmdBuffer.endRenderPass();
}

void vm::SSAO::destroy()
{
	UBssaoKernel.destroy();
	UBssaoPVM.destroy();
	ssaoNoise.destroy();
	if (renderPass) {
		vulkan->device.destroyRenderPass(renderPass);
		renderPass = nullptr;
		std::cout << "RenderPass destroyed\n";
	}
	if (blurRenderPass) {
		vulkan->device.destroyRenderPass(blurRenderPass);
		blurRenderPass = nullptr;
		std::cout << "RenderPass destroyed\n";
	}
	for (auto &frameBuffer : frameBuffers) {
		if (frameBuffer) {
			vulkan->device.destroyFramebuffer(frameBuffer);
			std::cout << "Frame Buffer destroyed\n";
		}
	}
	for (auto &frameBuffer : blurFrameBuffers) {
		if (frameBuffer) {
			vulkan->device.destroyFramebuffer(frameBuffer);
			std::cout << "Frame Buffer destroyed\n";
		}
	}
	pipeline.destroy();
	pipelineBlur.destroy();
	if (DSLayoutSSAO) {
		vulkan->device.destroyDescriptorSetLayout(DSLayoutSSAO);
		DSLayoutSSAO = nullptr;
		std::cout << "Descriptor Set Layout destroyed\n";
	}
	if (DSLayoutSSAOBlur) {
		vulkan->device.destroyDescriptorSetLayout(DSLayoutSSAOBlur);
		DSLayoutSSAOBlur = nullptr;
		std::cout << "Descriptor Set Layout destroyed\n";
	}
}
