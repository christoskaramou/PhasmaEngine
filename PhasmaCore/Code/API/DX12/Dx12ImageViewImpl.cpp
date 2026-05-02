#include "API/DX12/Dx12ImageViewImpl.h"

#include "API/DX12/Dx12ImageImpl.h"
#include "API/DX12/Dx12RhiImpl.h"
#include "API/DX12/Dx12Translate.h"
#include "API/RHI.h"

namespace pe
{
    namespace
    {
        D3D12_DESCRIPTOR_HEAP_TYPE HeapTypeForKind(Dx12ImageViewKind kind)
        {
            switch (kind)
            {
            case Dx12ImageViewKind::Rtv:
                return D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
            case Dx12ImageViewKind::Dsv:
                return D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
            case Dx12ImageViewKind::Srv:
            case Dx12ImageViewKind::Uav:
                return D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            default:
                return D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            }
        }

        DXGI_FORMAT ViewFormat(const ImageViewDesc &desc, const Dx12ImageImpl *image, Dx12ImageViewKind kind)
        {
            DXGI_FORMAT format = desc.format == PE_FORMAT_UNDEFINED ? image->GetViewFormat() : pe_dx12::Format(desc.format);
            if (kind == Dx12ImageViewKind::Srv)
                format = pe_dx12::DepthToSRVFormat(format);
            return format;
        }

        D3D12_SRV_DIMENSION SrvDimension(PeImageViewType type)
        {
            switch (type)
            {
            case PE_IMAGE_VIEW_TYPE_1D:
                return D3D12_SRV_DIMENSION_TEXTURE1D;
            case PE_IMAGE_VIEW_TYPE_1D_ARRAY:
                return D3D12_SRV_DIMENSION_TEXTURE1DARRAY;
            case PE_IMAGE_VIEW_TYPE_2D:
                return D3D12_SRV_DIMENSION_TEXTURE2D;
            case PE_IMAGE_VIEW_TYPE_2D_ARRAY:
                return D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
            case PE_IMAGE_VIEW_TYPE_3D:
                return D3D12_SRV_DIMENSION_TEXTURE3D;
            case PE_IMAGE_VIEW_TYPE_CUBE:
                return D3D12_SRV_DIMENSION_TEXTURECUBE;
            case PE_IMAGE_VIEW_TYPE_CUBE_ARRAY:
                return D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
            default:
                return D3D12_SRV_DIMENSION_TEXTURE2D;
            }
        }

        D3D12_UAV_DIMENSION UavDimension(PeImageViewType type)
        {
            switch (type)
            {
            case PE_IMAGE_VIEW_TYPE_1D:
                return D3D12_UAV_DIMENSION_TEXTURE1D;
            case PE_IMAGE_VIEW_TYPE_1D_ARRAY:
                return D3D12_UAV_DIMENSION_TEXTURE1DARRAY;
            case PE_IMAGE_VIEW_TYPE_2D:
                return D3D12_UAV_DIMENSION_TEXTURE2D;
            case PE_IMAGE_VIEW_TYPE_2D_ARRAY:
                return D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
            case PE_IMAGE_VIEW_TYPE_3D:
                return D3D12_UAV_DIMENSION_TEXTURE3D;
            default:
                return D3D12_UAV_DIMENSION_TEXTURE2D;
            }
        }

        D3D12_RTV_DIMENSION RtvDimension(PeImageViewType type)
        {
            switch (type)
            {
            case PE_IMAGE_VIEW_TYPE_1D:
                return D3D12_RTV_DIMENSION_TEXTURE1D;
            case PE_IMAGE_VIEW_TYPE_1D_ARRAY:
                return D3D12_RTV_DIMENSION_TEXTURE1DARRAY;
            case PE_IMAGE_VIEW_TYPE_2D:
            case PE_IMAGE_VIEW_TYPE_CUBE:
                return D3D12_RTV_DIMENSION_TEXTURE2D;
            case PE_IMAGE_VIEW_TYPE_2D_ARRAY:
            case PE_IMAGE_VIEW_TYPE_CUBE_ARRAY:
                return D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
            case PE_IMAGE_VIEW_TYPE_3D:
                return D3D12_RTV_DIMENSION_TEXTURE3D;
            default:
                return D3D12_RTV_DIMENSION_TEXTURE2D;
            }
        }

        D3D12_DSV_DIMENSION DsvDimension(PeImageViewType type)
        {
            switch (type)
            {
            case PE_IMAGE_VIEW_TYPE_1D:
                return D3D12_DSV_DIMENSION_TEXTURE1D;
            case PE_IMAGE_VIEW_TYPE_1D_ARRAY:
                return D3D12_DSV_DIMENSION_TEXTURE1DARRAY;
            case PE_IMAGE_VIEW_TYPE_2D:
            case PE_IMAGE_VIEW_TYPE_CUBE:
                return D3D12_DSV_DIMENSION_TEXTURE2D;
            case PE_IMAGE_VIEW_TYPE_2D_ARRAY:
            case PE_IMAGE_VIEW_TYPE_CUBE_ARRAY:
                return D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
            default:
                return D3D12_DSV_DIMENSION_TEXTURE2D;
            }
        }

