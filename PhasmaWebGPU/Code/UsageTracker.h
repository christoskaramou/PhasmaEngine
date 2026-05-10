#pragma once

#include <webgpu/webgpu.h>

struct WGPUTextureImpl;
struct WGPUTextureViewImpl;
struct WGPUBufferImpl;

namespace pwgpu
{
    enum class SubresourceUsageKind : uint8_t
    {
        None = 0,
        Sampled,
        ReadOnlyStorage,
        WriteOnlyStorage,
        ReadWriteStorage,
        AttachmentReadOnly,
        Attachment,
    };

    enum class BufferUsageKind : uint8_t
    {
        None = 0,
        Input = 1 << 0,       // INDEX | VERTEX | INDIRECT
        Constant = 1 << 1,    // UNIFORM binding
        StorageRead = 1 << 2, // read-only-storage binding
        Storage = 1 << 3,     // storage (read_write) binding
    };

    struct SubresourceKey
    {
        WGPUTextureImpl *texture = nullptr;
        uint32_t aspect = 0;
        uint32_t mip = 0;
        uint32_t layer = 0;

        bool operator==(const SubresourceKey &other) const
        {
            return texture == other.texture && aspect == other.aspect &&
                   mip == other.mip && layer == other.layer;
        }
    };

    struct SubresourceKeyHash
    {
        size_t operator()(const SubresourceKey &k) const noexcept
        {
            size_t h = std::hash<const void *>{}(k.texture);
            h = h * 1315423911u + k.aspect;
            h = h * 1315423911u + k.mip;
            h = h * 1315423911u + k.layer;
            return h;
        }
    };

    struct UsageScope
    {
        bool AddSubresource(const SubresourceKey &k, SubresourceUsageKind kind, std::string &outErr);
        bool AddView(WGPUTextureViewImpl *view, SubresourceUsageKind kind, std::string &outErr);
        bool AddBuffer(WGPUBufferImpl *buffer, BufferUsageKind kind, std::string &outErr);
        bool MergeFrom(const UsageScope &other, std::string &outErr);
        void Clear()
        {
            map.clear();
            bufferMap.clear();
        }

        std::unordered_map<SubresourceKey, SubresourceUsageKind, SubresourceKeyHash> map;
        std::unordered_map<WGPUBufferImpl *, uint8_t> bufferMap;
        bool strictWritableDuplicates = false;
    };

} // namespace pwgpu
