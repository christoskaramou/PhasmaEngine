#include "API/DX12/Dx12RootSignature.h"

namespace pe
{
    Dx12RootSignature::Dx12RootSignature(ID3D12Device *device)
    {
        PE_ERROR_IF(!device, "Dx12RootSignature requires a valid D3D12 device");

        std::array<D3D12_ROOT_PARAMETER1, 1 + DX12_DESCRIPTOR_SPACE_COUNT * 2> parameters{};

        parameters[DX12_ROOT_CONSTANTS_INDEX].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameters[DX12_ROOT_CONSTANTS_INDEX].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        parameters[DX12_ROOT_CONSTANTS_INDEX].Constants.ShaderRegister = 0;
        parameters[DX12_ROOT_CONSTANTS_INDEX].Constants.RegisterSpace = 0;
        parameters[DX12_ROOT_CONSTANTS_INDEX].Constants.Num32BitValues = 32;

        std::array<std::array<D3D12_DESCRIPTOR_RANGE1, 3>, DX12_DESCRIPTOR_SPACE_COUNT> resourceRanges{};
        std::array<D3D12_DESCRIPTOR_RANGE1, DX12_DESCRIPTOR_SPACE_COUNT> samplerRanges{};

        for (uint32_t space = 0; space < DX12_DESCRIPTOR_SPACE_COUNT; ++space)
        {
            const uint32_t cbvBase = Dx12CbvBaseRegister(space);
            auto &cbvRange = resourceRanges[space][0];
            cbvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
            cbvRange.NumDescriptors = DX12_DESCRIPTORS_PER_TYPE - cbvBase;
            cbvRange.BaseShaderRegister = cbvBase;
            cbvRange.RegisterSpace = space;
            cbvRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;
            cbvRange.OffsetInDescriptorsFromTableStart = DX12_CBV_TABLE_OFFSET;

            auto &srvRange = resourceRanges[space][1];
            srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            srvRange.NumDescriptors = DX12_DESCRIPTORS_PER_TYPE;
            srvRange.BaseShaderRegister = 0;
            srvRange.RegisterSpace = space;
            srvRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;
            srvRange.OffsetInDescriptorsFromTableStart = DX12_SRV_TABLE_OFFSET;

            auto &uavRange = resourceRanges[space][2];
            uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
            uavRange.NumDescriptors = DX12_DESCRIPTORS_PER_TYPE;
            uavRange.BaseShaderRegister = 0;
            uavRange.RegisterSpace = space;
            uavRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;
            uavRange.OffsetInDescriptorsFromTableStart = DX12_UAV_TABLE_OFFSET;

            const uint32_t resourceRoot = Dx12CbvSrvUavRootIndex(space);
            parameters[resourceRoot].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            parameters[resourceRoot].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            parameters[resourceRoot].DescriptorTable.NumDescriptorRanges = static_cast<UINT>(resourceRanges[space].size());
            parameters[resourceRoot].DescriptorTable.pDescriptorRanges = resourceRanges[space].data();

            auto &samplerRange = samplerRanges[space];
            samplerRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
            samplerRange.NumDescriptors = DX12_DESCRIPTORS_PER_TYPE;
            samplerRange.BaseShaderRegister = 0;
            samplerRange.RegisterSpace = space;
            samplerRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;
            samplerRange.OffsetInDescriptorsFromTableStart = 0;

            const uint32_t samplerRoot = Dx12SamplerRootIndex(space);
            parameters[samplerRoot].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            parameters[samplerRoot].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            parameters[samplerRoot].DescriptorTable.NumDescriptorRanges = 1;
            parameters[samplerRoot].DescriptorTable.pDescriptorRanges = &samplerRange;
        }

        D3D12_VERSIONED_ROOT_SIGNATURE_DESC desc{};
        desc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
        desc.Desc_1_1.NumParameters = static_cast<UINT>(std::size(parameters));
        desc.Desc_1_1.pParameters = parameters.data();
        desc.Desc_1_1.NumStaticSamplers = 0;
        desc.Desc_1_1.pStaticSamplers = nullptr;
        desc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
                              D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED |
                              D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED;

        Microsoft::WRL::ComPtr<ID3DBlob> blob;
        Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
        HRESULT hr = D3D12SerializeVersionedRootSignature(&desc, &blob, &errorBlob);
        if (FAILED(hr))
        {
            const char *message = errorBlob ? static_cast<const char *>(errorBlob->GetBufferPointer()) : "no details";
            PE_ERROR("Dx12RootSignature: D3D12SerializeVersionedRootSignature failed (0x%08X): %s",
                     static_cast<unsigned>(hr),
                     message);
        }

        hr = device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature));
        PE_ERROR_IF(FAILED(hr),
                    "Dx12RootSignature: CreateRootSignature failed (0x%08X)",
                    static_cast<unsigned>(hr));
    }
} // namespace pe
