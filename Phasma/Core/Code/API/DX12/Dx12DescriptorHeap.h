#pragma once

#include <d3d12.h>
#include <wrl/client.h>

namespace pe
{
    class Dx12DescriptorHeap
    {
    public:
        Dx12DescriptorHeap(ID3D12Device *device, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t capacity, bool shaderVisible);

        uint32_t Allocate();
        uint32_t AllocateRange(uint32_t count);
        void Free(uint32_t slot);
        void FreeRange(uint32_t firstSlot, uint32_t count);

        D3D12_CPU_DESCRIPTOR_HANDLE GetCpuHandle(uint32_t slot) const;
        D3D12_GPU_DESCRIPTOR_HANDLE GetGpuHandle(uint32_t slot) const;
        ID3D12DescriptorHeap *Get() const { return m_heap.Get(); }

        D3D12_DESCRIPTOR_HEAP_TYPE GetType() const { return m_type; }
        uint32_t GetCapacity() const { return m_capacity; }
        bool IsShaderVisible() const { return m_shaderVisible; }

    private:
        void ValidateSlot(uint32_t slot) const;
        uint32_t AllocateLocked();
        void FreeLocked(uint32_t slot);

        // Views, samplers and descriptor tables are created/destroyed from loader and render threads.
        std::mutex m_mutex;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_heap;
        std::vector<uint32_t> m_freeList;
        std::vector<uint8_t> m_allocated;
        D3D12_DESCRIPTOR_HEAP_TYPE m_type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        uint32_t m_capacity = 0;
        uint32_t m_stride = 0;
        uint32_t m_nextSlot = 0;
        uint32_t m_rangeSearchStart = 0; // always <= the lowest free slot
        D3D12_CPU_DESCRIPTOR_HANDLE m_cpuStart{};
        D3D12_GPU_DESCRIPTOR_HANDLE m_gpuStart{};
        bool m_shaderVisible = false;
    };
} // namespace pe
