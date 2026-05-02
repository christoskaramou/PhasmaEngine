#pragma once

#include "API/RHITypes.h"

namespace pe
{
    struct BufferRange
    {
        void *data;    // source data
        size_t size;   // source data size in bytes
        size_t offset; // offset to destination data in bytes
    };

    struct BufferDesc
    {
        size_t size = 0;
        PeBufferUsageFlags usage = PE_BUFFER_USAGE_NONE;
        PeMemoryUsage memoryUsage = PE_MEMORY_USAGE_GPU_ONLY;
        std::string name;
    };

    class Buffer;
    class CommandBuffer;

    struct BufferBarrierInfo
    {
        Buffer *buffer = nullptr;
        PeBarrierSync stageMask = PE_STAGE_NONE;
        PeBarrierAccess accessMask = PE_ACCESS_NONE;
        uint32_t queueFamilyIndex = PE_QUEUE_FAMILY_IGNORED;
        size_t offset = 0;
        size_t size = PE_WHOLE_SIZE;
    };
    using BufferTrackInfo = BufferBarrierInfo;

    class Buffer : public NoCopy
    {
    public:
        struct Impl; // forward — defined in Buffer_Internal.h, body is engine-private

        static Buffer *Create(const BufferDesc &desc);
        static void Destroy(Buffer *&buf);
        static std::vector<Buffer *> GetHandles();

        void Map();
        void Unmap();
        void Flush(size_t size = 0, size_t offset = 0) const;
        void Zero() const;
        void Copy(uint32_t count, BufferRange *ranges, bool keepMapped);
        size_t Size() const { return m_size; }
        void *Data() const { return m_data; }
        PeBufferUsageFlags Usage() const { return m_usage; }
        uint64_t GetDeviceAddress() const;
        BufferTrackInfo &GetTrackInfo() { return m_trackInfo; }

    private:
        friend class CommandBuffer;
        friend struct VulkanBufferImpl;
        friend struct Dx12BufferImpl;

        Buffer(const BufferDesc &desc);
        ~Buffer();

        static void Barrier(CommandBuffer *cmd, const BufferBarrierInfo &info);
        static void Barriers(CommandBuffer *cmd, const std::vector<BufferBarrierInfo> &infos);

        void CopyBuffer(CommandBuffer *cmd, Buffer *src, size_t size, size_t srcOffset, size_t dstOffset);
        void CopyBufferStaged(CommandBuffer *cmd, const void *data, size_t size, size_t dstOffset);
        void CopyDataRaw(const void *data, size_t size, size_t offset = 0);

        Impl *m_impl;
        size_t m_size; // mirrored to avoid Impl hop on hot getters
        void *m_data;  // mirrored mapped pointer (nullptr when unmapped)
        PeBufferUsageFlags m_usage;
        PeMemoryUsage m_memoryUsage;
        std::string m_name;
        BufferTrackInfo m_trackInfo{};
    };
} // namespace pe
