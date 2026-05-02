#pragma once

#include "API/RHI_Internal.h"

#include <wrl/client.h>

namespace pe
{
    class Dx12RhiImpl final : public RHI::Impl
    {
    public:
        bool Init(SDL_Window *window) override;
        void Shutdown() override;
        void WaitDeviceIdle() override;
        void NextFrame() override;

        ID3D12Device *GetDevice() const { return m_device.Get(); }
        ID3D12CommandQueue *GetGraphicsQueue() const { return m_graphicsQueue.Get(); }
        IDXGIFactory6 *GetFactory() const { return m_factory.Get(); }
        IDXGIAdapter4 *GetAdapter() const { return m_adapter.Get(); }
        const RHI::Caps &GetCaps() const { return m_caps; }
        const std::string &GetAdapterName() const { return m_adapterName; }

    private:
        Microsoft::WRL::ComPtr<IDXGIFactory6> m_factory;
        Microsoft::WRL::ComPtr<IDXGIAdapter4> m_adapter;
        Microsoft::WRL::ComPtr<ID3D12Device> m_device;
        Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_graphicsQueue;
        Microsoft::WRL::ComPtr<ID3D12Fence> m_frameFence;
        HANDLE m_fenceEvent = nullptr;
        uint64_t m_fenceValue = 0;
        std::string m_adapterName;
        RHI::Caps m_caps{};
    };
} // namespace pe
