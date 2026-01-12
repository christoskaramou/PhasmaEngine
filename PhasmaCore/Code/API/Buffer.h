#pragma once

namespace pe
{
    struct BufferRange
    {
        void *data;    // source data
        size_t size;   // source data size in bytes
        size_t offset; // offset to destination data in bytes
    };

    class Buffer;

    struct BufferBarrierInfo
    {
        Buffer *buffer = nullptr;
        vk::PipelineStageFlags2 stageMask = vk::PipelineStageFlagBits2::eNone;
        vk::AccessFlags2 accessMask = vk::AccessFlagBits2::eNone;
        uint32_t queueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        size_t offset = 0;
        size_t size = VK_WHOLE_SIZE;
    };
    using BufferTrackInfo = BufferBarrierInfo;

    class Buffer : public PeHandle<Buffer, vk::Buffer>
    {
    public:
        Buffer(size_t size,
               vk::BufferUsageFlags2 usage,
               VmaAllocationCreateFlags vmaCreateFlags,
               const std::string &name);
        ~Buffer();

        void Map();
        void Unmap();
        void Flush(size_t size = 0, size_t offset = 0) const;
        void Zero() const;
        void Copy(uint32_t count, BufferRange *ranges, bool keepMapped);
        size_t Size();
        void *Data();
        uint64_t GetDeviceAddress() const;
        BufferTrackInfo &GetTrackInfo() { return m_trackInfo; }

    private:
        friend class CommandBuffer;

        static void Barrier(CommandBuffer *cmd, const BufferBarrierInfo &info);
        static void Barriers(CommandBuffer *cmd, const std::vector<BufferBarrierInfo> &infos);

        void CopyBuffer(CommandBuffer *cmd, Buffer *src, size_t size, size_t srcOffset, size_t dstOffset);
        void CopyBufferStaged(CommandBuffer *cmd, const void *data, size_t size, size_t dstOffset);
        void CopyDataRaw(const void *data, size_t size, size_t offset = 0);

        size_t m_size;
        void *m_data;
        vk::BufferUsageFlags2 m_usage;
        VmaAllocation m_allocation;
        VmaAllocationInfo m_allocationInfo;
        std::string m_name;
        BufferTrackInfo m_trackInfo{};
    };
} // namespace pe
