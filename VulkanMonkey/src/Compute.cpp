#include "../include/Compute.h"
#include "../include/Errors.h"

using namespace vm;

void Compute::createComputeUniforms(vk::Device device, vk::PhysicalDevice gpu, vk::DescriptorPool descriptorPool)
{
	std::vector<float> scrap(1024);
	for (unsigned i = 0; i < scrap.size(); i++) {
		scrap[i] = 1.f;
	}
	SBInOut.createBuffer(device, gpu, scrap.size() * sizeof(float), vk::BufferUsageFlagBits::eStorageBuffer, vk::MemoryPropertyFlagBits::eHostCoherent);
	VkCheck(device.mapMemory(SBInOut.memory, 0, SBInOut.size, vk::MemoryMapFlags(), &SBInOut.data));

	memcpy(SBInOut.data, scrap.data(), SBInOut.size);

	vk::DescriptorSetAllocateInfo allocCompInfo = vk::DescriptorSetAllocateInfo{
		descriptorPool,						//DescriptorPool descriptorPool;
		1,										//uint32_t descriptorSetCount;
		&DSLayoutCompute					//const DescriptorSetLayout* pSetLayouts;
	};
	VkCheck(device.allocateDescriptorSets(&allocCompInfo, &DSCompute));
	std::vector<vk::WriteDescriptorSet> writeCompDescriptorSets = {
		// Binding 0 (in out)
		vk::WriteDescriptorSet{
			DSCompute,							//DescriptorSet dstSet;
			0,										//uint32_t dstBinding;
			0,										//uint32_t dstArrayElement;
			1,										//uint32_t descriptorCount_;
			vk::DescriptorType::eStorageBuffer,		//DescriptorType descriptorType;
			nullptr,								//const DescriptorImageInfo* pImageInfo;
			&vk::DescriptorBufferInfo()				//const DescriptorBufferInfo* pBufferInfo;
				.setBuffer(SBInOut.buffer)			// Buffer buffer;
				.setOffset(0)							// DeviceSize offset;
				.setRange(SBInOut.size),			// DeviceSize range;
			nullptr									//const BufferView* pTexelBufferView;
		}
	};
	device.updateDescriptorSets(1, writeCompDescriptorSets.data(), 0, nullptr);
	std::cout << "DescriptorSet allocated and updated\n";

}

void vm::Compute::destroy(vk::Device device)
{
	SBInOut.destroy(device);
	if (DSLayoutCompute)
		device.destroyDescriptorSetLayout(DSLayoutCompute);
	pipeline.destroy(device);
}
