/*
Copyright (c) 2018-2021 Christos Karamoustos

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include "../Buffer.h"
#include "BufferVK.h"
#include "Vulkan.h"

namespace pe
{
	vk::BufferUsageFlags UsageFlagsToVK(BufferUsageFlags usage)
	{
		vk::BufferUsageFlags flags;
		
		if (usage & BufferUsage::TransferSrc)
			flags |= vk::BufferUsageFlagBits::eTransferSrc;
		if (usage & BufferUsage::TransferDst)
			flags |= vk::BufferUsageFlagBits::eTransferDst;
		if (usage & BufferUsage::UniformTexelBuffer)
			flags |= vk::BufferUsageFlagBits::eUniformTexelBuffer;
		if (usage & BufferUsage::StorageTexelBuffer)
			flags |= vk::BufferUsageFlagBits::eStorageTexelBuffer;
		if (usage & BufferUsage::UniformBuffer)
			flags |= vk::BufferUsageFlagBits::eUniformBuffer;
		if (usage & BufferUsage::StorageBuffer)
			flags |= vk::BufferUsageFlagBits::eStorageBuffer;
		if (usage & BufferUsage::IndexBuffer)
			flags |= vk::BufferUsageFlagBits::eIndexBuffer;
		if (usage & BufferUsage::VertexBuffer)
			flags |= vk::BufferUsageFlagBits::eVertexBuffer;
		
		return flags;
	}
	
	vk::MemoryPropertyFlags MemoryFlagsToVK(MemoryPropertyFlags properties)
	{
		vk::MemoryPropertyFlags flags;
		
		if (properties & MemoryProperty::DeviceLocal)
			flags |= vk::MemoryPropertyFlagBits::eDeviceLocal;
		if (properties & MemoryProperty::HostVisible)
			flags |= vk::MemoryPropertyFlagBits::eHostVisible;
		if (properties & MemoryProperty::HostCoherent)
			flags |= vk::MemoryPropertyFlagBits::eHostCoherent;
		if (properties & MemoryProperty::HostCached)
			flags |= vk::MemoryPropertyFlagBits::eHostCached;
		
		return flags;
	}
	
	BufferVK::BufferVK()
	{
		buffer = make_ref(vk::Buffer());
		allocation = nullptr;
		size = 0;
	}
	
	void BufferVK::CreateBuffer(size_t size, BufferUsageFlags usage, MemoryPropertyFlags properties)
	{
		sizeRequested = size;
		this->size = size;
		data = nullptr;
		
		vk::BufferUsageFlags usageVK = UsageFlagsToVK(usage);
		vk::BufferCreateInfo bufferInfo;
		bufferInfo.size = size;
		bufferInfo.usage = usageVK;
		bufferInfo.sharingMode = vk::SharingMode::eExclusive;
		VkBufferCreateInfo vkBufferCreateInfo = VkBufferCreateInfo(bufferInfo);
		//buffer = make_ref(vulkan->device->createBuffer(bufferInfo));
		
		vk::MemoryPropertyFlags propertiesVK = MemoryFlagsToVK(properties);
		VmaAllocationCreateInfo allocationCreateInfo = {};
		allocationCreateInfo.usage =
				usageVK & vk::BufferUsageFlagBits::eTransferSrc ?
				VMA_MEMORY_USAGE_CPU_ONLY : propertiesVK & vk::MemoryPropertyFlagBits::eDeviceLocal ?
				                            VMA_MEMORY_USAGE_GPU_ONLY : VMA_MEMORY_USAGE_CPU_TO_GPU;
		allocationCreateInfo.preferredFlags = VkMemoryPropertyFlags(propertiesVK);
		
		VkBuffer vkBuffer;
		VmaAllocationInfo allocationInfo;
		vmaCreateBuffer(
				VulkanContext::Get()->allocator, &vkBufferCreateInfo, &allocationCreateInfo, &vkBuffer, &allocation,
				&allocationInfo
		);
		buffer = make_ref(vk::Buffer(vkBuffer));
	}
	
	void BufferVK::Map(size_t mapSize, size_t offset)
	{
		if (data)
			return;
		assert(mapSize + offset <= size);
		vmaMapMemory(VulkanContext::Get()->allocator, allocation, &data);
	}
	
	void BufferVK::Unmap()
	{
		if (!data)
			return;
		vmaUnmapMemory(VulkanContext::Get()->allocator, allocation);
		data = nullptr;
	}
	
	void BufferVK::Zero() const
	{
		if (!data)
			return;
		memset(data, 0, size);
	}
	
	void BufferVK::CopyData(const void* srcData, size_t srcSize, size_t offset)
	{
		if (!data)
			return;
		assert(srcSize + offset <= size);
		memcpy((char*) data + offset, srcData, srcSize > 0 ? srcSize : size);
	}
	
	void BufferVK::CopyBuffer(const vk::Buffer srcBuffer, const size_t srcSize) const
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
	
	void BufferVK::Flush(size_t offset, size_t flushSize) const
	{
		if (!data)
			return;
		
		vmaFlushAllocation(VulkanContext::Get()->allocator, allocation, offset, flushSize);
	}
	
	void BufferVK::Destroy() const
	{
		if (*buffer)
			vmaDestroyBuffer(VulkanContext::Get()->allocator, VkBuffer(*buffer), allocation);
		*buffer = nullptr;
	}
}
