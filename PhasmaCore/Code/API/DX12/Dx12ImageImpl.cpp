#include "API/DX12/Dx12ImageImpl.h"

#include "API/Buffer.h"
#include "API/Command.h"
#include "API/DX12/Dx12BufferImpl.h"
#include "API/DX12/Dx12CommandBufferImpl.h"
#include "API/DX12/Dx12ImageViewImpl.h"
#include "API/DX12/Dx12RhiImpl.h"
#include "API/DX12/Dx12Translate.h"
#include "API/RHI.h"
#include "API/StagingManager.h"

#include <D3D12MemAlloc.h>

namespace pe
{
    namespace
    {
        bool IsDepthStencilFormat(DXGI_FORMAT format)
        {
            return format == DXGI_FORMAT_D32_FLOAT ||
                   format == DXGI_FORMAT_D24_UNORM_S8_UINT ||
                   format == DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
        }

        D3D12_RESOURCE_DIMENSION ToD3D12Dimension(PeImageType type)
        {
            switch (type)
            {
            case PE_IMAGE_TYPE_1D:
                return D3D12_RESOURCE_DIMENSION_TEXTURE1D;
            case PE_IMAGE_TYPE_2D:
                return D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            case PE_IMAGE_TYPE_3D:
                return D3D12_RESOURCE_DIMENSION_TEXTURE3D;
            default:
                return D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            }
        }

        D3D12_RESOURCE_FLAGS ToD3D12ResourceFlags(PeImageUsageFlags usage, DXGI_FORMAT format)
        {
            D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;
            if (usage & PE_IMAGE_USAGE_STORAGE)
                flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            if (usage & PE_IMAGE_USAGE_COLOR_ATTACHMENT)
                flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
            if ((usage & PE_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT) || IsDepthStencilFormat(format))
                flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
            return flags;
        }

        D3D12_RESOURCE_STATES ToInitialResourceState(PeImageLayout layout, PeImageUsageFlags usage)
        {
            switch (layout)
            {
            case PE_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            case PE_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL:
                return D3D12_RESOURCE_STATE_RENDER_TARGET;
            case PE_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
            case PE_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL:
            case PE_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL:
            case PE_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL:
            case PE_IMAGE_LAYOUT_STENCIL_ATTACHMENT_OPTIMAL:
                return D3D12_RESOURCE_STATE_DEPTH_WRITE;
            case PE_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
            case PE_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL:
            case PE_IMAGE_LAYOUT_STENCIL_READ_ONLY_OPTIMAL:
                return D3D12_RESOURCE_STATE_DEPTH_READ;
            case PE_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
                return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
            case PE_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
                return D3D12_RESOURCE_STATE_COPY_SOURCE;
            case PE_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
                return D3D12_RESOURCE_STATE_COPY_DEST;
            case PE_IMAGE_LAYOUT_PRESENT_SRC:
                return D3D12_RESOURCE_STATE_PRESENT;
            case PE_IMAGE_LAYOUT_GENERAL:
                return (usage & PE_IMAGE_USAGE_STORAGE) ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : D3D12_RESOURCE_STATE_COMMON;
            default:
                return D3D12_RESOURCE_STATE_COMMON;
            }
        }

        UINT ToD3D12SampleCount(PeSampleCount samples)
        {
            switch (samples)
            {
            case PE_SAMPLE_COUNT_1:
                return 1;
            case PE_SAMPLE_COUNT_2:
                return 2;
            case PE_SAMPLE_COUNT_4:
                return 4;
            case PE_SAMPLE_COUNT_8:
                return 8;
            case PE_SAMPLE_COUNT_16:
                return 16;
            default:
                return 1;
            }
        }

