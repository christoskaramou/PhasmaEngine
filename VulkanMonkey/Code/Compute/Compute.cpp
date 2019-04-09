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
			// Binding 2 (uniforms)
			vk::DescriptorSetLayoutBinding{
				2,										//uint32_t binding;
				vk::DescriptorType::eUniformBuffer,		//DescriptorType descriptorType;
				1,										//uint32_t descriptorCount;
				vk::ShaderStageFlagBits::eCompute,		//ShaderStageFlags stageFlags;
				nullptr									//const Sampler* pImmutableSamplers;
			},
			// Binding 3 (depth image)
			vk::DescriptorSetLayoutBinding{
				3,										//uint32_t binding;
				vk::DescriptorType::eCombinedImageSampler,//DescriptorType descriptorType;
				1,										//uint32_t descriptorCount;
				vk::ShaderStageFlagBits::eCompute,		//ShaderStageFlags stageFlags;
				nullptr									//const Sampler* pImmutableSamplers;
			}
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

void vm::Compute::update(std::map<std::string, Image>& renderTargets)
{
	if (!sboIn.size()) return;

	if (SBIn.size != sboIn.size() * sizeof(SBOIn)) {
		vulkan->device.unmapMemory(SBIn.memory);
		vulkan->device.unmapMemory(SBOut.memory);
		SBIn.destroy();
		SBOut.destroy();
		SBIn.createBuffer(sboIn.size() * sizeof(SBOIn), vk::BufferUsageFlagBits::eStorageBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
		SBIn.data = vulkan->device.mapMemory(SBIn.memory, 0, SBIn.size);
		SBOut.createBuffer(sboIn.size() * sizeof(SBOOut), vk::BufferUsageFlagBits::eStorageBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
		SBOut.data = vulkan->device.mapMemory(SBOut.memory, 0, SBOut.size);
		updateDescriptorSets(renderTargets);
	}
	memcpy(UBIn.data, &uboIn, sizeof(uboIn));
	memcpy(SBIn.data, sboIn.data(), SBIn.size);
	memset(SBOut.data, 0, SBOut.size);
}

void Compute::createComputeUniforms(size_t size, std::map<std::string, Image>& renderTargets)
{
	SBIn.createBuffer(size, vk::BufferUsageFlagBits::eStorageBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
	SBIn.data = vulkan->device.mapMemory(SBIn.memory, 0, SBIn.size);
	memset(SBIn.data, 0, SBIn.size);

	SBOut.createBuffer(size, vk::BufferUsageFlagBits::eStorageBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
	SBOut.data = vulkan->device.mapMemory(SBOut.memory, 0, SBOut.size);
	memset(SBOut.data, 0, SBOut.size);

	UBIn.createBuffer(sizeof(uboIn), vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
	UBIn.data = vulkan->device.mapMemory(UBIn.memory, 0, UBIn.size);
	memset(UBIn.data, 0, UBIn.size);

	DSCompute = vulkan->device.allocateDescriptorSets({ vulkan->descriptorPool, 1, &DSLayoutCompute }).at(0);

	updateDescriptorSets(renderTargets);
}

void vm::Compute::updateDescriptorSets(std::map<std::string, Image>& renderTargets)
{
	std::deque<vk::DescriptorBufferInfo> dsbi{};
	auto wSetBuffer = [&dsbi](vk::DescriptorSet& dstSet, uint32_t dstBinding, Buffer& buffer, vk::DescriptorType type) {
		dsbi.push_back({ buffer.buffer, 0, buffer.size });
		return vk::WriteDescriptorSet{ dstSet, dstBinding, 0, 1, type, nullptr, &dsbi.back(), nullptr };
	};
	std::deque<vk::DescriptorImageInfo> dsii{};
	auto wSetImage = [&dsii](vk::DescriptorSet& dstSet, uint32_t dstBinding, Image& image) {
		dsii.push_back({ image.sampler, image.view, vk::ImageLayout::eColorAttachmentOptimal });
		return vk::WriteDescriptorSet{ dstSet, dstBinding, 0, 1, vk::DescriptorType::eCombinedImageSampler, &dsii.back(), nullptr, nullptr };
	};
	std::vector<vk::WriteDescriptorSet> writeCompDescriptorSets{
		wSetBuffer(DSCompute, 0, SBIn, vk::DescriptorType::eStorageBuffer),
		wSetBuffer(DSCompute, 1, SBOut, vk::DescriptorType::eStorageBuffer),
		wSetBuffer(DSCompute, 2, UBIn, vk::DescriptorType::eUniformBuffer),
		wSetImage(DSCompute, 3, renderTargets["depth"])
	};
	vulkan->device.updateDescriptorSets(writeCompDescriptorSets, nullptr);
}

void Compute::destroy()
{
	sboIn.clear();
	SBIn.destroy();
	SBOut.destroy();
	UBIn.destroy();
	pipeline.destroy();
	if (Compute::DSLayoutCompute) {
		vulkan->device.destroyDescriptorSetLayout(Compute::DSLayoutCompute);
		Compute::DSLayoutCompute = nullptr;
	}
}
