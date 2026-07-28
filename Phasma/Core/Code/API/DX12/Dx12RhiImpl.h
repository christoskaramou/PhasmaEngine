#pragma once

#include "API/DX12/Dx12DescriptorHeap.h"
#include "API/DX12/Dx12RootSignature.h"
#include "API/RHI.h"
#include "API/RHI_Internal.h"

#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <windows.h>
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
        D3D12_RAYTRACING_TIER GetRayTracingTier() const { return m_rayTracingTier; }
        bool SupportsDxr() const { return m_rayTracingTier >= D3D12_RAYTRACING_TIER_1_0; }
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
        Microsoft::WRL::ComPtr<ID3D12InfoQueue1> m_infoQueueCallback;
        DWORD m_infoQueueCallbackCookie = 0;
        HANDLE m_fenceEvent = nullptr;
        uint64_t m_fenceValue = 0;
        D3D12_RAYTRACING_TIER m_rayTracingTier = D3D12_RAYTRACING_TIER_NOT_SUPPORTED;
        std::string m_adapterName;
        RHI::Caps m_caps{};
    };

    inline ID3D12Device *GetDx12Device()
    {
        Dx12RhiImpl *impl = static_cast<Dx12RhiImpl *>(RHII.GetImpl());
        return impl ? impl->GetDevice() : nullptr;
    }

    // Logs GetDeviceRemovedReason plus DRED breadcrumbs/page-fault data (when DRED was
    // enabled at device creation) after a device removal. No-op while the device is alive.
    void Dx12ReportDeviceRemoved();
} // namespace pe
