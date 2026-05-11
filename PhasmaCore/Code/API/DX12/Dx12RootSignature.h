#pragma once

#include "API/RHITypes.h"

#include <d3d12.h>
#include <wrl/client.h>

namespace pe
{
    constexpr uint32_t DX12_DESCRIPTOR_SPACE_COUNT = 4;
    constexpr uint32_t DX12_DESCRIPTORS_PER_TYPE = 512;
    static_assert(DX12_DESCRIPTORS_PER_TYPE >= PE_MAX_DESCRIPTORS_PER_BINDING,
                  "DX12 descriptor ranges must cover the unbounded-array reflection fallback");
    constexpr uint32_t DX12_CBV_TABLE_OFFSET = 0;
    constexpr uint32_t DX12_SRV_TABLE_OFFSET = DX12_DESCRIPTORS_PER_TYPE;
    constexpr uint32_t DX12_UAV_TABLE_OFFSET = DX12_DESCRIPTORS_PER_TYPE * 2;
    constexpr uint32_t DX12_ROOT_CONSTANTS_INDEX = 0;

    inline uint32_t Dx12CbvBaseRegister(uint32_t space)
    {
        return space == 0 ? 1u : 0u;
    }

    inline uint32_t Dx12CbvSrvUavRootIndex(uint32_t space)
    {
        return 1u + space * 2u;
    }

    inline uint32_t Dx12SamplerRootIndex(uint32_t space)
    {
        return Dx12CbvSrvUavRootIndex(space) + 1u;
    }

    class Dx12RootSignature
    {
    public:
        explicit Dx12RootSignature(ID3D12Device *device);

        ID3D12RootSignature *Get() const { return m_rootSignature.Get(); }

    private:
        Microsoft::WRL::ComPtr<ID3D12RootSignature> m_rootSignature;
    };
} // namespace pe
