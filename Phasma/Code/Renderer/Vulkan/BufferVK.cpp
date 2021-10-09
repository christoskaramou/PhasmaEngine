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

#include "Renderer/Buffer.h"
#include "BufferVK.h"
#include "Vulkan.h"

namespace pe
{
	BufferVK::BufferVK(size_t size, BufferUsageFlags usage, MemoryPropertyFlags properties)
	{
		vk::BufferUsageFlags usageVK = vk::BufferUsageFlags(usage);
		vk::MemoryPropertyFlags propertiesVK = vk::MemoryPropertyFlags(properties);

		sizeRequested = size;
		this->size = size;
		data = nullptr;

		vk::BufferCreateInfo bufferInfo;
		bufferInfo.size = size;
		bufferInfo.usage = usageVK;
		bufferInfo.sharingMode = vk::SharingMode::eExclusive;
		VkBufferCreateInfo vkBufferCreateInfo = VkBufferCreateInfo(bufferInfo);
		VmaAllocationCreateInfo allocationCreateInfo = {};
		allocationCreateInfo.usage =
			usageVK & vk::BufferUsageFlagBits::eTransferSrc ?
			VMA_MEMORY_USAGE_CPU_ONLY : propertiesVK & vk::MemoryPropertyFlagBits::eDeviceLocal ?
			VMA_MEMORY_USAGE_GPU_ONLY : VMA_MEMORY_USAGE_CPU_TO_GPU;
		allocationCreateInfo.preferredFlags = VkMemoryPropertyFlags(propertiesVK);

		VmaAllocationInfo allocationInfo;
		vmaCreateBuffer(
			VULKAN.allocator, &vkBufferCreateInfo, &allocationCreateInfo, &handle, &allocation,
			&allocationInfo
		);
	}

	BufferVK::~BufferVK()
	{
		Destroy();
	}
	
	void BufferVK::Map()
	{
		if (data)
			return;
		vmaMapMemory(VULKAN.allocator, allocation, &data);
	}
	
	void BufferVK::Unmap()
	{
		if (!data)
			return;
		vmaUnmapMemory(VULKAN.allocator, allocation);
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
	
	void BufferVK::CopyBuffer(Buffer* srcBuffer, const size_t srcSize) const
	{
		VkBuffer scrBufferVk = static_cast<BufferVK*>(srcBuffer)->handle;
		assert(srcSize <= size);
		vk::CommandBufferAllocateInfo cbai;
		cbai.level = vk::CommandBufferLevel::ePrimary;
		cbai.commandPool = *VULKAN.commandPool2;
		cbai.commandBufferCount = 1;
		const vk::CommandBuffer copyCmd = VULKAN.device->allocateCommandBuffers(cbai).at(0);
		
		vk::CommandBufferBeginInfo beginInfo;
		beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
		copyCmd.begin(beginInfo);
		
		vk::BufferCopy bufferCopy {};
		bufferCopy.size = srcSize > 0 ? srcSize : size;
		
		copyCmd.copyBuffer(scrBufferVk, handle, bufferCopy);
		
		copyCmd.end();
		
		VULKAN.submitAndWaitFence(copyCmd, nullptr, nullptr, nullptr);
		
		VULKAN.device->freeCommandBuffers(*VULKAN.commandPool2, copyCmd);
	}
	
	void BufferVK::Flush(size_t offset, size_t flushSize) const
	{
		if (!data)
			return;
		
		vmaFlushAllocation(VULKAN.allocator, allocation, offset, flushSize);
	}
	
	void BufferVK::Destroy()
	{
		if (handle)
			vmaDestroyBuffer(VULKAN.allocator, handle, allocation);
		handle = nullptr;
	}

	void BufferVK::SetDebugName(const std::string& debugName)
	{
#if _DEBUG
		VULKAN.SetDebugObjectName(vk::Buffer(handle), debugName);
#endif
	}
	size_t BufferVK::Size()
	{
		return size;
	}

	size_t BufferVK::SizeRequested()
	{
		return sizeRequested;
	}

	void* BufferVK::Data()
	{
		return data;
	}

	void* BufferVK::Handle()
	{
		return &handle;
	}
}
