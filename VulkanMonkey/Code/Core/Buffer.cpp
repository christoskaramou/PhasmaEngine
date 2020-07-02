#include "vulkanPCH.h"
#include "Buffer.h"
#include "../VulkanContext/VulkanContext.h"

namespace vm
{
	Buffer::Buffer()
	{
		buffer = make_ref(vk::Buffer());
		memory = make_ref(vk::DeviceMemory());
		size = 0;
	}

	void Buffer::createBuffer(size_t size, const vk::BufferUsageFlags& usage, const vk::MemoryPropertyFlags& properties)
	{
		auto vulkan = VulkanContext::get();
		this->size = size;
		data = nullptr;

		vk::BufferCreateInfo bufferInfo;
		bufferInfo.size = size;
		bufferInfo.usage = usage;
		bufferInfo.sharingMode = vk::SharingMode::eExclusive;
		buffer = make_ref(vulkan->device->createBuffer(bufferInfo));

		uint32_t memTypeIndex = UINT32_MAX;
		auto const memRequirements = vulkan->device->getBufferMemoryRequirements(*buffer);
		auto const memProperties = vulkan->gpu->getMemoryProperties();
		for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
			if (memRequirements.memoryTypeBits & (1 << i) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
				memTypeIndex = i;
				break;
			}
		}
		if (memTypeIndex == UINT32_MAX)
			throw std::runtime_error("Could not Create buffer, memTypeIndex not valid");

		if (this->size < memRequirements.size)
			this->size = memRequirements.size;
		//allocate memory of buffer
		vk::MemoryAllocateInfo allocInfo;
		allocInfo.allocationSize = this->size;
		allocInfo.memoryTypeIndex = memTypeIndex;
		memory = make_ref(vulkan->device->allocateMemory(allocInfo));

		//binding memory with buffer
		vulkan->device->bindBufferMemory(*buffer, *memory, 0);
	}

	void Buffer::map(size_t offset)
	{
		if (data)
			throw std::runtime_error("Map called when buffer data is not null");
		data = VulkanContext::get()->device->mapMemory(*memory, offset, size - offset, vk::MemoryMapFlags());
	}

	void Buffer::unmap()
	{
		if (!data)
			throw std::runtime_error("Buffer is not mapped");
		VulkanContext::get()->device->unmapMemory(*memory);
		data = nullptr;
	}

	void Buffer::zero()
	{
		if (!data)
			throw std::runtime_error("Buffer is not mapped");
		memset(data, 0, size);
	}

	void Buffer::copyData(const void* srcData, size_t srcSize, size_t offset)
	{
		if (!data)
			throw std::runtime_error("Buffer is not mapped");
		memcpy((char*)data + offset, srcData, srcSize > 0 ? srcSize : size);
	}

	void Buffer::copyBuffer(const vk::Buffer srcBuffer, const size_t size) const
	{
		vk::CommandBufferAllocateInfo cbai;
		cbai.level = vk::CommandBufferLevel::ePrimary;
		cbai.commandPool = *VulkanContext::get()->commandPool2;
		cbai.commandBufferCount = 1;
		const vk::CommandBuffer copyCmd = VulkanContext::get()->device->allocateCommandBuffers(cbai).at(0);

		vk::CommandBufferBeginInfo beginInfo;
		beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
		copyCmd.begin(beginInfo);

		vk::BufferCopy bufferCopy{};
		bufferCopy.size = size;

		copyCmd.copyBuffer(srcBuffer, *buffer, bufferCopy);

		copyCmd.end();

		VulkanContext::get()->submitAndWaitFence(copyCmd, nullptr, nullptr, nullptr);

		VulkanContext::get()->device->freeCommandBuffers(*VulkanContext::get()->commandPool2, copyCmd);
	}

	void Buffer::flush(size_t size)
	{
		if (!data)
			throw std::runtime_error("Buffer is not mapped");

		vk::MappedMemoryRange range;
		range.memory = *memory;
		range.size = size > 0 ? size : this->size;

		VulkanContext::get()->device->flushMappedMemoryRanges(range);
	}

	void Buffer::destroy()
	{
		if (*buffer)
			VulkanContext::get()->device->destroyBuffer(*buffer);
		if (*memory)
			VulkanContext::get()->device->freeMemory(*memory);
		*buffer = nullptr;
		*memory = nullptr;
	}
}
