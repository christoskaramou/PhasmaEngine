#pragma once

#include "API/Image_Internal.h"

#include <D3D12MemAlloc.h>
#include <wrl/client.h>

namespace pe
{
    struct Dx12ImageImpl final : public Image::Impl
    {
        Dx12ImageImpl(Image *owner, const ImageDesc &desc);
        Dx12ImageImpl(Image *owner, ID3D12Resource *externalResource, DXGI_FORMAT format, D3D12_RESOURCE_STATES initialState);
        ~Dx12ImageImpl() override;

        void CopyImage(CommandBuffer *cmd, Image *src) override;
        void CopyToBuffer(CommandBuffer *cmd, Buffer *dst) override;
        void Blit(CommandBuffer *cmd, Image *src, const ImageBlit &region, PeFilter filter) override;
        void CopyDataToImageStaged(CommandBuffer *cmd,
                                   void *data,
                                   size_t size,
                                   uint32_t baseArrayLayer,
                                   uint32_t layerCount,
                                   uint32_t mipLevel) override;
        void CreateRTV() override;
        void CreateSRV(PeImageViewType type, int mip) override;
        void CreateUAV(PeImageViewType type, uint32_t mip) override;

        static Dx12ImageImpl *From(Image *image) { return static_cast<Dx12ImageImpl *>(image->m_impl); }
        static const Dx12ImageImpl *From(const Image *image) { return static_cast<const Dx12ImageImpl *>(image->m_impl); }
        static Dx12ImageImpl *TryFrom(Image *image) { return image && image->m_impl ? From(image) : nullptr; }
        static const Dx12ImageImpl *TryFrom(const Image *image) { return image && image->m_impl ? From(image) : nullptr; }

        ID3D12Resource *GetResource() const { return m_resource.Get(); }
        DXGI_FORMAT GetResourceFormat() const { return m_resourceFormat; }
        DXGI_FORMAT GetViewFormat() const { return m_viewFormat; }

        Image *m_owner = nullptr;
        Microsoft::WRL::ComPtr<D3D12MA::Allocation> m_allocation;
        Microsoft::WRL::ComPtr<ID3D12Resource> m_resource;
        DXGI_FORMAT m_resourceFormat = DXGI_FORMAT_UNKNOWN;
        DXGI_FORMAT m_viewFormat = DXGI_FORMAT_UNKNOWN;
        D3D12_RESOURCE_STATES m_state = D3D12_RESOURCE_STATE_COMMON;
        D3D12_RESOURCE_STATES m_firstUseDiscardState = D3D12_RESOURCE_STATE_COMMON;
        bool m_needsFirstUseDiscard = false;
    };

    Image::Impl *CreateDx12SwapchainImageImpl(Image *owner, ID3D12Resource *externalResource, DXGI_FORMAT format);
} // namespace pe
