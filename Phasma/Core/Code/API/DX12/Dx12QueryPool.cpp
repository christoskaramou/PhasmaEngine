#include "API/DX12/Dx12QueryPool.h"

#if defined(PE_WIN32)

#include "API/DX12/Dx12RhiImpl.h"

namespace pe
{
    namespace
    {
        D3D12_QUERY_HEAP_TYPE ToDx12HeapType(PeQueryType type)
        {
            switch (type)
            {
            case PE_QUERY_TYPE_TIMESTAMP:
                return D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
            case PE_QUERY_TYPE_OCCLUSION:
            default:
                return D3D12_QUERY_HEAP_TYPE_OCCLUSION;
            }
        }

        D3D12_QUERY_TYPE ToDx12QueryType(PeQueryType type, bool binary)
        {
            switch (type)
            {
            case PE_QUERY_TYPE_TIMESTAMP:
                return D3D12_QUERY_TYPE_TIMESTAMP;
            case PE_QUERY_TYPE_OCCLUSION:
            default:
                return binary ? D3D12_QUERY_TYPE_BINARY_OCCLUSION : D3D12_QUERY_TYPE_OCCLUSION;
            }
        }
    } // namespace

    Dx12QueryPool::Dx12QueryPool(const QueryPoolDesc &desc)
        : m_queryType{ToDx12QueryType(desc.type, desc.binary)}
    {
        PE_ERROR_IF(desc.count == 0, "Dx12QueryPool: query count must be non-zero");

        ID3D12Device *device = GetDx12Device();
        PE_ERROR_IF(!device, "Dx12QueryPool: DX12 device is not initialized");

        D3D12_QUERY_HEAP_DESC heapDesc{};
        heapDesc.Type = ToDx12HeapType(desc.type);
        heapDesc.Count = desc.count;
        heapDesc.NodeMask = 0;

        HRESULT hr = device->CreateQueryHeap(&heapDesc, IID_PPV_ARGS(&m_heap));
        PE_ERROR_IF(FAILED(hr) || !m_heap,
                    "Dx12QueryPool: CreateQueryHeap failed (0x%08X)", static_cast<unsigned>(hr));

        if (!desc.name.empty())
        {
            std::wstring wname(desc.name.begin(), desc.name.end());
            m_heap->SetName(wname.c_str());
        }
    }
} // namespace pe

#endif // PE_WIN32
