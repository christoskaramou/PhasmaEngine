#pragma once

#include "API/QueryPool_Internal.h"

#if defined(PE_WIN32)

#include <d3d12.h>
#include <wrl/client.h>

namespace pe
{
    class Dx12QueryPool final : public QueryPool::Impl
    {
    public:
        Dx12QueryPool(const QueryPoolDesc &desc);
        ~Dx12QueryPool() override = default;

        ID3D12QueryHeap *Heap() const { return m_heap.Get(); }
        D3D12_QUERY_TYPE QueryType() const { return m_queryType; }

        static Dx12QueryPool *From(QueryPool *pool)
        {
            return pool ? static_cast<Dx12QueryPool *>(pool->GetImpl()) : nullptr;
        }

    private:
        Microsoft::WRL::ComPtr<ID3D12QueryHeap> m_heap;
        D3D12_QUERY_TYPE m_queryType = D3D12_QUERY_TYPE_OCCLUSION;
    };
} // namespace pe

#endif // PE_WIN32
