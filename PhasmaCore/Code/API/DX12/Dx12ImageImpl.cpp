#include "API/DX12/Dx12ImageImpl.h"

#include "API/DX12/Dx12ImageViewImpl.h"
#include "API/DX12/Dx12RhiImpl.h"
#include "API/DX12/Dx12Translate.h"
#include "API/RHI.h"

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
            if (desc.usage & PE_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT)
            {
                D3D12_CLEAR_VALUE clear{};
                clear.Format = viewFormat;
                clear.DepthStencil.Depth = 1.0f;
                clear.DepthStencil.Stencil = 0;
                return clear;
            }

            if (desc.usage & PE_IMAGE_USAGE_COLOR_ATTACHMENT)
            {
                D3D12_CLEAR_VALUE clear{};
                clear.Format = viewFormat;
                clear.Color[0] = 0.0f;
                clear.Color[1] = 0.0f;
                clear.Color[2] = 0.0f;
                clear.Color[3] = 1.0f;
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

    Dx12ImageImpl::~Dx12ImageImpl()
    {
        m_resource.Reset();
        m_allocation.Reset();
    }

    void Dx12ImageImpl::CopyImage(CommandBuffer * /*cmd*/, Image * /*src*/)
    {
        PE_ERROR("Dx12ImageImpl::CopyImage waits for the DX12 CommandBuffer slice");
    }

    void Dx12ImageImpl::CopyToBuffer(CommandBuffer * /*cmd*/, Buffer * /*dst*/)
    {
        PE_ERROR("Dx12ImageImpl::CopyToBuffer waits for the DX12 CommandBuffer slice");
    }

    void Dx12ImageImpl::Blit(CommandBuffer * /*cmd*/, Image * /*src*/, const vk::ImageBlit & /*region*/, vk::Filter /*filter*/)
    {
        PE_ERROR("Dx12ImageImpl::Blit is Vulkan-specific and waits for the DX12 mip-generation path");
    }

    void Dx12ImageImpl::CopyDataToImageStaged(CommandBuffer * /*cmd*/,
                                              void * /*data*/,
                                              size_t /*size*/,
                                              uint32_t /*baseArrayLayer*/,
                                              uint32_t /*layerCount*/,
                                              uint32_t /*mipLevel*/)
    {
        PE_ERROR("Dx12ImageImpl::CopyDataToImageStaged waits for the DX12 CommandBuffer slice");
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
        viewInfo.viewType = PE_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = image->GetFormat();
        viewInfo.aspectMask = AspectForFormat(m_viewFormat);
        viewInfo.baseMipLevel = 0;
        viewInfo.levelCount = image->GetMipLevels();
        viewInfo.baseArrayLayer = 0;
        viewInfo.layerCount = image->GetArrayLayers();

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
