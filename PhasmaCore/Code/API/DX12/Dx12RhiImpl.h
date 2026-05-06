#pragma once

#include "API/DX12/Dx12DescriptorHeap.h"
#include "API/DX12/Dx12RootSignature.h"
#include "API/RHI_Internal.h"

#include <memory>
#include <wrl/client.h>

namespace D3D12MA
{
    class Allocator;
}

namespace pe
{
    class Dx12RhiImpl final : public RHI::Impl
    {
    public:
        bool Init(SDL_Window *window) override;
        void Shutdown() override;
        void WaitDeviceIdle() override;
        void NextFrame() override;
        GpuMemorySnapshot GetGpuMemorySnapshot() override;

        ID3D12Device *GetDevice() const { return m_device.Get(); }
        ID3D12CommandQueue *GetGraphicsQueue() const { return m_graphicsQueue.Get(); }
        IDXGIFactory6 *GetFactory() const { return m_factory.Get(); }
        IDXGIAdapter4 *GetAdapter() const { return m_adapter.Get(); }
        bool SupportsAllowTearing() const { return m_allowTearing; }
        D3D12MA::Allocator *GetAllocator() const { return m_d3d12Allocator; }
        Dx12DescriptorHeap *GetCbvSrvUavHeap() const { return m_cbvSrvUavHeap.get(); }
        Dx12DescriptorHeap *GetSamplerHeap() const { return m_samplerHeap.get(); }
        Dx12DescriptorHeap *GetCbvSrvUavStagingHeap() const { return m_cbvSrvUavStagingHeap.get(); }
        Dx12DescriptorHeap *GetSamplerStagingHeap() const { return m_samplerStagingHeap.get(); }
        Dx12DescriptorHeap *GetRtvStagingHeap() const { return m_rtvStagingHeap.get(); }
        Dx12DescriptorHeap *GetDsvStagingHeap() const { return m_dsvStagingHeap.get(); }
        Dx12RootSignature *GetSharedRootSig() const { return m_sharedRootSignature.get(); }
        const RHI::Caps &GetCaps() const { return m_caps; }
        const std::string &GetAdapterName() const { return m_adapterName; }

    private:
        Microsoft::WRL::ComPtr<IDXGIFactory6> m_factory;
        Microsoft::WRL::ComPtr<IDXGIAdapter4> m_adapter;
        Microsoft::WRL::ComPtr<ID3D12Device> m_device;
        bool m_allowTearing = false;
        D3D12MA::Allocator *m_d3d12Allocator = nullptr;
        std::unique_ptr<Dx12DescriptorHeap> m_cbvSrvUavHeap;
        std::unique_ptr<Dx12DescriptorHeap> m_samplerHeap;
        std::unique_ptr<Dx12DescriptorHeap> m_cbvSrvUavStagingHeap;
        std::unique_ptr<Dx12DescriptorHeap> m_samplerStagingHeap;
        std::unique_ptr<Dx12DescriptorHeap> m_rtvStagingHeap;
        std::unique_ptr<Dx12DescriptorHeap> m_dsvStagingHeap;
        std::unique_ptr<Dx12RootSignature> m_sharedRootSignature;
        Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_graphicsQueue;
        Microsoft::WRL::ComPtr<ID3D12Fence> m_frameFence;
        HANDLE m_fenceEvent = nullptr;
        uint64_t m_fenceValue = 0;
        std::string m_adapterName;
        RHI::Caps m_caps{};
    };
} // namespace pe
