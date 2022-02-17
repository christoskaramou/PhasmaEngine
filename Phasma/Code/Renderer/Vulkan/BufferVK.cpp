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

namespace pe
{
    Buffer::Buffer(size_t size, BufferUsageFlags usage, AllocationCreateFlags properties)
        : size(size), data(nullptr)
    {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = usage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocationCreateInfo = {};
        allocationCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocationCreateInfo.flags = (VmaAllocationCreateFlags)properties;
        allocationCreateInfo.preferredFlags = 0;

        VkBuffer bufferVK;
        VmaAllocation allocationVK;
        VmaAllocationInfo allocationInfo;
        vmaCreateBuffer(RHII.allocator, &bufferInfo, &allocationCreateInfo, &bufferVK, &allocationVK, &allocationInfo);
        m_handle = bufferVK;
        allocation = allocationVK;
    }

    Buffer::~Buffer()
    {
        if (data)
            Unmap();
            
        if (m_handle)
        {
            vmaDestroyBuffer(RHII.allocator, m_handle, allocation);
            m_handle = {};
        }
    }

    void Buffer::Map()
    {
        if (data)
            return;
        vmaMapMemory(RHII.allocator, allocation, &data);
    }

    void Buffer::Unmap()
    {
        if (!data)
            return;
        vmaUnmapMemory(RHII.allocator, allocation);
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

        std::array<CommandBuffer *, 1> copyCmd{};
        copyCmd[0] = CommandBuffer::Create(RHII.commandPool2);
        copyCmd[0]->Begin();
        copyCmd[0]->CopyBuffer(srcBuffer, this, 1, &bufferCopy);
        copyCmd[0]->End();
        RHII.SubmitAndWaitFence(1, copyCmd.data(), nullptr, 0, nullptr, 0, nullptr);
        copyCmd[0]->Destroy();
    }

    void Buffer::Flush(size_t offset, size_t flushSize) const
    {
        if (!data)
            return;

        vmaFlushAllocation(RHII.allocator, allocation, offset, flushSize);
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
