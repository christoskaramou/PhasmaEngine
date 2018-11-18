#include "../include/SSAO.h"
#include "../include/Errors.h"

void SSAO::createSSAOUniforms(std::map<std::string, Image>& renderTargets, vk::Device device, vk::PhysicalDevice gpu, vk::CommandPool commandPool, vk::Queue graphicsQueue, vk::DescriptorPool descriptorPool)
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
	UBssaoKernel.createBuffer(device, gpu, sizeof(vm::vec4) * 32, vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostCoherent);
	VkCheck(device.mapMemory(UBssaoKernel.memory, 0, UBssaoKernel.size, vk::MemoryMapFlags(), &UBssaoKernel.data));
	memcpy(UBssaoKernel.data, ssaoKernel.data(), UBssaoKernel.size);
	// noise image
	std::vector<vm::vec4> noise{};
	for (unsigned int i = 0; i < 16; i++)
		noise.push_back(vm::vec4(vm::rand(-1.f, 1.f), vm::rand(-1.f, 1.f), 0.f, 1.f));
	Buffer staging;
	void* data;
	uint64_t bufSize = sizeof(vm::vec4) * 16;
	staging.createBuffer(device, gpu, bufSize, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
	device.mapMemory(staging.memory, vk::DeviceSize(), staging.size, vk::MemoryMapFlags(), &data);
	memcpy(data, noise.data(), staging.size);
	device.unmapMemory(staging.memory);
	ssaoNoise.filter = vk::Filter::eNearest;
	ssaoNoise.minLod = 0.0f;
	ssaoNoise.maxLod = 0.0f;
	ssaoNoise.maxAnisotropy = 1.0f;
	ssaoNoise.format = vk::Format::eR16G16B16A16Sfloat;
	ssaoNoise.createImage(device, gpu, 4, 4, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled, vk::MemoryPropertyFlagBits::eDeviceLocal);
	ssaoNoise.transitionImageLayout(device, commandPool, graphicsQueue, vk::ImageLayout::ePreinitialized, vk::ImageLayout::eTransferDstOptimal);
	ssaoNoise.copyBufferToImage(device, commandPool, graphicsQueue, staging.buffer, 0, 0, 4, 4);
	ssaoNoise.transitionImageLayout(device, commandPool, graphicsQueue, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal);
	ssaoNoise.createImageView(device, vk::ImageAspectFlagBits::eColor);
	ssaoNoise.createSampler(device);
	staging.destroy(device);
	// pvm uniform
	UBssaoPVM.createBuffer(device, gpu, 2 * sizeof(vm::mat4), vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostCoherent);
	VkCheck(device.mapMemory(UBssaoPVM.memory, 0, UBssaoPVM.size, vk::MemoryMapFlags(), &UBssaoPVM.data));

	vk::DescriptorSetAllocateInfo allocInfoSSAO = vk::DescriptorSetAllocateInfo{
		descriptorPool,						//DescriptorPool descriptorPool;
		1,										//uint32_t descriptorSetCount;
		&DSLayoutSSAO						//const DescriptorSetLayout* pSetLayouts;
	};
	VkCheck(device.allocateDescriptorSets(&allocInfoSSAO, &DSssao));

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
	device.updateDescriptorSets(static_cast<uint32_t>(writeDescriptorSetsSSAO.size()), writeDescriptorSetsSSAO.data(), 0, nullptr);
	std::cout << "DescriptorSet allocated and updated\n";

	// DESCRIPTOR SET FOR SSAO BLUR
	vk::DescriptorSetAllocateInfo allocInfoSSAOBlur = vk::DescriptorSetAllocateInfo{
		descriptorPool,						//DescriptorPool descriptorPool;
		1,										//uint32_t descriptorSetCount;
		&DSLayoutSSAOBlur					//const DescriptorSetLayout* pSetLayouts;
	};

	VkCheck(device.allocateDescriptorSets(&allocInfoSSAOBlur, &DSssaoBlur));

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
	device.updateDescriptorSets(static_cast<uint32_t>(writeDescriptorSetsSSAOBlur.size()), writeDescriptorSetsSSAOBlur.data(), 0, nullptr);
	std::cout << "DescriptorSet allocated and updated\n";
}
