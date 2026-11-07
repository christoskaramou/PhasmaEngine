#include "API/WebGPU/WebGpuImageImpl.h"
#include "API/Buffer.h"
#include "API/Command.h"
#include "API/RHI.h"
#include "API/StagingManager.h"
#include "API/WebGPU/WebGpuBlit.h"
#include "API/WebGPU/WebGpuBufferImpl.h"
#include "API/WebGPU/WebGpuCommandBufferImpl.h"
#include "API/WebGPU/WebGpuFormat.h"
#include "API/WebGPU/WebGpuRhiImpl.h"

namespace pe
{
    namespace
    {
        constexpr uint32_t kCopyRowAlignment = 256; // bytesPerRow of buffer <-> texture copies

        WGPUTextureViewDimension ToWgpuViewDimension(PeImageViewType type)
        {
            switch (type)
            {
            case PE_IMAGE_VIEW_TYPE_1D:
                return WGPUTextureViewDimension_1D;
            case PE_IMAGE_VIEW_TYPE_1D_ARRAY:
            case PE_IMAGE_VIEW_TYPE_2D_ARRAY:
                return WGPUTextureViewDimension_2DArray;
            case PE_IMAGE_VIEW_TYPE_CUBE:
                return WGPUTextureViewDimension_Cube;
            case PE_IMAGE_VIEW_TYPE_CUBE_ARRAY:
                return WGPUTextureViewDimension_CubeArray;
            case PE_IMAGE_VIEW_TYPE_3D:
                return WGPUTextureViewDimension_3D;
            default:
                return WGPUTextureViewDimension_2D;
            }
        }

        WGPUTextureDimension ToWgpuDimension(PeImageType type)
        {
            switch (type)
            {
            case PE_IMAGE_TYPE_1D:
                return WGPUTextureDimension_1D;
            case PE_IMAGE_TYPE_3D:
                return WGPUTextureDimension_3D;
            default:
                return WGPUTextureDimension_2D;
            }
        }

        WGPUTextureAspect ToWgpuAspect(PeImageAspectFlags aspect)
        {
            const bool depth = aspect & PE_IMAGE_ASPECT_DEPTH;
            const bool stencil = aspect & PE_IMAGE_ASPECT_STENCIL;
            if (depth && !stencil)
                return WGPUTextureAspect_DepthOnly;

            if (stencil && !depth)
                return WGPUTextureAspect_StencilOnly;

            return WGPUTextureAspect_All;
        }

        // Views of a depth-stencil texture read depth; attachments and color textures use every aspect.
        PeImageAspectFlags ViewAspect(WGPUTextureFormat format, bool attachment)
        {
            if (!IsDepthOrStencil(format))
                return PE_IMAGE_ASPECT_COLOR;

            if (attachment)
                return PE_IMAGE_ASPECT_DEPTH | PE_IMAGE_ASPECT_STENCIL;

            return format == WGPUTextureFormat_Stencil8 ? PE_IMAGE_ASPECT_STENCIL : PE_IMAGE_ASPECT_DEPTH;
        }

        WGPUTextureUsage ToWgpuUsage(PeImageUsageFlags usage, PeSampleCount samples)
        {
            WGPUTextureUsage out = WGPUTextureUsage_None;
            if (samples == PE_SAMPLE_COUNT_1) // multisampled textures cannot be copied
                out |= WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst;

            if (usage & (PE_IMAGE_USAGE_SAMPLED | PE_IMAGE_USAGE_INPUT_ATTACHMENT))
                out |= WGPUTextureUsage_TextureBinding;

            if (usage & PE_IMAGE_USAGE_STORAGE)
                out |= WGPUTextureUsage_StorageBinding;

            if (usage & (PE_IMAGE_USAGE_COLOR_ATTACHMENT | PE_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT))
                out |= WGPUTextureUsage_RenderAttachment;

            return out;
        }

        uint32_t MipExtent(uint32_t extent, uint32_t mip)
        {
            return std::max(1u, extent >> mip);
        }

        uint32_t AlignUp(uint32_t value, uint32_t alignment)
        {
            return (value + alignment - 1) / alignment * alignment;
        }

