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
#include "Renderer/CommandPool.h"
#include "Renderer/CommandBuffer.h"
#include "BufferVK.h"
#include "Vulkan.h"

namespace pe
{
	BufferVK::BufferVK(size_t size, BufferUsageFlags usage, MemoryPropertyFlags properties)
	{
		sizeRequested = size;
		this->size = size;
		data = nullptr;

		VkBufferCreateInfo bufferInfo{};
		bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferInfo.size = size;
		bufferInfo.usage = usage;
		bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		VmaAllocationCreateInfo allocationCreateInfo = {};
		allocationCreateInfo.usage =
			usage & VK_BUFFER_USAGE_TRANSFER_SRC_BIT ?
			VMA_MEMORY_USAGE_CPU_ONLY : properties & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT ?
			VMA_MEMORY_USAGE_GPU_ONLY : VMA_MEMORY_USAGE_CPU_TO_GPU;
		allocationCreateInfo.preferredFlags = properties;

		VkBuffer buffer;
		VmaAllocationInfo allocationInfo;
		vmaCreateBuffer(VULKAN.allocator, &bufferInfo, &allocationCreateInfo, &buffer, &allocation, &allocationInfo);
		m_handle = buffer;
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
	
	void BufferVK::CopyBuffer(Buffer* srcBuffer, const size_t srcSize)
	{
		assert(srcSize <= size);

		BufferCopy bufferCopy{};
		bufferCopy.size = srcSize > 0 ? srcSize : size;

		CommandPool pool(VkCommandPool(*VULKAN.commandPool2));
		CommandBuffer copyCmd;
		copyCmd.Create(pool);
		copyCmd.Begin();
		copyCmd.CopyBuffer(*srcBuffer, *this, 1, &bufferCopy);
		copyCmd.End();
		VULKAN.submitAndWaitFence(vk::CommandBuffer(copyCmd.Handle()), nullptr, nullptr, nullptr);
		copyCmd.Destroy();
	}
	
	void BufferVK::Flush(size_t offset, size_t flushSize) const
	{
		if (!data)
			return;
		
		vmaFlushAllocation(VULKAN.allocator, allocation, offset, flushSize);
	}
	
	void BufferVK::Destroy()
	{
		if (m_handle)
			vmaDestroyBuffer(VULKAN.allocator, m_handle, allocation);
		m_handle = {};
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
}
