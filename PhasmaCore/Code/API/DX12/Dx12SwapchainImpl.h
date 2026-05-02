#pragma once

#include "API/Swapchain_Internal.h"

#include <wrl/client.h>

namespace pe
{
    class Semaphore;

    class Dx12SwapchainImpl final : public Swapchain::Impl
    {
    public:
        Dx12SwapchainImpl(Swapchain *owner, const SwapchainDesc &desc);
        ~Dx12SwapchainImpl() override;

        uint32_t AquireNextImage(Semaphore *semaphore) override;
        void Present() override;

        IDXGISwapChain3 *GetSwapchain() const { return m_swapchain.Get(); }
        ID3D12Resource *GetBackbuffer(uint32_t i) const { return m_backbuffers[i].Get(); }
        D3D12_CPU_DESCRIPTOR_HANDLE GetRtv(uint32_t i) const { return m_rtvHandles[i]; }
        uint32_t GetCurrentBackbufferIndex() const { return m_currentBackbuffer; }
        DXGI_FORMAT GetFormat() const { return m_format; }
        bool AllowTearing() const { return m_allowTearing; }

    private:
        Swapchain *m_owner;
        Microsoft::WRL::ComPtr<IDXGISwapChain3> m_swapchain;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
        std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> m_backbuffers;
        std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> m_rtvHandles;
        DXGI_FORMAT m_format{DXGI_FORMAT_R8G8B8A8_UNORM};
        uint32_t m_currentBackbuffer{0};
        bool m_allowTearing{false};
    };
} // namespace pe