        WGPUCommandEncoder EncoderOf(CommandBuffer *cmd)
        {
            PE_ERROR_IF(!cmd, "WebGPU image copy: no command buffer specified");
            return WebGpuCommandBufferImpl::From(cmd)->GetEncoder();
        }
    } // namespace

    WebGpuImageImpl::WebGpuImageImpl(Image *owner)
        : m_owner{owner}
    {
    }

    WebGpuImageImpl::WebGpuImageImpl(Image *owner, const ImageDesc &desc)
        : m_owner{owner},
          m_format{ToWgpuFormat(desc.format)},
          m_ownsTexture{true}
    {
        PE_ERROR_IF(m_format == WGPUTextureFormat_Undefined,
                    "WebGpuImageImpl: '%s' has a format WebGPU lacks (%u)",
                    desc.name.c_str(),
                    static_cast<uint32_t>(desc.format));

        const bool is3D = desc.imageType == PE_IMAGE_TYPE_3D;
        WGPUTextureDescriptor textureDesc{};
        textureDesc.label = {desc.name.c_str(), desc.name.size()};
        textureDesc.usage = ToWgpuUsage(desc.usage, desc.samples);

        // Mip chains and blits render into the image with the WGSL blit (decision 5), so they need RenderAttachment.
        const bool blitTarget = desc.mipLevels > 1 || (desc.usage & PE_IMAGE_USAGE_TRANSFER_DST);
        const bool singleSample = desc.samples == PE_SAMPLE_COUNT_1;
        if (blitTarget && singleSample && IsBlittable(m_format, WebGpuRhiImpl::Get()->m_float32Filterable))
            textureDesc.usage |= WGPUTextureUsage_RenderAttachment;
        textureDesc.dimension = ToWgpuDimension(desc.imageType);
        textureDesc.size = {desc.width, desc.height, is3D ? desc.depth : desc.arrayLayers};
        textureDesc.format = m_format;
        textureDesc.mipLevelCount = desc.mipLevels;
        textureDesc.sampleCount = 1u << static_cast<uint32_t>(desc.samples); // PeSampleCount is an index: 1, 2, 4, ...

        // mutableFormat images get their sRGB twin as a view format (decision 5).
        const WGPUTextureFormat srgb = ToWgpuFormat(Image::SrgbFormatOf(desc.format));
        if (desc.mutableFormat && srgb != WGPUTextureFormat_Undefined)
        {
            textureDesc.viewFormatCount = 1;
            textureDesc.viewFormats = &srgb;
        }

        m_texture = wgpuDeviceCreateTexture(WebGpuRhiImpl::Get()->m_device, &textureDesc);
        PE_ERROR_IF(!m_texture, "WebGpuImageImpl: failed to create texture '%s'", desc.name.c_str());
    }

    WebGpuImageImpl::~WebGpuImageImpl()
    {
        if (!m_texture)
            return;

        if (m_ownsTexture)
            wgpuTextureDestroy(m_texture);

        wgpuTextureRelease(m_texture);
    }

    void WebGpuImageImpl::SetSwapchainTexture(WGPUTexture texture)
    {
        if (m_texture)
            wgpuTextureRelease(m_texture);

        m_texture = texture;
        m_format = texture ? wgpuTextureGetFormat(texture) : WGPUTextureFormat_Undefined;
        if (m_owner->HasRTV())
            WebGpuImageViewImpl::From(m_owner->GetRTV())->Rebuild();
    }

    void WebGpuImageImpl::CopyImage(CommandBuffer *cmd, Image *src)
    {
        Image *dst = m_owner;
        PE_ERROR_IF(!src, "WebGpuImageImpl::CopyImage: null source image");
        const bool sameShape = dst->GetWidth() == src->GetWidth() &&
                               dst->GetHeight() == src->GetHeight() &&
                               dst->GetMipLevels() == src->GetMipLevels() &&
                               dst->GetArrayLayers() == src->GetArrayLayers();
        PE_ERROR_IF(!sameShape, "WebGpuImageImpl::CopyImage: image shapes differ");

        WGPUCommandEncoder encoder = EncoderOf(cmd);
        const bool is3D = dst->m_imageType == PE_IMAGE_TYPE_3D;
        for (uint32_t mip = 0; mip < dst->GetMipLevels(); ++mip)
        {
            WGPUTexelCopyTextureInfo from{};
            from.texture = From(src)->m_texture;
            from.mipLevel = mip;
            from.aspect = WGPUTextureAspect_All;

            WGPUTexelCopyTextureInfo to = from;
            to.texture = m_texture;

            const uint32_t layers = is3D ? MipExtent(dst->m_depth, mip) : dst->GetArrayLayers();
            const WGPUExtent3D size{MipExtent(dst->GetWidth(), mip), MipExtent(dst->GetHeight(), mip), layers};
            wgpuCommandEncoderCopyTextureToTexture(encoder, &from, &to, &size);
        }
    }