        void FillSrvDesc(D3D12_SHADER_RESOURCE_VIEW_DESC &srv, const ImageViewDesc &desc, DXGI_FORMAT format)
        {
            srv.Format = format;
            srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srv.ViewDimension = SrvDimension(desc.viewType);
            switch (srv.ViewDimension)
            {
            case D3D12_SRV_DIMENSION_TEXTURE1D:
                srv.Texture1D.MostDetailedMip = desc.baseMipLevel;
                srv.Texture1D.MipLevels = desc.levelCount;
                break;
            case D3D12_SRV_DIMENSION_TEXTURE1DARRAY:
                srv.Texture1DArray.MostDetailedMip = desc.baseMipLevel;
                srv.Texture1DArray.MipLevels = desc.levelCount;
                srv.Texture1DArray.FirstArraySlice = desc.baseArrayLayer;
                srv.Texture1DArray.ArraySize = desc.layerCount;
                break;
            case D3D12_SRV_DIMENSION_TEXTURE2D:
                srv.Texture2D.MostDetailedMip = desc.baseMipLevel;
                srv.Texture2D.MipLevels = desc.levelCount;
                break;
            case D3D12_SRV_DIMENSION_TEXTURE2DARRAY:
                srv.Texture2DArray.MostDetailedMip = desc.baseMipLevel;
                srv.Texture2DArray.MipLevels = desc.levelCount;
                srv.Texture2DArray.FirstArraySlice = desc.baseArrayLayer;
                srv.Texture2DArray.ArraySize = desc.layerCount;
                break;
            case D3D12_SRV_DIMENSION_TEXTURE3D:
                srv.Texture3D.MostDetailedMip = desc.baseMipLevel;
                srv.Texture3D.MipLevels = desc.levelCount;
                break;
            case D3D12_SRV_DIMENSION_TEXTURECUBE:
                srv.TextureCube.MostDetailedMip = desc.baseMipLevel;
                srv.TextureCube.MipLevels = desc.levelCount;
                break;
            case D3D12_SRV_DIMENSION_TEXTURECUBEARRAY:
                srv.TextureCubeArray.MostDetailedMip = desc.baseMipLevel;
                srv.TextureCubeArray.MipLevels = desc.levelCount;
                srv.TextureCubeArray.First2DArrayFace = desc.baseArrayLayer;
                srv.TextureCubeArray.NumCubes = desc.layerCount / 6;
                break;
            default:
                break;
            }
        }

        void FillUavDesc(D3D12_UNORDERED_ACCESS_VIEW_DESC &uav, const ImageViewDesc &desc, DXGI_FORMAT format)
        {
            uav.Format = format;
            uav.ViewDimension = UavDimension(desc.viewType);
            switch (uav.ViewDimension)
            {
            case D3D12_UAV_DIMENSION_TEXTURE1D:
                uav.Texture1D.MipSlice = desc.baseMipLevel;
                break;
            case D3D12_UAV_DIMENSION_TEXTURE1DARRAY:
                uav.Texture1DArray.MipSlice = desc.baseMipLevel;
                uav.Texture1DArray.FirstArraySlice = desc.baseArrayLayer;
                uav.Texture1DArray.ArraySize = desc.layerCount;
                break;
            case D3D12_UAV_DIMENSION_TEXTURE2D:
                uav.Texture2D.MipSlice = desc.baseMipLevel;
                break;
            case D3D12_UAV_DIMENSION_TEXTURE2DARRAY:
                uav.Texture2DArray.MipSlice = desc.baseMipLevel;
                uav.Texture2DArray.FirstArraySlice = desc.baseArrayLayer;
                uav.Texture2DArray.ArraySize = desc.layerCount;
                break;
            case D3D12_UAV_DIMENSION_TEXTURE3D:
                uav.Texture3D.MipSlice = desc.baseMipLevel;
                uav.Texture3D.FirstWSlice = desc.baseArrayLayer;
                uav.Texture3D.WSize = desc.layerCount;
                break;
            default:
                break;
            }
        }

        void FillRtvDesc(D3D12_RENDER_TARGET_VIEW_DESC &rtv, const ImageViewDesc &desc, DXGI_FORMAT format)
        {
            rtv.Format = format;
            rtv.ViewDimension = RtvDimension(desc.viewType);
            switch (rtv.ViewDimension)
            {
            case D3D12_RTV_DIMENSION_TEXTURE1D:
                rtv.Texture1D.MipSlice = desc.baseMipLevel;
                break;
            case D3D12_RTV_DIMENSION_TEXTURE1DARRAY:
                rtv.Texture1DArray.MipSlice = desc.baseMipLevel;
                rtv.Texture1DArray.FirstArraySlice = desc.baseArrayLayer;
                rtv.Texture1DArray.ArraySize = desc.layerCount;
                break;
            case D3D12_RTV_DIMENSION_TEXTURE2D:
                rtv.Texture2D.MipSlice = desc.baseMipLevel;
                break;
            case D3D12_RTV_DIMENSION_TEXTURE2DARRAY:
                rtv.Texture2DArray.MipSlice = desc.baseMipLevel;
                rtv.Texture2DArray.FirstArraySlice = desc.baseArrayLayer;
                rtv.Texture2DArray.ArraySize = desc.layerCount;
                break;
            case D3D12_RTV_DIMENSION_TEXTURE3D:
                rtv.Texture3D.MipSlice = desc.baseMipLevel;
                rtv.Texture3D.FirstWSlice = desc.baseArrayLayer;
                rtv.Texture3D.WSize = desc.layerCount;
                break;
            default:
                break;
            }
        }

