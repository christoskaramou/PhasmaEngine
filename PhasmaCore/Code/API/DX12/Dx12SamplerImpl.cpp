#include "API/DX12/Dx12SamplerImpl.h"

#include "API/DX12/Dx12DescriptorHeap.h"
#include "API/DX12/Dx12RhiImpl.h"
#include "API/DX12/Dx12Translate.h"
#include "API/RHI.h"

namespace pe
{
    namespace
    {
        D3D12_FILTER_TYPE FilterType(PeFilter filter)
        {
            switch (filter)
            {
            case PE_FILTER_NEAREST:
                return D3D12_FILTER_TYPE_POINT;
            case PE_FILTER_LINEAR:
                return D3D12_FILTER_TYPE_LINEAR;
            default:
                return D3D12_FILTER_TYPE_LINEAR;
            }
        }

        D3D12_FILTER_TYPE MipFilterType(PeSamplerMipmapMode mode)
        {
            switch (mode)
            {
            case PE_SAMPLER_MIPMAP_MODE_NEAREST:
                return D3D12_FILTER_TYPE_POINT;
            case PE_SAMPLER_MIPMAP_MODE_LINEAR:
                return D3D12_FILTER_TYPE_LINEAR;
            default:
                return D3D12_FILTER_TYPE_LINEAR;
            }
        }

        D3D12_FILTER_REDUCTION_TYPE FilterReduction(bool compareEnable)
        {
            return compareEnable ? D3D12_FILTER_REDUCTION_TYPE_COMPARISON : D3D12_FILTER_REDUCTION_TYPE_STANDARD;
        }

        D3D12_FILTER Filter(const SamplerDesc &desc)
        {
            if (desc.anisotropyEnable && desc.maxAnisotropy > 1.0f)
                return desc.compareEnable ? D3D12_FILTER_COMPARISON_ANISOTROPIC : D3D12_FILTER_ANISOTROPIC;

            return D3D12_ENCODE_BASIC_FILTER(FilterType(desc.minFilter),
                                             FilterType(desc.magFilter),
                                             MipFilterType(desc.mipmapMode),
                                             FilterReduction(desc.compareEnable));
        }

        UINT MaxAnisotropy(const SamplerDesc &desc)
        {
            if (!desc.anisotropyEnable)
                return 1;
            return static_cast<UINT>(std::clamp(desc.maxAnisotropy, 1.0f, 16.0f));
        }

        void FillBorderColor(PeSamplerBorderColor color, FLOAT out[4])
        {
            switch (color)
            {
            case PE_SAMPLER_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK:
                out[0] = 0.0f;
                out[1] = 0.0f;
                out[2] = 0.0f;
                out[3] = 0.0f;
                break;
            case PE_SAMPLER_BORDER_COLOR_FLOAT_OPAQUE_BLACK:
                out[0] = 0.0f;
                out[1] = 0.0f;
                out[2] = 0.0f;
                out[3] = 1.0f;
                break;
            case PE_SAMPLER_BORDER_COLOR_FLOAT_OPAQUE_WHITE:
                out[0] = 1.0f;
                out[1] = 1.0f;
                out[2] = 1.0f;
                out[3] = 1.0f;
                break;
            default:
                out[0] = 0.0f;
                out[1] = 0.0f;
                out[2] = 0.0f;
                out[3] = 1.0f;
                break;
            }
        }
    } // namespace

    Dx12SamplerImpl::Dx12SamplerImpl(Sampler *owner, const SamplerDesc &desc)
        : m_owner{owner}
    {
        PE_ERROR_IF(desc.unnormalizedCoordinates, "Dx12SamplerImpl: unnormalized coordinates are not supported by DX12");

        auto *rhi = static_cast<Dx12RhiImpl *>(RHII.GetImpl());
        PE_ERROR_IF(!rhi || !rhi->GetDevice(), "Dx12SamplerImpl requires an initialized DX12 device");
        ID3D12Device *device = rhi->GetDevice();

        m_heap = rhi->GetSamplerStagingHeap();
        PE_ERROR_IF(!m_heap, "Dx12SamplerImpl requires the DX12 sampler staging heap");
        m_slot = m_heap->Allocate();
        m_cpuHandle = m_heap->GetCpuHandle(m_slot);

        D3D12_SAMPLER_DESC samplerDesc{};
        samplerDesc.Filter = Filter(desc);
        samplerDesc.AddressU = pe_dx12::AddressMode(desc.addressModeU);
        samplerDesc.AddressV = pe_dx12::AddressMode(desc.addressModeV);
        samplerDesc.AddressW = pe_dx12::AddressMode(desc.addressModeW);
        samplerDesc.MipLODBias = desc.mipLodBias;
        samplerDesc.MaxAnisotropy = MaxAnisotropy(desc);
        samplerDesc.ComparisonFunc = desc.compareEnable ? pe_dx12::CompareOp(desc.compareOp) : D3D12_COMPARISON_FUNC_NONE;
        samplerDesc.MinLOD = desc.minLod;
        samplerDesc.MaxLOD = desc.maxLod;
        FillBorderColor(desc.borderColor, samplerDesc.BorderColor);

        device->CreateSampler(&samplerDesc, m_cpuHandle);
    }

    Dx12SamplerImpl::~Dx12SamplerImpl()
    {
        if (m_heap && m_slot != UINT32_MAX)
        {
            m_heap->Free(m_slot);
            m_slot = UINT32_MAX;
        }
    }
} // namespace pe