    void WebGpuImageImpl::CopyToBuffer(CommandBuffer *, Buffer *, uint32_t, uint32_t, uint32_t)
    {
        // Its callers (screenshots, ImageCapture, perception readback) map the buffer afterwards (decision 5).
        PE_ERROR("WebGpuImageImpl::CopyToBuffer: GPU readback is not available on the web yet");
    }

    void WebGpuImageImpl::Blit(CommandBuffer *cmd, Image *src, const ImageBlit &region, PeFilter filter)
    {
        PE_ERROR_IF(!src, "WebGpuImageImpl::Blit: null source image");
        WebGpuImageImpl *source = From(src);
        const bool float32Filterable = WebGpuRhiImpl::Get()->m_float32Filterable;
        const bool blittable = IsBlittable(m_format, float32Filterable) && IsBlittable(source->m_format, float32Filterable);
        PE_ERROR_IF(!blittable,
                    "WebGpuImageImpl::Blit: '%s' -> '%s' has a format the WGSL blit cannot sample or render",
                    src->GetName().c_str(),
                    m_owner->GetName().c_str());
        PE_ERROR_IF(region.srcSubresource.layerCount != region.dstSubresource.layerCount,
                    "WebGpuImageImpl::Blit: source and destination layer counts differ");

        WGPUCommandEncoder encoder = EncoderOf(cmd);
        for (uint32_t layer = 0; layer < region.dstSubresource.layerCount; ++layer)
        {
            WebGpuBlitRegion blit{};
            blit.srcMip = region.srcSubresource.mipLevel;
            blit.srcLayer = region.srcSubresource.baseArrayLayer + layer;
            blit.srcMin[0] = static_cast<float>(region.srcOffsets[0].x);
            blit.srcMin[1] = static_cast<float>(region.srcOffsets[0].y);
            blit.srcMax[0] = static_cast<float>(region.srcOffsets[1].x);
            blit.srcMax[1] = static_cast<float>(region.srcOffsets[1].y);
            blit.dstMip = region.dstSubresource.mipLevel;
            blit.dstLayer = region.dstSubresource.baseArrayLayer + layer;
            blit.dstFrom[0] = region.dstOffsets[0].x;
            blit.dstFrom[1] = region.dstOffsets[0].y;
            blit.dstTo[0] = region.dstOffsets[1].x;
            blit.dstTo[1] = region.dstOffsets[1].y;
            WebGpuBlitTexture(encoder, source->m_texture, m_texture, blit, filter == PE_FILTER_LINEAR, false);
        }
    }

    void WebGpuImageImpl::GenerateMipMaps(CommandBuffer *cmd)
    {
        Image *image = m_owner;
        if (image->m_mipmapsGenerated || image->GetMipLevels() < 2)
            return;

        // SPD is not used on the web (decision 10): each mip is a linear blit of the one above it.
        PE_ERROR_IF(image->m_imageType == PE_IMAGE_TYPE_3D, "WebGpuImageImpl::GenerateMipMaps: 3D images are not supported");
        PE_ERROR_IF(!IsBlittable(m_format, WebGpuRhiImpl::Get()->m_float32Filterable),
                    "WebGpuImageImpl::GenerateMipMaps: '%s' has a format the WGSL blit cannot sample or render",
                    image->GetName().c_str());

        WGPUCommandEncoder encoder = EncoderOf(cmd);
        for (uint32_t layer = 0; layer < image->GetArrayLayers(); ++layer)
        {
            for (uint32_t mip = 1; mip < image->GetMipLevels(); ++mip)
            {
                WebGpuBlitRegion blit{};
                blit.srcMip = mip - 1;
                blit.srcLayer = layer;
                blit.srcMax[0] = static_cast<float>(MipExtent(image->GetWidth(), mip - 1));
                blit.srcMax[1] = static_cast<float>(MipExtent(image->GetHeight(), mip - 1));
                blit.dstMip = mip;
                blit.dstLayer = layer;
                blit.dstTo[0] = static_cast<int32_t>(MipExtent(image->GetWidth(), mip));
                blit.dstTo[1] = static_cast<int32_t>(MipExtent(image->GetHeight(), mip));
                WebGpuBlitTexture(encoder, m_texture, m_texture, blit, true, true);
            }
        }

        image->m_mipmapsGenerated = true;
    }

