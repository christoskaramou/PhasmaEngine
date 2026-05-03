#pragma once

#include <d3d12.h>
#include <wrl/client.h>

namespace pe
{
    class Dx12RootSignature
    {
    public:
        explicit Dx12RootSignature(ID3D12Device *device);

        ID3D12RootSignature *Get() const { return m_rootSignature.Get(); }

    private:
        Microsoft::WRL::ComPtr<ID3D12RootSignature> m_rootSignature;
    };
} // namespace pe
