#include "SSDO.h"

using namespace vm;

void SSDO::createUniforms(std::map<std::string, Image>& renderTargets)
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

	vk::DescriptorSetAllocateInfo allocInfo = vk::DescriptorSetAllocateInfo{
		vulkan->descriptorPool,						//DescriptorPool descriptorPool;
		1,										//uint32_t descriptorSetCount;
		&DSLayout						//const DescriptorSetLayout* pSetLayouts;
	};
	DSet = vulkan->device.allocateDescriptorSets(allocInfo).at(0);

	vk::DescriptorImageInfo texDescriptorPosition = vk::DescriptorImageInfo{
		renderTargets["depth"].sampler,		//Sampler sampler;
		renderTargets["depth"].view,			//ImageView imageView;
		vk::ImageLayout::eColorAttachmentOptimal	//ImageLayout imageLayout;
	};
	vk::DescriptorImageInfo texDescriptorNormal = vk::DescriptorImageInfo{
		renderTargets["normal"].sampler,			//Sampler sampler;
		renderTargets["normal"].view,			//ImageView imageView;
		vk::ImageLayout::eColorAttachmentOptimal	//ImageLayout imageLayout;
	};
	vk::DescriptorImageInfo texDescriptor = vk::DescriptorImageInfo{
		renderTargets["ssdo"].sampler,			//Sampler sampler;
		renderTargets["ssdo"].view,				//ImageView imageView;
		vk::ImageLayout::eColorAttachmentOptimal	//ImageLayout imageLayout;
	};
	std::vector<vk::WriteDescriptorSet> writeDescriptorSets = {
		// Binding 0: Position texture target
		vk::WriteDescriptorSet{
			DSet,								//DescriptorSet dstSet;
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
			DSet,								//DescriptorSet dstSet;
			1,										//uint32_t dstBinding;
			0,										//uint32_t dstArrayElement;
			1,										//uint32_t descriptorCount_;
			vk::DescriptorType::eCombinedImageSampler,//DescriptorType descriptorType;
			&texDescriptorNormal,					//const DescriptorImageInfo* pImageInfo;
			nullptr,								//const DescriptorBufferInfo* pBufferInfo;
			nullptr									//const BufferView* pTexelBufferView;
		},
		// Binding 2: SSDO Noise Image
		vk::WriteDescriptorSet{
			DSet,								//DescriptorSet dstSet;
			2,										//uint32_t dstBinding;
			0,										//uint32_t dstArrayElement;
			1,										//uint32_t descriptorCount_;
			vk::DescriptorType::eCombinedImageSampler,//DescriptorType descriptorType;
			&vk::DescriptorImageInfo()				//const DescriptorImageInfo* pImageInfo;
				.setSampler(noiseTex.sampler)
				.setImageView(noiseTex.view)
				.setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal),
			nullptr,								//const DescriptorBufferInfo* pBufferInfo;
			nullptr									//const BufferView* pTexelBufferView;
		},
		// Binding 3: SSDO Kernel
		vk::WriteDescriptorSet{
			DSet,								//DescriptorSet dstSet;
			3,										//uint32_t dstBinding;
			0,										//uint32_t dstArrayElement;
			1,										//uint32_t descriptorCount_;
			vk::DescriptorType::eUniformBuffer,		//DescriptorType descriptorType;
			nullptr,								//const DescriptorImageInfo* pImageInfo;
			&vk::DescriptorBufferInfo()				//const DescriptorBufferInfo* pBufferInfo;
				.setBuffer(UB_Kernel.buffer)		// Buffer buffer;
				.setOffset(0)							// DeviceSize offset;
				.setRange(UB_Kernel.size),		// DeviceSize range;
			nullptr									//const BufferView* pTexelBufferView;
		},
		// Binding 4: Projection, View, InvProj
		vk::WriteDescriptorSet{
			DSet,								//DescriptorSet dstSet;
			4,										//uint32_t dstBinding;
			0,										//uint32_t dstArrayElement;
			1,										//uint32_t descriptorCount_;
			vk::DescriptorType::eUniformBuffer,		//DescriptorType descriptorType;
			nullptr,								//const DescriptorImageInfo* pImageInfo;
			&vk::DescriptorBufferInfo()				//const DescriptorBufferInfo* pBufferInfo;
				.setBuffer(UB_PVM.buffer)		// Buffer buffer;
				.setOffset(0)							// DeviceSize offset;
				.setRange(UB_PVM.size),			// DeviceSize range;
			nullptr									//const BufferView* pTexelBufferView;
		}
	};
	vulkan->device.updateDescriptorSets(writeDescriptorSets, nullptr);

	// DESCRIPTOR SET FOR SSDO BLUR
	vk::DescriptorSetAllocateInfo allocInfoBlur = vk::DescriptorSetAllocateInfo{
		vulkan->descriptorPool,						//DescriptorPool descriptorPool;
		1,										//uint32_t descriptorSetCount;
		&DSLayoutBlur					//const DescriptorSetLayout* pSetLayouts;
	};

	DSBlur = vulkan->device.allocateDescriptorSets(allocInfoBlur).at(0);

	std::vector<vk::WriteDescriptorSet> writeDescriptorSetsBlur = {
		// Binding 0: Position texture target
		vk::WriteDescriptorSet{
			DSBlur,							//DescriptorSet dstSet;
			0,										//uint32_t dstBinding;
			0,										//uint32_t dstArrayElement;
			1,										//uint32_t descriptorCount_;
			vk::DescriptorType::eCombinedImageSampler,//DescriptorType descriptorType;
			&texDescriptor,					//const DescriptorImageInfo* pImageInfo;
			nullptr,								//const DescriptorBufferInfo* pBufferInfo;
			nullptr									//const BufferView* pTexelBufferView;
		},
		// Binding 1: Normals texture
		vk::WriteDescriptorSet{
			DSBlur,									//DescriptorSet dstSet;
			1,										//uint32_t dstBinding;
			0,										//uint32_t dstArrayElement;
			1,										//uint32_t descriptorCount_;
			vk::DescriptorType::eCombinedImageSampler,//DescriptorType descriptorType;
			&texDescriptorNormal,					//const DescriptorImageInfo* pImageInfo;
			nullptr,								//const DescriptorBufferInfo* pBufferInfo;
			nullptr									//const BufferView* pTexelBufferView;
		}
	};
	vulkan->device.updateDescriptorSets(writeDescriptorSetsBlur, nullptr);
}

