#pragma once

namespace pe
{
    struct MemoryRange
    {
        void *data;    // source data
        size_t size;   // source data size in bytes
        size_t offset; // offset to destination data in bytes
    };

    class Buffer : public IHandle<Buffer, BufferHandle>
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

        void Copy(uint32_t count, MemoryRange *ranges, bool persistent);

        size_t Size();

        void *Data();

    private:
        friend class CommandBuffer;

        void CopyBuffer(CommandBuffer* cmd, Buffer *src, size_t size, size_t srcOffset, size_t dstOffset);

        void CopyBufferStaged(CommandBuffer *cmd, void *data, size_t size, size_t dtsOffset);
        
        void CopyDataRaw(const void *data, size_t size, size_t offset = 0);

        size_t size;
        void *data;
        BufferUsageFlags usage;
        AllocationHandle allocation;
        std::string name;
    };
}