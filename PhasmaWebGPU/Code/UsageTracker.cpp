#include "UsageTracker.h"
#include "BindGroup.h"
#include "Texture.h"
#include "FormatMap.h"

namespace pwgpu
{
    static const char *KindName(SubresourceUsageKind k)
    {
        switch (k)
        {
        case SubresourceUsageKind::Sampled:
            return "sampled-texture";
        case SubresourceUsageKind::ReadOnlyStorage:
            return "readonly-storage-texture";
        case SubresourceUsageKind::WriteOnlyStorage:
            return "writeonly-storage-texture";
        case SubresourceUsageKind::ReadWriteStorage:
            return "readwrite-storage-texture";
        case SubresourceUsageKind::AttachmentReadOnly:
            return "read-only-attachment";
        case SubresourceUsageKind::Attachment:
            return "render-attachment";
        default:
            return "none";
        }
    }

    static bool IsReadable(SubresourceUsageKind k)
    {
        return k == SubresourceUsageKind::Sampled ||
               k == SubresourceUsageKind::ReadOnlyStorage ||
               k == SubresourceUsageKind::AttachmentReadOnly;
    }

    static bool IsWritable(SubresourceUsageKind k)
    {
        return k == SubresourceUsageKind::WriteOnlyStorage ||
               k == SubresourceUsageKind::ReadWriteStorage;
    }

    static SubresourceUsageKind Merge(SubresourceUsageKind a, SubresourceUsageKind b,
                                      bool strictWritable)
    {
        if (a == SubresourceUsageKind::None)
            return b;
        if (b == SubresourceUsageKind::None)
            return a;
        if (a == b)
        {
            if (a == SubresourceUsageKind::Attachment)
                return SubresourceUsageKind::None;
            if (strictWritable && IsWritable(a))
                return SubresourceUsageKind::None;
            return a;
        }
        if (IsReadable(a) && IsReadable(b))
            return a;
        return SubresourceUsageKind::None;
    }

    bool UsageScope::AddSubresource(const SubresourceKey &k, SubresourceUsageKind kind,
                                    std::string &outErr)
    {
        auto it = map.find(k);
        if (it == map.end())
        {
            map.emplace(k, kind);
            return true;
        }
        SubresourceUsageKind merged = Merge(it->second, kind, strictWritableDuplicates);
        if (merged == SubresourceUsageKind::None)
        {
            outErr = "subresource used with incompatible usages in one pass: ";
            outErr += KindName(it->second);
            outErr += " + ";
            outErr += KindName(kind);
            return false;
        }
        it->second = merged;
        return true;
    }

    bool UsageScope::AddView(WGPUTextureViewImpl *view, SubresourceUsageKind kind,
                             std::string &outErr)
    {
        if (!view || !view->texture)
            return true;

        WGPUTextureAspect aspects[2];
        int aspectCount = 0;
        {
            const WGPUTextureFormat fmt = view->texture->format;
            const bool hasDepth = pwgpu::HasDepthAspect(fmt);
            const bool hasStencil = pwgpu::HasStencilAspect(fmt);
            if (view->aspect == WGPUTextureAspect_All)
            {
                if (hasDepth)
                    aspects[aspectCount++] = WGPUTextureAspect_DepthOnly;
                if (hasStencil)
                    aspects[aspectCount++] = WGPUTextureAspect_StencilOnly;
                if (!hasDepth && !hasStencil)
                    aspects[aspectCount++] = WGPUTextureAspect_All;
            }
            else
            {
                aspects[aspectCount++] = view->aspect;
            }
        }

        SubresourceKey key{};
        key.texture = view->texture;

        const uint32_t mipEnd = view->baseMipLevel + view->mipLevelCount;
        const uint32_t layerEnd = view->baseArrayLayer + view->arrayLayerCount;
        for (int a = 0; a < aspectCount; ++a)
        {
            key.aspect = static_cast<uint32_t>(aspects[a]);
            for (uint32_t m = view->baseMipLevel; m < mipEnd; ++m)
            {
                for (uint32_t l = view->baseArrayLayer; l < layerEnd; ++l)
                {
                    key.mip = m;
                    key.layer = l;
                    if (!AddSubresource(key, kind, outErr))
                        return false;
                }
            }
        }
        return true;
    }

    bool UsageScope::MergeFrom(const UsageScope &other, std::string &outErr)
    {
        for (auto &kv : other.map)
        {
            if (!AddSubresource(kv.first, kv.second, outErr))
                return false;
        }
        return true;
    }

} // namespace pwgpu
