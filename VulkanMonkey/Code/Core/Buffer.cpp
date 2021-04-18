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
        sizeRequested = size;
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

	void Buffer::map(size_t mapSize, size_t offset)
	{
		if (data)
            return;
        assert(mapSize + offset <= size);
		data = VulkanContext::get()->device->mapMemory(*memory, offset, mapSize > 0 ? mapSize : size - offset, vk::MemoryMapFlags());
	}

	void Buffer::unmap()
	{
		if (!data)
            return;
		VulkanContext::get()->device->unmapMemory(*memory);
		data = nullptr;
	}

	void Buffer::zero() const
	{
		if (!data)
            return;
		memset(data, 0, size);
	}

	void Buffer::copyData(const void* srcData, size_t srcSize, size_t offset)
	{
		if (!data)
            return;
        assert(srcSize + offset <= size);
		memcpy((char*)data + offset, srcData, srcSize > 0 ? srcSize : size);
	}

	void Buffer::copyBuffer(const vk::Buffer srcBuffer, const size_t srcSize) const
	{
        assert(srcSize <= size);
		vk::CommandBufferAllocateInfo cbai;
		cbai.level = vk::CommandBufferLevel::ePrimary;
		cbai.commandPool = *VulkanContext::get()->commandPool2;
		cbai.commandBufferCount = 1;
		const vk::CommandBuffer copyCmd = VulkanContext::get()->device->allocateCommandBuffers(cbai).at(0);

		vk::CommandBufferBeginInfo beginInfo;
		beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
		copyCmd.begin(beginInfo);

		vk::BufferCopy bufferCopy{};
		bufferCopy.size = srcSize > 0 ? srcSize : size;

		copyCmd.copyBuffer(srcBuffer, *buffer, bufferCopy);

		copyCmd.end();

		VulkanContext::get()->submitAndWaitFence(copyCmd, nullptr, nullptr, nullptr);

		VulkanContext::get()->device->freeCommandBuffers(*VulkanContext::get()->commandPool2, copyCmd);
	}

	void Buffer::flush(size_t flushSize)
	{
		if (!data)
            return;

        assert(flushSize <= size);
		vk::MappedMemoryRange range;
		range.memory = *memory;
		range.size = flushSize > 0 ? flushSize : size;

		VulkanContext::get()->device->flushMappedMemoryRanges(range);
	}

	void Buffer::destroy() const
	{
		if (*buffer)
			VulkanContext::get()->device->destroyBuffer(*buffer);
		if (*memory)
			VulkanContext::get()->device->freeMemory(*memory);
		*buffer = nullptr;
		*memory = nullptr;
	}
}
