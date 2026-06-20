#pragma once

#include "API/RHITypes.h"

namespace pe
{
    struct QueryPoolDesc
    {
        PeQueryType type = PE_QUERY_TYPE_OCCLUSION;
        uint32_t count = 0;
        bool binary = false; // request the DX12 binary-occlusion query type; ignored where unsupported
        std::string name;
    };

    // Backend-neutral GPU query pool (occlusion / timestamp). Mirrors the GpuTimer
    // pImpl pattern: the neutral object owns a backend Impl that holds the native
    // VkQueryPool / ID3D12QueryHeap. Record reset/begin/end/resolve via CommandBuffer.
    class QueryPool : public PeHandle<QueryPool, void *>
    {
    public:
        class Impl;

        QueryPool(const QueryPoolDesc &desc);
        ~QueryPool();

        PeQueryType GetType() const { return m_desc.type; }
        uint32_t GetCount() const { return m_desc.count; }
        bool IsBinary() const { return m_desc.binary; }
        const QueryPoolDesc &GetDesc() const { return m_desc; }
        Impl *GetImpl() const { return m_impl.get(); }

    private:
        QueryPoolDesc m_desc;
        std::unique_ptr<Impl> m_impl;
    };
} // namespace pe