    void WebGpuImageImpl::CopyDataToImageStaged(CommandBuffer *cmd,
                                                void *data,
                                                size_t size,
                                                uint32_t baseArrayLayer,
                                                uint32_t layerCount,
                                                uint32_t mipLevel)
    {
        Image *image = m_owner;
        PE_ERROR_IF(!data, "WebGpuImageImpl::CopyDataToImageStaged: null source data");
        PE_ERROR_IF(image->GetSamples() != PE_SAMPLE_COUNT_1, "WebGpuImageImpl::CopyDataToImageStaged: multisampled upload");
        PE_ERROR_IF(mipLevel >= image->GetMipLevels(), "WebGpuImageImpl::CopyDataToImageStaged: mip level out of range");

        const WgpuTexelBlock block = TexelBlockOf(m_format);
        PE_ERROR_IF(!block.bytes, "WebGpuImageImpl::CopyDataToImageStaged: '%s' format cannot be uploaded", image->GetName().c_str());

        const bool is3D = image->m_imageType == PE_IMAGE_TYPE_3D;
        const uint32_t layers = layerCount ? layerCount : image->GetArrayLayers();
        PE_ERROR_IF(!is3D && baseArrayLayer + layers > image->GetArrayLayers(),
                    "WebGpuImageImpl::CopyDataToImageStaged: array layer range overflow");

        const uint32_t width = MipExtent(image->GetWidth(), mipLevel);
        const uint32_t height = MipExtent(image->GetHeight(), mipLevel);
        const uint32_t slices = is3D ? MipExtent(image->m_depth, mipLevel) : layers;
        const uint32_t blocksWide = (width + block.width - 1) / block.width;
        const uint32_t blocksHigh = (height + block.width - 1) / block.width;
        const uint32_t tightRow = blocksWide * block.bytes;
        const uint32_t alignedRow = AlignUp(tightRow, kCopyRowAlignment);
        const size_t tightSize = static_cast<size_t>(tightRow) * blocksHigh * slices;
        PE_ERROR_IF(size && size < tightSize,
                    "WebGpuImageImpl::CopyDataToImageStaged: %zu bytes given, %zu needed",
                    size,
                    tightSize);

        // Repack tightly packed rows into 256-byte-aligned rows (decision 5).
        const size_t stagedSize = static_cast<size_t>(alignedRow) * blocksHigh * slices;
        StagingAllocation alloc = RHII.GetStagingManager()->Allocate(stagedSize);
        const auto *src = static_cast<const uint8_t *>(data);
        auto *dst = static_cast<uint8_t *>(alloc.data);
        for (size_t row = 0; row < static_cast<size_t>(blocksHigh) * slices; ++row)
            std::memcpy(dst + row * alignedRow, src + row * tightRow, tightRow);

        alloc.buffer->Flush(stagedSize, 0);

        WGPUTexelCopyBufferInfo source{};
        source.buffer = WebGpuBufferImpl::From(alloc.buffer)->m_buffer;
        source.layout.offset = 0;
        source.layout.bytesPerRow = alignedRow;
        source.layout.rowsPerImage = blocksHigh;

        WGPUTexelCopyTextureInfo destination{};
        destination.texture = m_texture;
        destination.mipLevel = mipLevel;
        destination.origin = {0, 0, is3D ? 0 : baseArrayLayer};
        destination.aspect = WGPUTextureAspect_All;

        // Block-compressed copies cover whole blocks (the mip's physical size).
        const WGPUExtent3D extent{blocksWide * block.width, blocksHigh * block.width, slices};
        wgpuCommandEncoderCopyBufferToTexture(EncoderOf(cmd), &source, &destination, &extent);
        cmd->AddAfterWaitCallback([alloc = std::move(alloc)]()
                                  { RHII.GetStagingManager()->SetUnused(alloc); });
    }