        D3D12_RESOURCE_DESC TextureResourceDesc(const ImageDesc &desc, DXGI_FORMAT resourceFormat)
        {
            D3D12_RESOURCE_DESC resource{};
            resource.Dimension = ToD3D12Dimension(desc.imageType);
            resource.Alignment = 0;
            resource.Width = desc.width;
            resource.Height = desc.height;
            resource.DepthOrArraySize = static_cast<UINT16>(desc.imageType == PE_IMAGE_TYPE_3D ? desc.depth : desc.arrayLayers);
            resource.MipLevels = static_cast<UINT16>(desc.mipLevels);
            resource.Format = resourceFormat;
            resource.SampleDesc.Count = ToD3D12SampleCount(desc.samples);
            resource.SampleDesc.Quality = 0;
            resource.Layout = desc.tilingLinear ? D3D12_TEXTURE_LAYOUT_ROW_MAJOR : D3D12_TEXTURE_LAYOUT_UNKNOWN;
            resource.Flags = ToD3D12ResourceFlags(desc.usage, resourceFormat);
            return resource;
        }

        std::optional<D3D12_CLEAR_VALUE> OptimizedClearValue(const ImageDesc &desc, DXGI_FORMAT viewFormat)
        {
            if (!desc.useOptimizedClearValue)
                return std::nullopt;

            if (desc.usage & PE_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT)
            {
                D3D12_CLEAR_VALUE clear{};
                clear.Format = viewFormat;
                clear.DepthStencil.Depth = desc.clearColor[0];
                clear.DepthStencil.Stencil = static_cast<UINT8>(desc.clearColor[1]);
                return clear;
            }

            if (desc.usage & PE_IMAGE_USAGE_COLOR_ATTACHMENT)
            {
                D3D12_CLEAR_VALUE clear{};
                clear.Format = viewFormat;
                clear.Color[0] = desc.clearColor[0];
                clear.Color[1] = desc.clearColor[1];
                clear.Color[2] = desc.clearColor[2];
                clear.Color[3] = desc.clearColor[3];
                return clear;
            }

            return std::nullopt;
        }

        PeImageAspectFlags AspectForFormat(DXGI_FORMAT format)
        {
            if (!IsDepthStencilFormat(format))
                return PE_IMAGE_ASPECT_COLOR;
            if (format == DXGI_FORMAT_D24_UNORM_S8_UINT || format == DXGI_FORMAT_D32_FLOAT_S8X24_UINT)
                return PE_IMAGE_ASPECT_DEPTH | PE_IMAGE_ASPECT_STENCIL;
            return PE_IMAGE_ASPECT_DEPTH;
        }

        UINT SubresourceIndex(const Image *image, uint32_t arrayLayer, uint32_t mipLevel)
        {
            return mipLevel + arrayLayer * image->GetMipLevels();
        }

        uint32_t MipExtent(uint32_t extent, uint32_t mipLevel)
        {
            return std::max(extent >> mipLevel, 1u);
        }

        void CopyTightlyPackedToFootprints(uint8_t *dst,
                                           const uint8_t *src,
                                           size_t srcSize,
                                           size_t dstSize,
                                           const D3D12_PLACED_SUBRESOURCE_FOOTPRINT *layouts,
                                           const UINT *rowCounts,
                                           const UINT64 *rowSizes,
                                           UINT subresourceCount)
        {
            size_t srcOffset = 0;
            for (UINT subresource = 0; subresource < subresourceCount; ++subresource)
            {
                const auto &layout = layouts[subresource];
                const UINT rows = rowCounts[subresource];
                const UINT64 rowSize = rowSizes[subresource];
                const UINT depth = std::max<UINT>(layout.Footprint.Depth, 1u);

                for (UINT z = 0; z < depth; ++z)
                {
                    for (UINT row = 0; row < rows; ++row)
                    {
                        PE_ERROR_IF(srcOffset + rowSize > srcSize, "Dx12ImageImpl::CopyDataToImageStaged: source data is smaller than the copy footprint");
                        PE_ERROR_IF(layout.Offset +
                                            static_cast<UINT64>(z) * layout.Footprint.RowPitch * layout.Footprint.Height +
                                            static_cast<UINT64>(row) * layout.Footprint.RowPitch + rowSize >
                                        dstSize,
                                    "Dx12ImageImpl::CopyDataToImageStaged: destination footprint exceeds staging allocation");
                        uint8_t *dstRow = dst + layout.Offset +
                                          static_cast<size_t>(z) * layout.Footprint.RowPitch * layout.Footprint.Height +
                                          static_cast<size_t>(row) * layout.Footprint.RowPitch;
                        std::memcpy(dstRow, src + srcOffset, static_cast<size_t>(rowSize));
                        srcOffset += static_cast<size_t>(rowSize);
                    }
                }
            }
        }

