#pragma once

#include "API/ImageView_Internal.h"

#include <wrl/client.h>

namespace pe
{
    enum class Dx12ImageViewKind
    {
        Srv,
        Uav,
        Rtv,
        Dsv,
    };

    struct Dx12ImageViewImpl final : public ImageView::Impl
    {
        Dx12ImageViewImpl(ImageView *owner, const ImageViewDesc &desc, Dx12ImageViewKind kind);
        ~Dx12ImageViewImpl() override = default;

        static ImageView *Create(Image *parent, const ImageViewDesc &desc, Dx12ImageViewKind kind, const std::string &name);
        static Dx12ImageViewImpl *From(ImageView *view) { return static_cast<Dx12ImageViewImpl *>(view->m_impl); }
        static const Dx12ImageViewImpl *From(const ImageView *view) { return static_cast<const Dx12ImageViewImpl *>(view->m_impl); }

        D3D12_CPU_DESCRIPTOR_HANDLE GetCpuHandle() const { return m_cpuHandle; }
        Dx12ImageViewKind GetKind() const { return m_kind; }

        ImageView *m_owner = nullptr;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_heap;
        D3D12_CPU_DESCRIPTOR_HANDLE m_cpuHandle{};
        Dx12ImageViewKind m_kind = Dx12ImageViewKind::Srv;
    };
} // namespace pe