        void FillDsvDesc(D3D12_DEPTH_STENCIL_VIEW_DESC &dsv, const ImageViewDesc &desc, DXGI_FORMAT format)
        {
            dsv.Format = format;
            dsv.ViewDimension = DsvDimension(desc.viewType);
            switch (dsv.ViewDimension)
            {
            case D3D12_DSV_DIMENSION_TEXTURE1D:
                dsv.Texture1D.MipSlice = desc.baseMipLevel;
                break;
            case D3D12_DSV_DIMENSION_TEXTURE1DARRAY:
                dsv.Texture1DArray.MipSlice = desc.baseMipLevel;
                dsv.Texture1DArray.FirstArraySlice = desc.baseArrayLayer;
                dsv.Texture1DArray.ArraySize = desc.layerCount;
                break;
            case D3D12_DSV_DIMENSION_TEXTURE2D:
                dsv.Texture2D.MipSlice = desc.baseMipLevel;
                break;
            case D3D12_DSV_DIMENSION_TEXTURE2DARRAY:
                dsv.Texture2DArray.MipSlice = desc.baseMipLevel;
                dsv.Texture2DArray.FirstArraySlice = desc.baseArrayLayer;
                dsv.Texture2DArray.ArraySize = desc.layerCount;
                break;
            default:
                break;
            }
        }
    } // namespace

    Dx12ImageViewImpl::Dx12ImageViewImpl(ImageView *owner, const ImageViewDesc &desc, Dx12ImageViewKind kind)
        : m_owner{owner}, m_kind{kind}
    {
        auto *rhi = static_cast<Dx12RhiImpl *>(RHII.GetImpl());
        PE_ERROR_IF(!rhi || !rhi->GetDevice(), "Dx12ImageViewImpl requires an initialized DX12 device");
        PE_ERROR_IF(!owner->m_parent, "Dx12ImageViewImpl: parent image is null");

        const auto *image = Dx12ImageImpl::From(owner->m_parent);
        ID3D12Device *device = rhi->GetDevice();

        D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
        heapDesc.Type = HeapTypeForKind(kind);
        heapDesc.NumDescriptors = 1;
        heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        HRESULT hr = device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_heap));
        PE_ERROR_IF(FAILED(hr), "Dx12ImageViewImpl: CreateDescriptorHeap failed (0x%08X)", static_cast<unsigned>(hr));
        m_cpuHandle = m_heap->GetCPUDescriptorHandleForHeapStart();

        const DXGI_FORMAT format = ViewFormat(desc, image, kind);
        switch (kind)
        {
        case Dx12ImageViewKind::Srv:
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
            FillSrvDesc(srv, desc, format);
            device->CreateShaderResourceView(image->GetResource(), &srv, m_cpuHandle);
            break;
        }
        case Dx12ImageViewKind::Uav:
        {
            D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
            FillUavDesc(uav, desc, format);
            device->CreateUnorderedAccessView(image->GetResource(), nullptr, &uav, m_cpuHandle);
            break;
        }
        case Dx12ImageViewKind::Rtv:
        {
            D3D12_RENDER_TARGET_VIEW_DESC rtv{};
            FillRtvDesc(rtv, desc, format);
            device->CreateRenderTargetView(image->GetResource(), &rtv, m_cpuHandle);
            break;
        }
        case Dx12ImageViewKind::Dsv:
        {
            D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
            FillDsvDesc(dsv, desc, format);
            device->CreateDepthStencilView(image->GetResource(), &dsv, m_cpuHandle);
            break;
        }
        }
    }

    ImageView *Dx12ImageViewImpl::Create(Image *parent, const ImageViewDesc &desc, Dx12ImageViewKind kind, const std::string &name)
    {
        ImageView *view = new ImageView(parent, desc, name, true);
        view->m_impl = new Dx12ImageViewImpl(view, desc, kind);
#if defined(PE_TRACK_RESOURCES)
        PeTracker::Track(typeid(ImageView), reinterpret_cast<void *>(view));
#if !defined(PE_TRACK_RESOURCES_NOSPAM)
        PE_INFO("Object ImageView created (Handle: %p)", reinterpret_cast<void *>(view));
#endif
#endif
        return view;
    }
} // namespace pe
