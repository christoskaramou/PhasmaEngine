#include "API/Buffer.h"
#include "API/RHI.h"
#include "API/Command.h"

namespace pe
{
    Buffer::Buffer(size_t size, vk::BufferUsageFlags2 usage, VmaAllocationCreateFlags createFlags, const std::string &name)
        : m_size{size},
          m_usage{usage},
          m_name{name},
          m_data{nullptr}

    {
        if (m_usage & vk::BufferUsageFlagBits2::eUniformBuffer)
        {
            m_size = RHII.AlignUniform(m_size);
            PE_ERROR_IF(m_size > RHII.GetMaxUniformBufferSize(), "Uniform buffer size is too big");
        }
        if (m_usage & vk::BufferUsageFlagBits2::eStorageBuffer)
        {
            m_size = RHII.AlignStorage(m_size);
            PE_ERROR_IF(m_size > RHII.GetMaxStorageBufferSize(), "Storage buffer size is too big");
        }
        if (m_usage & vk::BufferUsageFlagBits2::eIndirectBuffer)
        {
            PE_ERROR_IF(m_size > RHII.GetMaxDrawIndirectCount() * sizeof(VkDrawIndexedIndirectCommand), "Indirect command buffer size is too big");
        }

        vk::BufferUsageFlags2CreateInfo flags2{};
        flags2.usage = m_usage;

        vk::BufferCreateInfo bufferInfo{};
        bufferInfo.pNext = &flags2;
        bufferInfo.size = m_size;
        bufferInfo.sharingMode = vk::SharingMode::eExclusive;

        VmaAllocationCreateInfo allocationCreateInfo{};
        allocationCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocationCreateInfo.flags = createFlags;

        VkBuffer vkBuffer = {};
        PE_CHECK(vmaCreateBuffer(RHII.GetAllocator(), reinterpret_cast<const VkBufferCreateInfo *>(&bufferInfo), &allocationCreateInfo, &vkBuffer, &m_allocation, &m_allocationInfo));
        m_apiHandle = vkBuffer;

        vmaSetAllocationName(RHII.GetAllocator(), m_allocation, m_name.c_str());
        Debug::SetObjectName(m_apiHandle, m_name);
    }

    Buffer::~Buffer()
    {
        Unmap();
        vmaDestroyBuffer(RHII.GetAllocator(), m_apiHandle, m_allocation);
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
            vk::BufferUsageFlagBits2::eTransferSrc,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
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

        vk::BufferCopy2 region{};
        region.srcOffset = srcOffset;
        region.dstOffset = dstOffset;
        region.size = size;

        vk::CopyBufferInfo2 copyInfo{};
        copyInfo.srcBuffer = src->ApiHandle();
        copyInfo.dstBuffer = ApiHandle();
        copyInfo.regionCount = 1;
        copyInfo.pRegions = &region;

        cmd->ApiHandle().copyBuffer2(copyInfo);
    }

    void Buffer::Barrier(CommandBuffer *cmd, const vk::BufferMemoryBarrier2 &info)
    {
        PE_ERROR_IF(!info.buffer, "Buffer::Barrier: no buffer specified");

        vk::DependencyInfo dependencyInfo = {};
        dependencyInfo.bufferMemoryBarrierCount = 1;
        dependencyInfo.pBufferMemoryBarriers = &info;

        cmd->ApiHandle().pipelineBarrier2(dependencyInfo);
    }

    void Buffer::Barriers(CommandBuffer *cmd, const std::vector<vk::BufferMemoryBarrier2> &infos)
    {
        if (infos.empty())
            return;

        for (const auto &info : infos)
        {
            PE_ERROR_IF(!info.buffer, "Buffer::Barriers: no buffer specified");
        }

        vk::DependencyInfo dependencyInfo = {};
        dependencyInfo.bufferMemoryBarrierCount = static_cast<uint32_t>(infos.size());
        dependencyInfo.pBufferMemoryBarriers = infos.data();

        cmd->ApiHandle().pipelineBarrier2(dependencyInfo);
    }
}