    void WebGpuImageImpl::CreateRTV()
    {
        Image *image = m_owner;
        if (image->m_rtv)
        {
            CommandBuffer::ClearFramebufferCache();
            ImageView::Destroy(image->m_rtv);
        }

        // WebGPU attachments are one mip and one layer.
        ImageViewDesc viewDesc{};
        viewDesc.viewType = PE_IMAGE_VIEW_TYPE_2D;
        viewDesc.format = image->GetFormat();
        viewDesc.aspectMask = ViewAspect(m_format, true);
        image->m_rtv = ImageView::Create(image, viewDesc, image->GetName() + "_RTV");
    }

    void WebGpuImageImpl::CreateSRV(PeImageViewType type, int mip)
    {
        Image *image = m_owner;
        PE_ERROR_IF(!(image->GetUsage() & PE_IMAGE_USAGE_SAMPLED), "Image was not created with Sampled usage for SRV");

        ImageViewDesc viewDesc{};
        viewDesc.viewType = type;
        viewDesc.format = image->GetFormat();
        viewDesc.aspectMask = ViewAspect(m_format, false);
        viewDesc.baseMipLevel = mip == -1 ? 0 : static_cast<uint32_t>(mip);
        viewDesc.levelCount = mip == -1 ? image->GetMipLevels() : 1;
        viewDesc.layerCount = image->GetArrayLayers();
        ImageView *view = ImageView::Create(image, viewDesc, image->GetName() + "_SRV");

        ImageView *&slot = mip == -1 ? image->m_srv : image->m_srvs[mip];
        ImageView::Destroy(slot);
        slot = view;
    }

    void WebGpuImageImpl::CreateUAV(PeImageViewType type, uint32_t mip)
    {
        Image *image = m_owner;
        PE_ERROR_IF(!(image->GetUsage() & PE_IMAGE_USAGE_STORAGE), "Image was not created with Storage usage for UAV");

        ImageViewDesc viewDesc{};
        viewDesc.viewType = type;
        viewDesc.format = image->GetFormat();
        viewDesc.baseMipLevel = mip;
        viewDesc.layerCount = image->GetArrayLayers();
        ImageView::Destroy(image->m_uavs[mip]);
        image->m_uavs[mip] = ImageView::Create(image, viewDesc, image->GetName() + "_UAV_" + std::to_string(mip));
    }

    WebGpuImageViewImpl::WebGpuImageViewImpl(ImageView *owner, const ImageViewDesc &)
        : m_owner{owner}
    {
        Rebuild();
    }

    WebGpuImageViewImpl::~WebGpuImageViewImpl()
    {
        if (m_view)
            wgpuTextureViewRelease(m_view);
    }

    void WebGpuImageViewImpl::Rebuild()
    {
        if (m_view)
            wgpuTextureViewRelease(m_view);

        m_view = nullptr;
        WGPUTexture texture = WebGpuImageImpl::From(m_owner->GetParent())->m_texture;
        if (!texture)
            return;

        const ImageViewDesc &desc = m_owner->GetDesc();
        const WGPUTextureViewDimension dimension = ToWgpuViewDimension(desc.viewType);
        const bool is3D = dimension == WGPUTextureViewDimension_3D;

        WGPUTextureViewDescriptor viewDesc{};
        viewDesc.label = {m_owner->GetName().c_str(), m_owner->GetName().size()};
        viewDesc.format = ToWgpuFormat(desc.format);
        viewDesc.dimension = dimension;
        viewDesc.baseMipLevel = desc.baseMipLevel;
        viewDesc.mipLevelCount = desc.levelCount;
        viewDesc.baseArrayLayer = is3D ? 0 : desc.baseArrayLayer;
        viewDesc.arrayLayerCount = is3D ? 1 : desc.layerCount;
        viewDesc.aspect = ToWgpuAspect(desc.aspectMask);
        viewDesc.usage = WGPUTextureUsage_None; // inherit the texture's usage
        m_view = wgpuTextureCreateView(texture, &viewDesc);
    }
} // namespace pe
