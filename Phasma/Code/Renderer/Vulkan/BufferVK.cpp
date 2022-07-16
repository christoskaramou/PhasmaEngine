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
    {
        if (usage & BufferUsage::UniformBufferBit)
        {
            size = RHII.AlignUniform(size);
            PE_ERROR_IF(size > RHII.GetMaxUniformBufferSize(), "Buffer size is too big");
        }
        else if (usage & BufferUsage::StorageBufferBit)
        {
            size = RHII.AlignStorage(size);
            PE_ERROR_IF(size > RHII.GetMaxStorageBufferSize(), "Buffer size is too big");
        }

        this->size = size;
        data = nullptr;

        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = Translate<VkBufferUsageFlags>(usage);
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocationCreateInfo{};
        allocationCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocationCreateInfo.flags = Translate<VmaAllocationCreateFlags>(createFlags);

        VkBuffer bufferVK;
        VmaAllocation allocationVK;
        VmaAllocationInfo allocationInfo;
        PE_CHECK(vmaCreateBuffer(RHII.GetAllocator(), &bufferInfo, &allocationCreateInfo, &bufferVK, &allocationVK, &allocationInfo));
        m_handle = bufferVK;
        allocation = allocationVK;

        this->name = name;

        Debug::SetObjectName(m_handle, ObjectType::Buffer, name);
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
            PE_ERROR("Buffer::Zero: Buffer is not mapped");
        memset(data, 0, size);
    }

    void Buffer::CopyDataRaw(const void *data, size_t size, size_t offset)
    {
        if (!this->data)
            PE_ERROR("Buffer::CopyDataRaw: Buffer is not mapped");
        if (offset + size > this->size)
            PE_ERROR("Buffer::CopyDataRaw: Source data size is too large");

        memcpy((char *)this->data + offset, data, size);
    }

    void Buffer::Copy(uint32_t count, MemoryRange *ranges, bool persistent)
    {
        // Map or check if the buffer is already mapped
        Map();

        for (uint32_t i = 0; i < count; i++)
            CopyDataRaw(ranges[i].data, ranges[i].size, ranges[i].offset);

        // Keep the buffer mapped if it is persistent
        if (!persistent)
        {
            for (uint32_t i = 0; i < count; i++)
                Flush(ranges[i].size, ranges[i].offset);

            Unmap();
        }
    }

    void Buffer::CopyBufferStaged(CommandBuffer *cmd, void *data, size_t size, size_t dtsOffset)
    {
        if (dtsOffset + size > this->size)
            PE_ERROR("Buffer::StagedCopy: Data size is too large");

        // Staging buffer
        Buffer *staging = Buffer::Create(
            size,
            BufferUsage::TransferSrcBit,
            AllocationCreate::HostAccessSequentialWriteBit,
            "staging_buffer");

        // Copy data to staging buffer
        MemoryRange mr{};
        mr.data = data;
        mr.size = size;
        mr.offset = 0;
        staging->Copy(1, &mr, false);

        // Copy staging buffer to this buffer
        cmd->CopyBuffer(staging, this, size, 0, dtsOffset);
        cmd->AddAfterWaitCallback([staging]()
                                  { Buffer::Destroy(staging); });
    }

    void Buffer::Flush(size_t size, size_t offset) const
    {
        if (!data)
            return;

        PE_CHECK(vmaFlushAllocation(RHII.GetAllocator(), allocation, offset, size > 0 ? size : this->size));
    }

    size_t Buffer::Size()
    {
        return size;
    }

    void *Buffer::Data()
    {
        return data;
    }

    void Buffer::CopyBuffer(CommandBuffer *cmd, Buffer *src, size_t size, size_t srcOffset, size_t dstOffset)
    {
        PE_ERROR_IF(size + srcOffset > src->Size(), "Buffer::CopyBuffer: Source size is too large");
        PE_ERROR_IF(size + dstOffset > this->size, "Buffer::CopyBuffer: Destination size is too small");

        VkBufferCopy region{};
        region.srcOffset = srcOffset;
        region.dstOffset = dstOffset;
        region.size = size;

        vkCmdCopyBuffer(cmd->Handle(), src->Handle(), Handle(), 1, &region);
    }
}
#endif
