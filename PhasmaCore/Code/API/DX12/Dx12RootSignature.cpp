#include "API/DX12/Dx12RootSignature.h"

namespace pe
{
    Dx12RootSignature::Dx12RootSignature(ID3D12Device *device)
    {
        PE_ERROR_IF(!device, "Dx12RootSignature requires a valid D3D12 device");

        D3D12_ROOT_PARAMETER1 parameters[6]{};

        parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        parameters[0].Constants.ShaderRegister = 0;
        parameters[0].Constants.RegisterSpace = 0;
        parameters[0].Constants.Num32BitValues = 32;

        parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        parameters[1].Descriptor.ShaderRegister = 1;
        parameters[1].Descriptor.RegisterSpace = 0;
        parameters[1].Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE;

        parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        parameters[2].Descriptor.ShaderRegister = 2;
        parameters[2].Descriptor.RegisterSpace = 0;
        parameters[2].Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE;

        D3D12_DESCRIPTOR_RANGE1 srvRange{};
        srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRange.NumDescriptors = UINT_MAX;
        srvRange.BaseShaderRegister = 0;
        srvRange.RegisterSpace = 0;
        srvRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;
        srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        parameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        parameters[3].DescriptorTable.NumDescriptorRanges = 1;
        parameters[3].DescriptorTable.pDescriptorRanges = &srvRange;

        D3D12_DESCRIPTOR_RANGE1 uavRange{};
        uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        uavRange.NumDescriptors = 2;
        uavRange.BaseShaderRegister = 0;
        uavRange.RegisterSpace = 0;
        uavRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;
        uavRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        parameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        parameters[4].DescriptorTable.NumDescriptorRanges = 1;
        parameters[4].DescriptorTable.pDescriptorRanges = &uavRange;

        D3D12_DESCRIPTOR_RANGE1 samplerRange{};
        samplerRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
        samplerRange.NumDescriptors = UINT_MAX;
        samplerRange.BaseShaderRegister = 0;
        samplerRange.RegisterSpace = 0;
        samplerRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;
        samplerRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        parameters[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        parameters[5].DescriptorTable.NumDescriptorRanges = 1;
        parameters[5].DescriptorTable.pDescriptorRanges = &samplerRange;

        D3D12_VERSIONED_ROOT_SIGNATURE_DESC desc{};
        desc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
        desc.Desc_1_1.NumParameters = static_cast<UINT>(std::size(parameters));
        desc.Desc_1_1.pParameters = parameters;
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
