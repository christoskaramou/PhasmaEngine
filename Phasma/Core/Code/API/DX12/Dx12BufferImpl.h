#pragma once

#include "API/Buffer_Internal.h"

#include <D3D12MemAlloc.h>
#include <wrl/client.h>

namespace pe
{
    class CommandBuffer;

    struct Dx12BufferImpl final : public Buffer::Impl
    {
        Dx12BufferImpl(Buffer *owner, const BufferDesc &desc);
        ~Dx12BufferImpl() override;

        void *Map() override;
        void Unmap() override;
        void Flush(size_t size, size_t offset) const override;
        uint64_t GetDeviceAddress() const override;
        void CopyBuffer(CommandBuffer *cmd, Buffer *src, size_t size, size_t srcOffset, size_t dstOffset) override;

        static Dx12BufferImpl *From(Buffer *buf) { return static_cast<Dx12BufferImpl *>(buf->m_impl); }
        static const Dx12BufferImpl *From(const Buffer *buf) { return static_cast<const Dx12BufferImpl *>(buf->m_impl); }

        ID3D12Resource *GetResource() const { return m_resource.Get(); }
        bool AllowsUnorderedAccess() const { return m_allowsUnorderedAccess; }

        Buffer *m_owner = nullptr;
        Microsoft::WRL::ComPtr<D3D12MA::Allocation> m_allocation;
        Microsoft::WRL::ComPtr<ID3D12Resource> m_resource;
        D3D12_HEAP_TYPE m_heapType = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_STATES m_state = D3D12_RESOURCE_STATE_COMMON;
        bool m_allowsUnorderedAccess = false;
        void *m_mapped = nullptr;
    };
} // namespace pe