        void TransitionForCopy(CommandBuffer *cmd, Image *image, PeImageLayout layout)
        {
            ImageBarrierInfo barrier{};
            barrier.image = image;
            barrier.layout = layout;
            barrier.stageFlags = PE_STAGE_TRANSFER;
            barrier.accessMask = layout == PE_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL ? PE_ACCESS_TRANSFER_READ : PE_ACCESS_TRANSFER_WRITE;
            cmd->ImageBarrier(barrier);
            Dx12CommandBufferImpl::From(cmd)->FlushBarriers();
        }
    } // namespace

    Dx12ImageImpl::Dx12ImageImpl(Image *owner, const ImageDesc &desc)
        : m_owner{owner}
    {
        auto *rhi = static_cast<Dx12RhiImpl *>(RHII.GetImpl());
        PE_ERROR_IF(!rhi || !rhi->GetAllocator(), "Dx12ImageImpl requires an initialized DX12 allocator");

        m_viewFormat = pe_dx12::Format(desc.format);
        PE_ERROR_IF(m_viewFormat == DXGI_FORMAT_UNKNOWN, "Dx12ImageImpl: unsupported image format");

        m_resourceFormat = IsDepthStencilFormat(m_viewFormat) ? pe_dx12::DepthToTypeless(m_viewFormat) : m_viewFormat;
        m_state = ToInitialResourceState(desc.initialLayout, desc.usage);

        D3D12_RESOURCE_DESC resourceDesc = TextureResourceDesc(desc, m_resourceFormat);
        std::optional<D3D12_CLEAR_VALUE> clear = OptimizedClearValue(desc, m_viewFormat);
        m_needsFirstUseDiscard = (resourceDesc.Flags & (D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)) != 0;
        if (resourceDesc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)
            m_firstUseDiscardState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        else if (resourceDesc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET)
            m_firstUseDiscardState = D3D12_RESOURCE_STATE_RENDER_TARGET;

        D3D12MA::ALLOCATION_DESC allocationDesc{};
        allocationDesc.HeapType = D3D12_HEAP_TYPE_DEFAULT;

        HRESULT hr = rhi->GetAllocator()->CreateResource(&allocationDesc,
                                                         &resourceDesc,
                                                         m_state,
                                                         clear ? &*clear : nullptr,
                                                         &m_allocation,
                                                         IID_PPV_ARGS(&m_resource));
        PE_ERROR_IF(FAILED(hr), "Dx12ImageImpl: CreateResource failed (0x%08X)", static_cast<unsigned>(hr));

        if (!desc.name.empty())
        {
            std::wstring name(desc.name.begin(), desc.name.end());
            m_resource->SetName(name.c_str());
            m_allocation->SetName(name.c_str());
        }
    }

    Dx12ImageImpl::Dx12ImageImpl(Image *owner,
                                 ID3D12Resource *externalResource,
                                 DXGI_FORMAT format,
                                 D3D12_RESOURCE_STATES initialState)
        : m_owner{owner},
          m_resource{externalResource},
          m_resourceFormat{format},
          m_viewFormat{format},
          m_state{initialState}
    {
        PE_ERROR_IF(!externalResource, "Dx12ImageImpl: external swapchain resource is null");
    }

    Dx12ImageImpl::~Dx12ImageImpl()
    {
        m_resource.Reset();
        m_allocation.Reset();
    }

