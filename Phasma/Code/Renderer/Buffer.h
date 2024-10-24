#pragma once

namespace pe
{
    struct MemoryRange
    {
        void *data;    // source data
        size_t size;   // source data size in bytes
        size_t offset; // offset to destination data in bytes
    };

    class Buffer;
    struct BufferBarrierInfo : public BarrierInfo
    {
        BufferBarrierInfo() { type = BarrierType::Buffer; }

        Buffer *buffer = nullptr;
        PipelineStageFlags srcStage = PipelineStage::None;
        PipelineStageFlags dstStage = PipelineStage::None;
        AccessFlags srcAccess = Access::None;
        AccessFlags dstAccess = Access::None;
        size_t size = 0;
        size_t offset = 0;
    };

    class Buffer : public PeHandle<Buffer, BufferApiHandle>
    {
    public:
        Buffer(size_t size,
               BufferUsageFlags usage,
               AllocationCreateFlags createFlags,
               const std::string &name);
        ~Buffer();

        void Map();
        void Unmap();
        void Flush(size_t size = 0, size_t offset = 0) const;
        void Zero() const;
        void Copy(uint32_t count, MemoryRange *ranges, bool keepMapped);
        size_t Size();
        void *Data();
        void Barrier(CommandBuffer *cmd, const BufferBarrierInfo &info);
        static void Barriers(CommandBuffer *cmd, const std::vector<BufferBarrierInfo> &infos);

    private:
        friend class CommandBuffer;

        void CopyBuffer(CommandBuffer* cmd, Buffer *src, size_t size, size_t srcOffset, size_t dstOffset);
        void CopyBufferStaged(CommandBuffer *cmd, void *data, size_t size, size_t dtsOffset);
        void CopyDataRaw(const void *data, size_t size, size_t offset = 0);

        size_t m_size;
        void *m_data;
        BufferUsageFlags usage;
        AllocationApiHandle allocation;
        std::string name;
    };
}
