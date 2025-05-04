#ifdef PE_VULKAN
#include "API/Buffer.h"
#include "API/RHI.h"
#include "API/Command.h"

namespace pe
{
    Buffer::Buffer(size_t size, BufferUsageFlags usage, AllocationCreateFlags createFlags, const std::string &name)
        : m_size{size},
          m_data{nullptr},
          m_usage{usage},
          m_allocation{},
          m_name{name}

    {
        if (m_usage & BufferUsage::UniformBufferBit)
        {
            m_size = RHII.AlignUniform(m_size);
            PE_ERROR_IF(m_size > RHII.GetMaxUniformBufferSize(), "Uniform buffer size is too big");
        }
        if (m_usage & BufferUsage::StorageBufferBit)
        {
            m_size = RHII.AlignStorage(m_size);
            PE_ERROR_IF(m_size > RHII.GetMaxStorageBufferSize(), "Storage buffer size is too big");
        }
        if (m_usage & BufferUsage::IndirectBufferBit)
        {
            PE_ERROR_IF(m_size > RHII.GetMaxDrawIndirectCount() * sizeof(VkDrawIndexedIndirectCommand), "Indirect command buffer size is too big");
        }

        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = m_size;
        bufferInfo.usage = Translate<VkBufferUsageFlags>(m_usage);
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocationCreateInfo{};
        allocationCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocationCreateInfo.flags = Translate<VmaAllocationCreateFlags>(createFlags);

        VkBuffer bufferVK;
        VmaAllocation allocationVK;
        VmaAllocationInfo allocationInfo;
        PE_CHECK(vmaCreateBuffer(RHII.GetAllocator(), &bufferInfo, &allocationCreateInfo, &bufferVK, &allocationVK, &allocationInfo));
        m_apiHandle = bufferVK;
        m_allocation = allocationVK;

        Debug::SetObjectName(m_apiHandle, m_name);
    }

    Buffer::~Buffer()
    {
        Unmap();

        if (m_apiHandle)
        {
            vmaDestroyBuffer(RHII.GetAllocator(), m_apiHandle, m_allocation);
            m_apiHandle = {};
        }
    }

    void Buffer::Map()
    {
        if (m_data)
            return;
        PE_CHECK(vmaMapMemory(RHII.GetAllocator(), m_allocation, &m_data));
    }

    void Buffer::Unmap()
    {
        if (!m_data)
            return;
        vmaUnmapMemory(RHII.GetAllocator(), m_allocation);
        m_data = nullptr;
    }

    void Buffer::Zero() const
    {
        PE_ERROR_IF(!m_data, "Buffer::Zero: Buffer is not mapped");
        memset(m_data, 0, m_size);
    }

    void Buffer::CopyDataRaw(const void *data, size_t size, size_t offset)
    {
        PE_ERROR_IF(!m_data, "Buffer::CopyDataRaw: Buffer is not mapped");
        PE_ERROR_IF(offset + size > m_size, "Buffer::CopyDataRaw: Source data size is too large");
        memcpy((char *)m_data + offset, data, size);
    }

    void Buffer::Copy(uint32_t count, BufferRange *ranges, bool keepMapped)
    {
        // Map or check if the buffer is already mapped
        Map();

        for (uint32_t i = 0; i < count; i++)
            CopyDataRaw(ranges[i].data, ranges[i].size, ranges[i].offset);

        // Keep the buffer mapped if it is persistent
        if (!keepMapped)
        {
            for (uint32_t i = 0; i < count; i++)
                Flush(ranges[i].size, ranges[i].offset);

            Unmap();
        }
    }