    Image::Impl *CreateDx12SwapchainImageImpl(Image *owner, ID3D12Resource *externalResource, DXGI_FORMAT format)
    {
        return new Dx12ImageImpl(owner, externalResource, format, D3D12_RESOURCE_STATE_PRESENT);
    }

    void Dx12ImageImpl::CopyImage(CommandBuffer *cmd, Image *src)
    {
        Image *dst = m_owner;
        PE_ERROR_IF(!cmd, "Dx12ImageImpl::CopyImage: no command buffer specified");
        PE_ERROR_IF(!src, "Dx12ImageImpl::CopyImage: null source image");
        PE_ERROR_IF(dst->GetWidth() != src->GetWidth() || dst->GetHeight() != src->GetHeight() ||
                        dst->GetMipLevels() != src->GetMipLevels() || dst->GetArrayLayers() != src->GetArrayLayers(),
                    "Dx12ImageImpl::CopyImage: image shapes differ");

        TransitionForCopy(cmd, src, PE_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        TransitionForCopy(cmd, dst, PE_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        ID3D12GraphicsCommandList *list = GetDx12CommandList(cmd);
        const uint32_t mipLevels = dst->GetMipLevels();
        const uint32_t arrayLayers = dst->GetArrayLayers();
        for (uint32_t layer = 0; layer < arrayLayers; ++layer)
        {
            for (uint32_t mip = 0; mip < mipLevels; ++mip)
            {
                D3D12_TEXTURE_COPY_LOCATION source{};
                source.pResource = Dx12ImageImpl::From(src)->GetResource();
                source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                source.SubresourceIndex = SubresourceIndex(src, layer, mip);

                D3D12_TEXTURE_COPY_LOCATION dest{};
                dest.pResource = m_resource.Get();
                dest.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                dest.SubresourceIndex = SubresourceIndex(dst, layer, mip);

                list->CopyTextureRegion(&dest, 0, 0, 0, &source, nullptr);
            }
        }
    }

    void Dx12ImageImpl::CopyToBuffer(CommandBuffer *cmd, Buffer *dst)
    {
        Image *src = m_owner;
        PE_ERROR_IF(!cmd, "Dx12ImageImpl::CopyToBuffer: no command buffer specified");
        PE_ERROR_IF(!dst, "Dx12ImageImpl::CopyToBuffer: null destination buffer");
        PE_ERROR_IF(src->GetSamples() != PE_SAMPLE_COUNT_1, "Dx12ImageImpl::CopyToBuffer: multisampled image readback is not supported yet");
        PE_ERROR_IF(IsDepthStencilFormat(m_viewFormat), "Dx12ImageImpl::CopyToBuffer: depth/stencil image readback is not supported yet");

        TransitionForCopy(cmd, src, PE_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

        ID3D12Device *device = static_cast<Dx12RhiImpl *>(RHII.GetImpl())->GetDevice();
        const D3D12_RESOURCE_DESC desc = m_resource->GetDesc();

        D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout{};
        UINT rowCount = 0;
        UINT64 rowSize = 0;
        UINT64 totalBytes = 0;
        device->GetCopyableFootprints(&desc, 0, 1, 0, &layout, &rowCount, &rowSize, &totalBytes);
        PE_ERROR_IF(totalBytes > dst->Size(), "Dx12ImageImpl::CopyToBuffer: destination buffer is too small");

        D3D12_TEXTURE_COPY_LOCATION source{};
        source.pResource = m_resource.Get();
        source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        source.SubresourceIndex = 0;

        D3D12_TEXTURE_COPY_LOCATION dest{};
        dest.pResource = Dx12BufferImpl::From(dst)->GetResource();
        dest.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dest.PlacedFootprint = layout;

        GetDx12CommandList(cmd)->CopyTextureRegion(&dest, 0, 0, 0, &source, nullptr);

        BufferTrackInfo &trackInfo = dst->GetTrackInfo();
        trackInfo.stageMask = PE_STAGE_TRANSFER;
        trackInfo.accessMask = PE_ACCESS_TRANSFER_WRITE;
    }

    void Dx12ImageImpl::Blit(CommandBuffer *cmd, Image *src, const ImageBlit &region, PeFilter /*filter*/)
    {
        Image *dst = m_owner;
        PE_ERROR_IF(!cmd, "Dx12ImageImpl::Blit: no command buffer specified");
        PE_ERROR_IF(!src, "Dx12ImageImpl::Blit: null source image");
        PE_ERROR_IF(src->GetSamples() != PE_SAMPLE_COUNT_1 || dst->GetSamples() != PE_SAMPLE_COUNT_1,
                    "Dx12ImageImpl::Blit: multisampled blits are not supported");

        const uint32_t srcMip = region.srcSubresource.mipLevel;
        const uint32_t dstMip = region.dstSubresource.mipLevel;
        PE_ERROR_IF(srcMip >= src->GetMipLevels() || dstMip >= dst->GetMipLevels(),
                    "Dx12ImageImpl::Blit: mip level out of range (src %u/%u, dst %u/%u)",
                    srcMip, src->GetMipLevels(), dstMip, dst->GetMipLevels());

        const uint32_t srcLayerCount = region.srcSubresource.layerCount ? region.srcSubresource.layerCount : 1u;
        const uint32_t dstLayerCount = region.dstSubresource.layerCount ? region.dstSubresource.layerCount : 1u;
        PE_ERROR_IF(srcLayerCount != dstLayerCount,
                    "Dx12ImageImpl::Blit: layer count mismatch (src %u, dst %u)",
                    srcLayerCount, dstLayerCount);
        PE_ERROR_IF(srcLayerCount > src->GetArrayLayers() ||
                        dstLayerCount > dst->GetArrayLayers() ||
                        region.srcSubresource.baseArrayLayer > src->GetArrayLayers() - srcLayerCount ||
                        region.dstSubresource.baseArrayLayer > dst->GetArrayLayers() - dstLayerCount,
                    "Dx12ImageImpl::Blit: array layer range overflow");

        const Offset3D &srcMin = region.srcOffsets[0];
        const Offset3D &srcMax = region.srcOffsets[1];
        const Offset3D &dstMin = region.dstOffsets[0];
        const Offset3D &dstMax = region.dstOffsets[1];

        const int32_t srcW = srcMax.x - srcMin.x;
        const int32_t srcH = srcMax.y - srcMin.y;
        const int32_t srcBack = std::max(srcMax.z, srcMin.z + 1);
        const int32_t dstBack = std::max(dstMax.z, dstMin.z + 1);
        const int32_t srcD = srcBack - srcMin.z;
        const int32_t dstW = dstMax.x - dstMin.x;
        const int32_t dstH = dstMax.y - dstMin.y;
        const int32_t dstD = dstBack - dstMin.z;

        PE_ERROR_IF(srcMin.x < 0 || srcMin.y < 0 || srcMin.z < 0 ||
                        dstMin.x < 0 || dstMin.y < 0 || dstMin.z < 0,
                    "Dx12ImageImpl::Blit: negative offsets are not supported");
        PE_ERROR_IF(srcW <= 0 || srcH <= 0 || dstW <= 0 || dstH <= 0,
                    "Dx12ImageImpl::Blit: degenerate region (src %dx%d, dst %dx%d)",
                    srcW, srcH, dstW, dstH);
        PE_ERROR_IF(srcMax.z < srcMin.z || dstMax.z < dstMin.z,
                    "Dx12ImageImpl::Blit: invalid depth range");
        PE_ERROR_IF(srcW != dstW || srcH != dstH || srcD != dstD,
                    "Dx12ImageImpl::Blit: scaling blits are not supported on DX12 yet "
                    "(src %dx%dx%d, dst %dx%dx%d) - needs the shader-blit slice",
                    srcW, srcH, srcD, dstW, dstH, dstD);

        const Dx12ImageImpl *srcImpl = Dx12ImageImpl::From(src);
        PE_ERROR_IF(IsDepthStencilFormat(srcImpl->m_viewFormat) || IsDepthStencilFormat(m_viewFormat),
                    "Dx12ImageImpl::Blit: depth/stencil blits are not supported on DX12 yet");
        PE_ERROR_IF(static_cast<uint32_t>(srcMax.x) > MipExtent(src->GetWidth(), srcMip) ||
                        static_cast<uint32_t>(srcMax.y) > MipExtent(src->GetHeight(), srcMip) ||
                        static_cast<uint32_t>(srcBack) > MipExtent(src->m_depth, srcMip) ||
                        static_cast<uint32_t>(dstMax.x) > MipExtent(dst->GetWidth(), dstMip) ||
                        static_cast<uint32_t>(dstMax.y) > MipExtent(dst->GetHeight(), dstMip) ||
                        static_cast<uint32_t>(dstBack) > MipExtent(dst->m_depth, dstMip),
                    "Dx12ImageImpl::Blit: region exceeds source or destination mip extent");
        PE_ERROR_IF(srcImpl->m_resourceFormat != m_resourceFormat,
                    "Dx12ImageImpl::Blit: format-converting blit not supported on DX12 yet "
                    "(src '%s' fmt=%u, dst '%s' fmt=%u) - needs the shader-blit slice",
                    src->GetName().c_str(), static_cast<uint32_t>(srcImpl->m_resourceFormat),
                    dst->GetName().c_str(), static_cast<uint32_t>(m_resourceFormat));

        cmd->BeginDebugRegion("BlitImage");

        std::vector<ImageBarrierInfo> barriers(2);
        barriers[0].image = dst;
        barriers[0].stageFlags = PE_STAGE_TRANSFER;
        barriers[0].accessMask = PE_ACCESS_TRANSFER_WRITE;
        barriers[0].layout = PE_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barriers[0].baseArrayLayer = region.dstSubresource.baseArrayLayer;
        barriers[0].arrayLayers = dstLayerCount;
        barriers[0].baseMipLevel = dstMip;
        barriers[0].mipLevels = 1;

        barriers[1].image = src;
        barriers[1].stageFlags = PE_STAGE_TRANSFER;
        barriers[1].accessMask = PE_ACCESS_TRANSFER_READ;
        barriers[1].layout = PE_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barriers[1].baseArrayLayer = region.srcSubresource.baseArrayLayer;
        barriers[1].arrayLayers = srcLayerCount;
        barriers[1].baseMipLevel = srcMip;
        barriers[1].mipLevels = 1;

        Image::Barriers(cmd, barriers);
        Dx12CommandBufferImpl::From(cmd)->FlushBarriers();

        ID3D12GraphicsCommandList *list = GetDx12CommandList(cmd);

        D3D12_BOX srcBox{};
        srcBox.left = static_cast<UINT>(srcMin.x);
        srcBox.top = static_cast<UINT>(srcMin.y);
        srcBox.front = static_cast<UINT>(srcMin.z);
        srcBox.right = static_cast<UINT>(srcMax.x);
        srcBox.bottom = static_cast<UINT>(srcMax.y);
        srcBox.back = static_cast<UINT>(srcBack);

        for (uint32_t i = 0; i < srcLayerCount; ++i)
        {
            D3D12_TEXTURE_COPY_LOCATION source{};
            source.pResource = srcImpl->GetResource();
            source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            source.SubresourceIndex = SubresourceIndex(src,
                                                       region.srcSubresource.baseArrayLayer + i,
                                                       srcMip);

            D3D12_TEXTURE_COPY_LOCATION dest{};
            dest.pResource = m_resource.Get();
            dest.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            dest.SubresourceIndex = SubresourceIndex(dst,
                                                     region.dstSubresource.baseArrayLayer + i,
                                                     dstMip);

            list->CopyTextureRegion(&dest,
                                    static_cast<UINT>(dstMin.x),
                                    static_cast<UINT>(dstMin.y),
                                    static_cast<UINT>(dstMin.z),
                                    &source,
                                    &srcBox);
        }

        cmd->EndDebugRegion();
    }

    void Dx12ImageImpl::CopyDataToImageStaged(CommandBuffer *cmd,
                                              void *data,
                                              size_t size,
                                              uint32_t baseArrayLayer,
                                              uint32_t layerCount,
                                              uint32_t mipLevel)
    {
        Image *image = m_owner;
        PE_ERROR_IF(!cmd, "Dx12ImageImpl::CopyDataToImageStaged: no command buffer specified");
        PE_ERROR_IF(!data, "Dx12ImageImpl::CopyDataToImageStaged: null source data");
        PE_ERROR_IF(image->GetSamples() != PE_SAMPLE_COUNT_1, "Dx12ImageImpl::CopyDataToImageStaged: multisampled image upload is not supported yet");
        PE_ERROR_IF(IsDepthStencilFormat(m_viewFormat), "Dx12ImageImpl::CopyDataToImageStaged: depth/stencil image upload is not supported yet");

        const uint32_t layers = layerCount ? layerCount : image->GetArrayLayers();
        PE_ERROR_IF(baseArrayLayer + layers > image->GetArrayLayers(), "Dx12ImageImpl::CopyDataToImageStaged: array layer range overflow");
        PE_ERROR_IF(mipLevel >= image->GetMipLevels(), "Dx12ImageImpl::CopyDataToImageStaged: mip level out of range");

        ID3D12Device *device = static_cast<Dx12RhiImpl *>(RHII.GetImpl())->GetDevice();
        const D3D12_RESOURCE_DESC desc = m_resource->GetDesc();

        std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> layouts(layers);
        std::vector<UINT> rowCounts(layers);
        std::vector<UINT64> rowSizes(layers);
        UINT64 nextOffset = 0;
        for (uint32_t i = 0; i < layers; ++i)
        {
            const UINT subresource = SubresourceIndex(image, baseArrayLayer + i, mipLevel);
            UINT64 requiredBytes = 0;
            device->GetCopyableFootprints(&desc,
                                          subresource,
                                          1,
                                          nextOffset,
                                          &layouts[i],
                                          &rowCounts[i],
                                          &rowSizes[i],
                                          &requiredBytes);
            const UINT64 footprintEnd = layouts[i].Offset +
                                        static_cast<UINT64>(std::max<UINT>(layouts[i].Footprint.Depth, 1u)) *
                                            layouts[i].Footprint.RowPitch * layouts[i].Footprint.Height;
            nextOffset = std::max(requiredBytes, footprintEnd);
        }

        StagingAllocation alloc = RHII.GetStagingManager()->Allocate(static_cast<size_t>(nextOffset));
        CopyTightlyPackedToFootprints(static_cast<uint8_t *>(alloc.data),
                                      static_cast<const uint8_t *>(data),
                                      size,
                                      static_cast<size_t>(nextOffset),
                                      layouts.data(),
                                      rowCounts.data(),
                                      rowSizes.data(),
                                      layers);
        alloc.buffer->Flush(static_cast<size_t>(nextOffset), 0);

        TransitionForCopy(cmd, image, PE_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        ID3D12GraphicsCommandList *list = GetDx12CommandList(cmd);
        for (uint32_t i = 0; i < layers; ++i)
        {
            D3D12_TEXTURE_COPY_LOCATION source{};
            source.pResource = Dx12BufferImpl::From(alloc.buffer)->GetResource();
            source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            source.PlacedFootprint = layouts[i];

            D3D12_TEXTURE_COPY_LOCATION dest{};
            dest.pResource = m_resource.Get();
            dest.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            dest.SubresourceIndex = SubresourceIndex(image, baseArrayLayer + i, mipLevel);

            list->CopyTextureRegion(&dest, 0, 0, 0, &source, nullptr);
        }

        cmd->AddAfterWaitCallback([alloc = std::move(alloc)]()
                                  { RHII.GetStagingManager()->SetUnused(alloc); });
    }

    void Dx12ImageImpl::CreateRTV()
    {
        Image *image = m_owner;
        PE_ERROR_IF(!(image->GetUsage() & PE_IMAGE_USAGE_COLOR_ATTACHMENT ||
                      image->GetUsage() & PE_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT),
                    "Image was not created with ColorAttachment or DepthStencilAttachment for RTV usage");

        if (image->m_rtv)
        {
            PE_INFO("Image::CreateRTV: RTV already exists, recreating.");
            ImageView::Destroy(image->m_rtv);
        }

        ImageViewDesc viewInfo{};
        viewInfo.viewType = image->m_imageType == PE_IMAGE_TYPE_3D
                                ? PE_IMAGE_VIEW_TYPE_3D
                                : PE_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = image->GetFormat();
        viewInfo.aspectMask = AspectForFormat(m_viewFormat);
        viewInfo.baseMipLevel = 0;
        viewInfo.levelCount = image->GetMipLevels();
        viewInfo.baseArrayLayer = 0;
        viewInfo.layerCount = image->m_imageType == PE_IMAGE_TYPE_3D
                                  ? image->m_depth
                                  : image->GetArrayLayers();

        const Dx12ImageViewKind kind = (image->GetUsage() & PE_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT)
                                           ? Dx12ImageViewKind::Dsv
                                           : Dx12ImageViewKind::Rtv;
        image->m_rtv = Dx12ImageViewImpl::Create(image, viewInfo, kind, image->GetName() + "_RTV");
    }

    void Dx12ImageImpl::CreateSRV(PeImageViewType type, int mip)
    {
        Image *image = m_owner;
        PE_ERROR_IF(!(image->GetUsage() & PE_IMAGE_USAGE_SAMPLED), "Image was not created with Sampled usage for SRV");

        ImageViewDesc viewInfo{};
        viewInfo.viewType = type;
        viewInfo.format = image->GetFormat();
        viewInfo.aspectMask = AspectForFormat(m_viewFormat);
        viewInfo.baseMipLevel = mip == -1 ? 0 : static_cast<uint32_t>(mip);
        viewInfo.levelCount = mip == -1 ? image->GetMipLevels() : 1;
        viewInfo.baseArrayLayer = 0;
        viewInfo.layerCount = image->GetArrayLayers();
        ImageView *view = Dx12ImageViewImpl::Create(image, viewInfo, Dx12ImageViewKind::Srv, image->GetName() + "_SRV");

        if (mip == -1)
        {
            if (image->m_srv)
            {
                PE_INFO("Image::CreateSRV: SRV already exists, recreating.");
                ImageView::Destroy(image->m_srv);
            }
            image->m_srv = view;
        }
        else
        {
            if (image->m_srvs[mip])
            {
                PE_INFO("Image::CreateSRV: SRV for mip {} already exists, recreating.", mip);
                ImageView::Destroy(image->m_srvs[mip]);
            }
            image->m_srvs[mip] = view;
        }
    }

    void Dx12ImageImpl::CreateUAV(PeImageViewType type, uint32_t mip)
    {
        Image *image = m_owner;
        PE_ERROR_IF(!(image->GetUsage() & PE_IMAGE_USAGE_STORAGE), "Image was not created with Storage usage for UAV");
        if (image->m_uavs[mip])
        {
            PE_INFO("Image::CreateUAV: UAV for mip {} already exists, recreating.", mip);
            ImageView::Destroy(image->m_uavs[mip]);
        }

        ImageViewDesc viewInfo{};
        viewInfo.viewType = type;
        viewInfo.format = image->GetFormat();
        viewInfo.aspectMask = PE_IMAGE_ASPECT_COLOR;
        viewInfo.baseMipLevel = mip;
        viewInfo.levelCount = 1;
        viewInfo.baseArrayLayer = 0;
        viewInfo.layerCount = image->GetArrayLayers();
        image->m_uavs[mip] = Dx12ImageViewImpl::Create(image, viewInfo, Dx12ImageViewKind::Uav, image->GetName() + "_UAV");
    }
} // namespace pe
