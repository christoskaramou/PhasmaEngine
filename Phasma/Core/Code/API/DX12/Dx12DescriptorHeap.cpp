#include "API/DX12/Dx12DescriptorHeap.h"

namespace pe
{
    namespace
    {
        const char *HeapTypeName(D3D12_DESCRIPTOR_HEAP_TYPE type)
        {
            switch (type)
            {
            case D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV:
                return "CBV_SRV_UAV";
            case D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER:
                return "SAMPLER";
            case D3D12_DESCRIPTOR_HEAP_TYPE_RTV:
                return "RTV";
            case D3D12_DESCRIPTOR_HEAP_TYPE_DSV:
                return "DSV";
            case D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES:
                return "NUM_TYPES";
            }
            return "UNKNOWN";
        }

        bool SupportsShaderVisible(D3D12_DESCRIPTOR_HEAP_TYPE type)
        {
            return type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV || type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
        }
    } // namespace

    Dx12DescriptorHeap::Dx12DescriptorHeap(ID3D12Device *device,
                                           D3D12_DESCRIPTOR_HEAP_TYPE type,
                                           uint32_t capacity,
                                           bool shaderVisible)
        : m_type{type}, m_capacity{capacity}, m_shaderVisible{shaderVisible}
    {
        PE_ERROR_IF(!device, "Dx12DescriptorHeap requires a valid D3D12 device");
        PE_ERROR_IF(capacity == 0, "Dx12DescriptorHeap(%s) requires a non-zero capacity", HeapTypeName(type));
        PE_ERROR_IF(shaderVisible && !SupportsShaderVisible(type),
                    "Dx12DescriptorHeap(%s) cannot be shader-visible",
                    HeapTypeName(type));

        D3D12_DESCRIPTOR_HEAP_DESC desc{};
        desc.Type = type;
        desc.NumDescriptors = capacity;
        desc.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        HRESULT hr = device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_heap));
        PE_ERROR_IF(FAILED(hr),
                    "Dx12DescriptorHeap(%s): CreateDescriptorHeap failed (0x%08X)",
                    HeapTypeName(type),
                    static_cast<unsigned>(hr));

        m_stride = device->GetDescriptorHandleIncrementSize(type);
        m_cpuStart = m_heap->GetCPUDescriptorHandleForHeapStart();
        if (m_shaderVisible)
            m_gpuStart = m_heap->GetGPUDescriptorHandleForHeapStart();
        m_allocated.resize(capacity, 0);
    }

    uint32_t Dx12DescriptorHeap::Allocate()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return AllocateLocked();
    }

    uint32_t Dx12DescriptorHeap::AllocateLocked()
    {
        uint32_t slot = 0;
        bool foundFreeSlot = false;
        while (!m_freeList.empty())
        {
            slot = m_freeList.back();
            m_freeList.pop_back();
            if (m_allocated[slot] == 0)
            {
                foundFreeSlot = true;
                break;
            }
        }

        if (!foundFreeSlot)
        {
            PE_ERROR_IF(m_nextSlot >= m_capacity,
                        "Dx12DescriptorHeap(%s) exhausted (%u descriptors)",
                        HeapTypeName(m_type),
                        m_capacity);
            slot = m_nextSlot++;
        }

        PE_ERROR_IF(m_allocated[slot] != 0, "Dx12DescriptorHeap(%s): slot %u is already allocated", HeapTypeName(m_type), slot);
        m_allocated[slot] = 1;
        return slot;
    }

    uint32_t Dx12DescriptorHeap::AllocateRange(uint32_t count)
    {
        PE_ERROR_IF(count == 0, "Dx12DescriptorHeap(%s): cannot allocate an empty range", HeapTypeName(m_type));
        std::lock_guard<std::mutex> lock(m_mutex);
        if (count == 1)
            return AllocateLocked();

        // ponytail: first-fit scan from a low-water hint instead of slot 0; a size-class free list is the
        // upgrade if range allocation ever shows in a profile.
        for (uint32_t first = m_rangeSearchStart; first + count <= m_nextSlot; ++first)
        {
            bool free = true;
            for (uint32_t i = 0; i < count; ++i)
            {
                if (m_allocated[first + i])
                {
                    free = false;
                    first += i;
                    break;
                }
            }

            if (!free)
                continue;

            if (first == m_rangeSearchStart)
                m_rangeSearchStart = first + count;
            for (uint32_t i = 0; i < count; ++i)
                m_allocated[first + i] = 1;
            return first;
        }

        PE_ERROR_IF(m_nextSlot + count > m_capacity,
                    "Dx12DescriptorHeap(%s) exhausted (%u descriptors, requested range %u)",
                    HeapTypeName(m_type),
                    m_capacity,
                    count);

        const uint32_t first = m_nextSlot;
        m_nextSlot += count;
        for (uint32_t i = 0; i < count; ++i)
            m_allocated[first + i] = 1;
        return first;
    }

    void Dx12DescriptorHeap::Free(uint32_t slot)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        FreeLocked(slot);
    }

    void Dx12DescriptorHeap::FreeLocked(uint32_t slot)
    {
        ValidateSlot(slot);
        PE_ERROR_IF(m_allocated[slot] == 0, "Dx12DescriptorHeap(%s): slot %u is already free", HeapTypeName(m_type), slot);
        m_allocated[slot] = 0;
        m_freeList.push_back(slot);
        m_rangeSearchStart = std::min(m_rangeSearchStart, slot);
    }

    void Dx12DescriptorHeap::FreeRange(uint32_t firstSlot, uint32_t count)
    {
        PE_ERROR_IF(count == 0, "Dx12DescriptorHeap(%s): cannot free an empty range", HeapTypeName(m_type));
        std::lock_guard<std::mutex> lock(m_mutex);
        if (count == 1)
        {
            FreeLocked(firstSlot);
            return;
        }

        ValidateSlot(firstSlot);
        ValidateSlot(firstSlot + count - 1);

        for (uint32_t i = 0; i < count; ++i)
        {
            const uint32_t slot = firstSlot + i;
            PE_ERROR_IF(m_allocated[slot] == 0, "Dx12DescriptorHeap(%s): slot %u is already free", HeapTypeName(m_type), slot);
            m_allocated[slot] = 0;
        }
        // Not pushed to m_freeList: ranges are re-taken through the hint scan, which would leave the
        // entries stale and grow the list every frame (transient blit descriptors).
        m_rangeSearchStart = std::min(m_rangeSearchStart, firstSlot);
    }

    D3D12_CPU_DESCRIPTOR_HANDLE Dx12DescriptorHeap::GetCpuHandle(uint32_t slot) const
    {
        ValidateSlot(slot);
        D3D12_CPU_DESCRIPTOR_HANDLE handle = m_cpuStart;
        handle.ptr += static_cast<SIZE_T>(slot) * m_stride;
        return handle;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE Dx12DescriptorHeap::GetGpuHandle(uint32_t slot) const
    {
        PE_ERROR_IF(!m_shaderVisible, "Dx12DescriptorHeap(%s) has no GPU handles", HeapTypeName(m_type));
        ValidateSlot(slot);
        D3D12_GPU_DESCRIPTOR_HANDLE handle = m_gpuStart;
        handle.ptr += static_cast<UINT64>(slot) * m_stride;
        return handle;
    }

    void Dx12DescriptorHeap::ValidateSlot(uint32_t slot) const
    {
        PE_ERROR_IF(slot >= m_capacity,
                    "Dx12DescriptorHeap(%s): slot %u exceeds capacity %u",
                    HeapTypeName(m_type),
                    slot,
                    m_capacity);
    }
} // namespace pe
