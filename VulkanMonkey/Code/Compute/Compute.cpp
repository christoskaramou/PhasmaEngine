#include "Compute.h"
#include <deque>

using namespace vm;

vk::DescriptorSetLayout Compute::DSLayoutCompute = nullptr;

vk::DescriptorSetLayout vm::Compute::getDescriptorLayout()
{
	if (!DSLayoutCompute) {
		std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings =
		{
			// Binding 0 (in)
			vk::DescriptorSetLayoutBinding{
				0,										//uint32_t binding;
				vk::DescriptorType::eStorageBuffer,		//DescriptorType descriptorType;
				1,										//uint32_t descriptorCount;
				vk::ShaderStageFlagBits::eCompute,		//ShaderStageFlags stageFlags;
				nullptr									//const Sampler* pImmutableSamplers;
			},
			// Binding 1 (out)
			vk::DescriptorSetLayoutBinding{
				1,										//uint32_t binding;
				vk::DescriptorType::eStorageBuffer,		//DescriptorType descriptorType;
				1,										//uint32_t descriptorCount;
				vk::ShaderStageFlagBits::eCompute,		//ShaderStageFlags stageFlags;
				nullptr									//const Sampler* pImmutableSamplers;
			},
		};

		vk::DescriptorSetLayoutCreateInfo dlci = vk::DescriptorSetLayoutCreateInfo{
			vk::DescriptorSetLayoutCreateFlags(),		//DescriptorSetLayoutCreateFlags flags;
			(uint32_t)setLayoutBindings.size(),			//uint32_t bindingCount;
			setLayoutBindings.data()					//const DescriptorSetLayoutBinding* pBindings;
		};
		DSLayoutCompute = VulkanContext::get().device.createDescriptorSetLayout(dlci);
	}
	return DSLayoutCompute;
}

void vm::Compute::update()
{

}

void Compute::createComputeStorageBuffers(size_t sizeIn, size_t sizeOut)
{
	SBIn.createBuffer(sizeIn, vk::BufferUsageFlagBits::eStorageBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
	SBIn.data = vulkan->device.mapMemory(SBIn.memory, 0, SBIn.size);
	memset(SBIn.data, 0, SBIn.size);

	SBOut.createBuffer(sizeOut, vk::BufferUsageFlagBits::eStorageBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
	SBOut.data = vulkan->device.mapMemory(SBOut.memory, 0, SBOut.size);
	memset(SBOut.data, 0, SBOut.size);

	DSCompute = vulkan->device.allocateDescriptorSets({ vulkan->descriptorPool, 1, &DSLayoutCompute }).at(0);

	updateDescriptorSets();
}

void vm::Compute::updateDescriptorSets()
{
	std::deque<vk::DescriptorBufferInfo> dsbi{};
	auto wSetBuffer = [&dsbi](vk::DescriptorSet& dstSet, uint32_t dstBinding, Buffer& buffer, vk::DescriptorType type) {
		dsbi.push_back({ buffer.buffer, 0, buffer.size });
		return vk::WriteDescriptorSet{ dstSet, dstBinding, 0, 1, type, nullptr, &dsbi.back(), nullptr };
	};
	std::vector<vk::WriteDescriptorSet> writeCompDescriptorSets{
		wSetBuffer(DSCompute, 0, SBIn, vk::DescriptorType::eStorageBuffer),
		wSetBuffer(DSCompute, 1, SBOut, vk::DescriptorType::eStorageBuffer),
	};
	vulkan->device.updateDescriptorSets(writeCompDescriptorSets, nullptr);
}

void Compute::destroy()
{
	SBIn.destroy();
	SBOut.destroy();
	pipeline.destroy();
	if (Compute::DSLayoutCompute) {
		vulkan->device.destroyDescriptorSetLayout(Compute::DSLayoutCompute);
		Compute::DSLayoutCompute = nullptr;
	}
}