    void Buffer::CopyBufferStaged(CommandBuffer *cmd, void *data, size_t size, size_t dtsOffset)
    {
        PE_ERROR_IF(dtsOffset + size > m_size, "Buffer::StagedCopy: Data size is too large");

        // Staging buffer
        Buffer *staging = Buffer::Create(
            size,
            BufferUsage::TransferSrcBit,
            AllocationCreate::HostAccessSequentialWriteBit,
            "staging_buffer");

        // Copy data to staging buffer
        BufferRange range{};
        range.data = data;
        range.size = size;
        range.offset = 0;
        staging->Copy(1, &range, false);

        // Copy staging buffer to this buffer
        CopyBuffer(cmd, staging, size, 0, dtsOffset);
        cmd->AddAfterWaitCallback([staging]()
                                  { Buffer *buf = staging;
                                    Buffer::Destroy(buf); });
    }

    void Buffer::Flush(size_t size, size_t offset) const
    {
        if (!m_data)
            return;

        PE_CHECK(vmaFlushAllocation(RHII.GetAllocator(), m_allocation, offset, size > 0 ? size : m_size));
    }

    size_t Buffer::Size()
    {
        return m_size;
    }

    void *Buffer::Data()
    {
        return m_data;
    }

    void Buffer::CopyBuffer(CommandBuffer *cmd, Buffer *src, size_t size, size_t srcOffset, size_t dstOffset)
    {
        PE_ERROR_IF(size + srcOffset > src->Size(), "Buffer::CopyBuffer: Source size is too big");
        PE_ERROR_IF(size + dstOffset > m_size, "Buffer::CopyBuffer: Destination size is too small");

        VkBufferCopy region{};
        region.srcOffset = srcOffset;
        region.dstOffset = dstOffset;
        region.size = size;

        vkCmdCopyBuffer(cmd->ApiHandle(), src->ApiHandle(), ApiHandle(), 1, &region);
    }

    void Buffer::Barrier(CommandBuffer *cmd, const BufferBarrierInfo &info)
    {
        PE_ERROR_IF(!info.buffer, "Buffer::Barrier: no buffer specified");

        VkBufferMemoryBarrier2 barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        barrier.srcStageMask = Translate<VkPipelineStageFlags2>(info.srcStage);
        barrier.dstStageMask = Translate<VkPipelineStageFlags2>(info.dstStage);
        barrier.srcAccessMask = Translate<VkAccessFlags2>(info.srcAccess);
        barrier.dstAccessMask = Translate<VkAccessFlags2>(info.dstAccess);
        barrier.buffer = info.buffer->m_apiHandle;
        barrier.offset = info.offset;
        barrier.size = info.size ? info.size : VK_WHOLE_SIZE;

        VkDependencyInfoKHR dependencyInfo = {};
        dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR;
        dependencyInfo.bufferMemoryBarrierCount = 1;
        dependencyInfo.pBufferMemoryBarriers = &barrier;

        vkCmdPipelineBarrier2(cmd->ApiHandle(), &dependencyInfo);
    }

    void Buffer::Barriers(CommandBuffer *cmd, const std::vector<BufferBarrierInfo> &infos)
    {
        if (infos.empty())
            return;

        std::vector<VkBufferMemoryBarrier2> barriers;
        barriers.reserve(infos.size());
        for (const auto &info : infos)
        {
            PE_ERROR_IF(!info.buffer, "Buffer::Barriers: no buffer specified");

            VkBufferMemoryBarrier2 barrier = {};
            barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            barrier.srcStageMask = Translate<VkPipelineStageFlags2>(info.srcStage);
            barrier.dstStageMask = Translate<VkPipelineStageFlags2>(info.dstStage);
            barrier.srcAccessMask = Translate<VkAccessFlags2>(info.srcAccess);
            barrier.dstAccessMask = Translate<VkAccessFlags2>(info.dstAccess);
            barrier.buffer = info.buffer->m_apiHandle;
            barrier.offset = info.offset;
            barrier.size = info.size ? info.size : VK_WHOLE_SIZE;
            barriers.push_back(barrier);
        }

        VkDependencyInfoKHR dependencyInfo = {};
        dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR;
        dependencyInfo.bufferMemoryBarrierCount = static_cast<uint32_t>(barriers.size());
        dependencyInfo.pBufferMemoryBarriers = barriers.data();

        vkCmdPipelineBarrier2(cmd->ApiHandle(), &dependencyInfo);
    }
}
#endif
