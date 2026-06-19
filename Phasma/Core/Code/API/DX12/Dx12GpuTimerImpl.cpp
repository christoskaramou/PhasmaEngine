#include "API/DX12/Dx12GpuTimerImpl.h"

#if defined(PE_WIN32)

#include "API/Buffer.h"
#include "API/Command.h"
#include "API/DX12/Dx12BufferImpl.h"
#include "API/DX12/Dx12CommandBufferImpl.h"
#include "API/DX12/Dx12RhiImpl.h"
#include "API/RHI.h"
#include "API/RHITypes.h"

#include <d3d12.h>

namespace pe
{
    Dx12GpuTimerImpl::Dx12GpuTimerImpl(const std::string &name)
    {
        auto *rhi = static_cast<Dx12RhiImpl *>(RHII.GetImpl());
        PE_ERROR_IF(!rhi, "Dx12GpuTimerImpl requires an initialized DX12 RHI");

        UINT64 freq = 0;
        rhi->GetGraphicsQueue()->GetTimestampFrequency(&freq);
        PE_ERROR_IF(freq == 0, "DX12 timestamp frequency is zero (queue does not support timestamps)");
        m_msPerTick = 1000.0 / static_cast<double>(freq);

        D3D12_QUERY_HEAP_DESC heapDesc{};
        heapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        heapDesc.Count = 2;
        heapDesc.NodeMask = 0;
        HRESULT hr = rhi->GetDevice()->CreateQueryHeap(&heapDesc, IID_PPV_ARGS(&m_heap));
        PE_ERROR_IF(FAILED(hr), "Dx12GpuTimerImpl: CreateQueryHeap failed (0x%08X)", static_cast<unsigned>(hr));

        if (!name.empty())
        {
            std::wstring wname(name.begin(), name.end());
            m_heap->SetName(wname.c_str());
        }

        BufferDesc bd{};
        bd.size = 2 * sizeof(uint64_t);
        bd.usage = PE_BUFFER_USAGE_TRANSFER_DST;
        bd.memoryUsage = PE_MEMORY_USAGE_GPU_TO_CPU_PERSISTENT;
        bd.name = name + "_readback";
        m_readback = Buffer::Create(bd);
        m_readback->Map();
    }

    Dx12GpuTimerImpl::~Dx12GpuTimerImpl()
    {
        if (m_readback)
        {
            m_readback->Unmap();
            Buffer::Destroy(m_readback);
        }
        m_heap.Reset();
    }

    void Dx12GpuTimerImpl::Start(CommandBuffer *cmd)
    {
        PE_ERROR_IF(m_inUse, "GpuTimer::Start() called before End()");
        m_cmd = cmd;
        auto *list = Dx12CommandBufferImpl::From(cmd)->Get();
        list->EndQuery(m_heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
        m_resultsReady = false;
        m_inUse = true;
    }

    void Dx12GpuTimerImpl::End()
    {
        PE_ERROR_IF(!m_inUse, "GpuTimer::End() called before Start()");
        auto *list = Dx12CommandBufferImpl::From(m_cmd)->Get();
        list->EndQuery(m_heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 1);

        ID3D12Resource *dst = Dx12BufferImpl::From(m_readback)->GetResource();
        list->ResolveQueryData(m_heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 2, dst, 0);

        m_resultsReady = false;
        m_inUse = false;
    }

    float Dx12GpuTimerImpl::GetTime()
    {
        PE_ERROR_IF(m_inUse, "GpuTimer::GetTime() called before End()");

        // The readback buffer is filled by ResolveQueryData when the recorded
        // command list completes; callers fetch results after CommandBuffer::Wait,
        // which is the synchronization point that makes the mapped pointer valid.
        if (!m_resultsReady)
        {
            const uint64_t *src = static_cast<const uint64_t *>(m_readback->Data());
            if (!src)
                return 0.f;
            m_queries[0] = src[0];
            m_queries[1] = src[1];
            m_cmd = nullptr;
            m_resultsReady = true;
        }

        if (m_queries[1] <= m_queries[0])
            return 0.f;
        return static_cast<float>(static_cast<double>(m_queries[1] - m_queries[0]) * m_msPerTick);
    }

    double Dx12GpuTimerImpl::GetStartTimeMs() const
    {
        return static_cast<double>(m_queries[0]) * m_msPerTick;
    }
} // namespace pe

#endif // PE_WIN32
