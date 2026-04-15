#pragma once

#include <webgpu/webgpu.h>
#include <string>
#include <unordered_map>

struct WGPUTextureImpl;
struct WGPUTextureViewImpl;

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
        bool MergeFrom(const UsageScope &other, std::string &outErr);
        void Clear() { map.clear(); }

        std::unordered_map<SubresourceKey, SubresourceUsageKind, SubresourceKeyHash> map;
        bool strictWritableDuplicates = false;
    };

} // namespace pwgpu