void SSDO::updateDescriptorSets(std::map<std::string, Image>& renderTargets)
{
	vk::DescriptorImageInfo texDescriptorPosition = vk::DescriptorImageInfo{
		renderTargets["depth"].sampler,		//Sampler sampler;
		renderTargets["depth"].view,			//ImageView imageView;
		vk::ImageLayout::eColorAttachmentOptimal	//ImageLayout imageLayout;
	};
	vk::DescriptorImageInfo texDescriptorNormal = vk::DescriptorImageInfo{
		renderTargets["normal"].sampler,			//Sampler sampler;
		renderTargets["normal"].view,			//ImageView imageView;
		vk::ImageLayout::eColorAttachmentOptimal	//ImageLayout imageLayout;
	};
	vk::DescriptorImageInfo texDescriptor = vk::DescriptorImageInfo{
		renderTargets["ssdo"].sampler,			//Sampler sampler;
		renderTargets["ssdo"].view,				//ImageView imageView;
		vk::ImageLayout::eColorAttachmentOptimal	//ImageLayout imageLayout;
	};
	std::vector<vk::WriteDescriptorSet> writeDescriptorSets = {
		// Binding 0: Position texture target
		vk::WriteDescriptorSet{
			DSet,								//DescriptorSet dstSet;
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
			DSet,								//DescriptorSet dstSet;
			1,										//uint32_t dstBinding;
			0,										//uint32_t dstArrayElement;
			1,										//uint32_t descriptorCount_;
			vk::DescriptorType::eCombinedImageSampler,//DescriptorType descriptorType;
			&texDescriptorNormal,					//const DescriptorImageInfo* pImageInfo;
			nullptr,								//const DescriptorBufferInfo* pBufferInfo;
			nullptr									//const BufferView* pTexelBufferView;
		},
		// Binding 2: SSDO Noise Image
		vk::WriteDescriptorSet{
			DSet,								//DescriptorSet dstSet;
			2,										//uint32_t dstBinding;
			0,										//uint32_t dstArrayElement;
			1,										//uint32_t descriptorCount_;
			vk::DescriptorType::eCombinedImageSampler,//DescriptorType descriptorType;
			&vk::DescriptorImageInfo()				//const DescriptorImageInfo* pImageInfo;
				.setSampler(noiseTex.sampler)
				.setImageView(noiseTex.view)
				.setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal),
			nullptr,								//const DescriptorBufferInfo* pBufferInfo;
			nullptr									//const BufferView* pTexelBufferView;
		},
		// Binding 3: SSDO Kernel
		vk::WriteDescriptorSet{
			DSet,								//DescriptorSet dstSet;
			3,										//uint32_t dstBinding;
			0,										//uint32_t dstArrayElement;
			1,										//uint32_t descriptorCount_;
			vk::DescriptorType::eUniformBuffer,		//DescriptorType descriptorType;
			nullptr,								//const DescriptorImageInfo* pImageInfo;
			&vk::DescriptorBufferInfo()				//const DescriptorBufferInfo* pBufferInfo;
				.setBuffer(UB_Kernel.buffer)		// Buffer buffer;
				.setOffset(0)							// DeviceSize offset;
				.setRange(UB_Kernel.size),		// DeviceSize range;
			nullptr									//const BufferView* pTexelBufferView;
		},
		// Binding 4: Projection View Size
		vk::WriteDescriptorSet{
			DSet,								//DescriptorSet dstSet;
			4,										//uint32_t dstBinding;
			0,										//uint32_t dstArrayElement;
			1,										//uint32_t descriptorCount_;
			vk::DescriptorType::eUniformBuffer,		//DescriptorType descriptorType;
			nullptr,								//const DescriptorImageInfo* pImageInfo;
			&vk::DescriptorBufferInfo()				//const DescriptorBufferInfo* pBufferInfo;
				.setBuffer(UB_PVM.buffer)		// Buffer buffer;
				.setOffset(0)							// DeviceSize offset;
				.setRange(UB_PVM.size),			// DeviceSize range;
			nullptr									//const BufferView* pTexelBufferView;
		}
	};
	vulkan->device.updateDescriptorSets(writeDescriptorSets, nullptr);


	std::vector<vk::WriteDescriptorSet> writeDescriptorSetsBlur = {
		// Binding 0: SSDO texture
		vk::WriteDescriptorSet{
			DSBlur,									//DescriptorSet dstSet;
			0,										//uint32_t dstBinding;
			0,										//uint32_t dstArrayElement;
			1,										//uint32_t descriptorCount_;
			vk::DescriptorType::eCombinedImageSampler,//DescriptorType descriptorType;
			&texDescriptor,							//const DescriptorImageInfo* pImageInfo;
			nullptr,								//const DescriptorBufferInfo* pBufferInfo;
			nullptr									//const BufferView* pTexelBufferView;
		},
		// Binding 1: Normals texture
		vk::WriteDescriptorSet{
			DSBlur,									//DescriptorSet dstSet;
			1,										//uint32_t dstBinding;
			0,										//uint32_t dstArrayElement;
			1,										//uint32_t descriptorCount_;
			vk::DescriptorType::eCombinedImageSampler,//DescriptorType descriptorType;
			&texDescriptorNormal,					//const DescriptorImageInfo* pImageInfo;
			nullptr,								//const DescriptorBufferInfo* pBufferInfo;
			nullptr									//const BufferView* pTexelBufferView;
		}
	};
	vulkan->device.updateDescriptorSets(writeDescriptorSetsBlur, nullptr);
}

void SSDO::update(Camera& camera)
{
	if (GUI::show_ssdo) {
		mat4 pvm[3]{ camera.projection, camera.view, camera.invProjection };
		memcpy(UB_PVM.data, pvm, sizeof(pvm));
	}
}
