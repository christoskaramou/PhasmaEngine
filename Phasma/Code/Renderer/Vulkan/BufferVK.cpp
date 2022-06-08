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

#if PE_VULKAN
#include "Renderer/RHI.h"
#include "Renderer/Buffer.h"
#include "Renderer/Command.h"
#include "Renderer/Queue.h"

namespace pe
{
    Buffer::Buffer(size_t size,
                   BufferUsageFlags usage,
                   AllocationCreateFlags createFlags,
                   const std::string &name)
        : size(size), data(nullptr)
    {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = usage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocationCreateInfo{};
        allocationCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocationCreateInfo.flags = (VmaAllocationCreateFlags)createFlags;

        VkBuffer bufferVK;
        VmaAllocation allocationVK;
        VmaAllocationInfo allocationInfo;
        PE_CHECK(vmaCreateBuffer(RHII.GetAllocator(), &bufferInfo, &allocationCreateInfo, &bufferVK, &allocationVK, &allocationInfo));
        m_handle = bufferVK;
        allocation = allocationVK;

        this->name = name;

        Debug::SetObjectName(m_handle, VK_OBJECT_TYPE_BUFFER, name);
    }

    Buffer::~Buffer()
    {
        Unmap();

        if (m_handle)
        {
            vmaDestroyBuffer(RHII.GetAllocator(), m_handle, allocation);
            m_handle = {};
        }
    }

    void Buffer::Map()
    {
        if (data)
            return;
        PE_CHECK(vmaMapMemory(RHII.GetAllocator(), allocation, &data));
    }

    void Buffer::Unmap()
    {
        if (!data)
            return;
        vmaUnmapMemory(RHII.GetAllocator(), allocation);
        data = nullptr;
    }

    void Buffer::Zero() const
    {
        if (!data)
            return;
        memset(data, 0, size);
    }

    void Buffer::CopyData(const void *srcData, size_t srcSize, size_t offset)
    {
        if (!data)
            return;
        assert(srcSize + offset <= size);
        memcpy((char *)data + offset, srcData, srcSize > 0 ? srcSize : size);
    }

    void Buffer::CopyBuffer(Buffer *srcBuffer, const size_t srcSize)
    {
        assert(srcSize <= size);

        BufferCopy bufferCopy{};
        bufferCopy.size = srcSize > 0 ? srcSize : size;

        Queue *queue = Queue::GetNext(QueueType::TransferBit, 1);
        CommandBuffer *copyCmd = CommandBuffer::GetNext(queue->GetFamilyId());
        copyCmd->Begin();
        copyCmd->CopyBuffer(srcBuffer, this, 1, &bufferCopy);
        copyCmd->End();
        queue->SubmitAndWaitFence(1, &copyCmd, nullptr, 0, nullptr, 0, nullptr);
        CommandBuffer::Return(copyCmd);
        Queue::Return(queue);
    }

    void Buffer::Copy(MemoryRange *ranges, uint32_t count, bool isPersistent)
    {
        // Map or check if the buffer is already mapped
        Map();

        for (uint32_t i = 0; i < count; i++)
            CopyData(ranges[i].data, ranges[i].size, ranges[i].offset);

        // Keep the buffer mapped if it is persistent
        if (!isPersistent)
        {
            for (uint32_t i = 0; i < count; i++)
                Flush(ranges[i].offset, ranges[i].size);

            Unmap();
        }
    }

    void Buffer::Flush(size_t offset, size_t flushSize) const
    {
        if (!data)
            return;

        PE_CHECK(vmaFlushAllocation(RHII.GetAllocator(), allocation, offset, flushSize > 0 ? flushSize : size));
    }

    size_t Buffer::Size()
    {
        return size;
    }

    void *Buffer::Data()
    {
        return data;
    }
}
#endif
