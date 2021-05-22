#include "Buffer.h"
#include "../Renderer/Vulkan/Vulkan.h"

namespace pe
{
	Buffer::Buffer()
	{
		buffer = make_ref(vk::Buffer());
		allocation = nullptr;
		size = 0;
	}
	
	void Buffer::createBuffer(size_t size, const vk::BufferUsageFlags& usage, const vk::MemoryPropertyFlags& properties)
	{
		auto vulkan = VulkanContext::Get();
		sizeRequested = size;
		this->size = size;
		data = nullptr;
		
		vk::BufferCreateInfo bufferInfo;
		bufferInfo.size = size;
		bufferInfo.usage = usage;
		bufferInfo.sharingMode = vk::SharingMode::eExclusive;
		VkBufferCreateInfo vkBufferCreateInfo = VkBufferCreateInfo(bufferInfo);
		//buffer = make_ref(vulkan->device->createBuffer(bufferInfo));
		
		VmaAllocationCreateInfo allocationCreateInfo = {};
		allocationCreateInfo.usage =
				usage & vk::BufferUsageFlagBits::eTransferSrc ?
				VMA_MEMORY_USAGE_CPU_ONLY : properties & vk::MemoryPropertyFlagBits::eDeviceLocal ?
				                            VMA_MEMORY_USAGE_GPU_ONLY : VMA_MEMORY_USAGE_CPU_TO_GPU;
		allocationCreateInfo.preferredFlags = VkMemoryPropertyFlags(properties);
		
		VkBuffer vkBuffer;
		VmaAllocationInfo allocationInfo;
		vmaCreateBuffer(
				VulkanContext::Get()->allocator, &vkBufferCreateInfo, &allocationCreateInfo, &vkBuffer, &allocation,
				&allocationInfo
		);
		buffer = make_ref(vk::Buffer(vkBuffer));
	}
	
	void Buffer::map(size_t mapSize, size_t offset)
	{
		if (data)
			return;
		assert(mapSize + offset <= size);
		vmaMapMemory(VulkanContext::Get()->allocator, allocation, &data);
	}
	
	void Buffer::unmap()
	{
		if (!data)
			return;
		vmaUnmapMemory(VulkanContext::Get()->allocator, allocation);
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
		memcpy((char*) data + offset, srcData, srcSize > 0 ? srcSize : size);
	}
	
	void Buffer::copyBuffer(const vk::Buffer srcBuffer, const size_t srcSize) const
	{
		assert(srcSize <= size);
		vk::CommandBufferAllocateInfo cbai;
		cbai.level = vk::CommandBufferLevel::ePrimary;
		cbai.commandPool = *VulkanContext::Get()->commandPool2;
		cbai.commandBufferCount = 1;
		const vk::CommandBuffer copyCmd = VulkanContext::Get()->device->allocateCommandBuffers(cbai).at(0);
		
		vk::CommandBufferBeginInfo beginInfo;
		beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
		copyCmd.begin(beginInfo);
		
		vk::BufferCopy bufferCopy {};
		bufferCopy.size = srcSize > 0 ? srcSize : size;
		
		copyCmd.copyBuffer(srcBuffer, *buffer, bufferCopy);
		
		copyCmd.end();
		
		VulkanContext::Get()->submitAndWaitFence(copyCmd, nullptr, nullptr, nullptr);
		
		VulkanContext::Get()->device->freeCommandBuffers(*VulkanContext::Get()->commandPool2, copyCmd);
	}
	
	void Buffer::flush(size_t offset, size_t flushSize) const
	{
		if (!data)
			return;
		
		vmaFlushAllocation(VulkanContext::Get()->allocator, allocation, offset, flushSize);
	}
	
	void Buffer::destroy() const
	{
		if (*buffer)
			vmaDestroyBuffer(VulkanContext::Get()->allocator, VkBuffer(*buffer), allocation);
		*buffer = nullptr;
	}
}
