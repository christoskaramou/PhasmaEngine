#include "API/DX12/Dx12BufferImpl.h"

#include "API/DX12/Dx12CommandBufferImpl.h"
#include "API/DX12/Dx12RhiImpl.h"
#include "API/RHI.h"

namespace pe
{
    namespace
    {
        D3D12_HEAP_TYPE ToD3D12HeapType(PeMemoryUsage usage)
        {
            switch (usage)
            {
            case PE_MEMORY_USAGE_GPU_ONLY:
            case PE_MEMORY_USAGE_GPU_ONLY_DEDICATED:
                return D3D12_HEAP_TYPE_DEFAULT;
            case PE_MEMORY_USAGE_CPU_TO_GPU:
            case PE_MEMORY_USAGE_CPU_TO_GPU_PERSISTENT:
                return D3D12_HEAP_TYPE_UPLOAD;
            case PE_MEMORY_USAGE_GPU_TO_CPU:
            case PE_MEMORY_USAGE_GPU_TO_CPU_PERSISTENT:
                return D3D12_HEAP_TYPE_READBACK;
            default:
                return D3D12_HEAP_TYPE_DEFAULT;
            }
        }

        bool IsAccelerationStructureStorage(PeBufferUsageFlags usage)
        {
            return (usage & PE_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_KHR) != 0;
        }

        D3D12_RESOURCE_STATES InitialStateForHeap(D3D12_HEAP_TYPE heapType, PeBufferUsageFlags usage)
        {
            if (IsAccelerationStructureStorage(usage))
                return D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;

            switch (heapType)
            {
            case D3D12_HEAP_TYPE_UPLOAD:
                return D3D12_RESOURCE_STATE_GENERIC_READ;
            case D3D12_HEAP_TYPE_READBACK:
                return D3D12_RESOURCE_STATE_COPY_DEST;
            default:
                return D3D12_RESOURCE_STATE_COMMON;
            }
        }

        bool NeedsUnorderedAccess(PeBufferUsageFlags usage)
        {
            return usage & (PE_BUFFER_USAGE_STORAGE_BUFFER |
                            PE_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_KHR |
                            PE_BUFFER_USAGE_SHADER_BINDING_TABLE_KHR);
        }

        D3D12_RESOURCE_FLAGS ToD3D12ResourceFlags(PeBufferUsageFlags usage, D3D12_HEAP_TYPE heapType)
        {
            D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;
            if (NeedsUnorderedAccess(usage) && heapType == D3D12_HEAP_TYPE_DEFAULT)
            {
                flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            }
            return flags;
        }

        D3D12_RESOURCE_DESC BufferResourceDesc(size_t size, PeBufferUsageFlags usage, D3D12_HEAP_TYPE heapType)
        {
            D3D12_RESOURCE_DESC desc{};
            desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            desc.Alignment = 0;
            desc.Width = static_cast<UINT64>(size);
            desc.Height = 1;
            desc.DepthOrArraySize = 1;
            desc.MipLevels = 1;
            desc.Format = DXGI_FORMAT_UNKNOWN;
            desc.SampleDesc.Count = 1;
            desc.SampleDesc.Quality = 0;
            desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            desc.Flags = ToD3D12ResourceFlags(usage, heapType);
            return desc;
        }
    } // namespace

    Dx12BufferImpl::Dx12BufferImpl(Buffer *owner, const BufferDesc &desc)
        : m_owner{owner},
          m_heapType{ToD3D12HeapType(desc.memoryUsage)}
    {
        auto *rhi = static_cast<Dx12RhiImpl *>(RHII.GetImpl());
        PE_ERROR_IF(!rhi || !rhi->GetAllocator(), "Dx12BufferImpl requires an initialized DX12 allocator");

        D3D12MA::ALLOCATION_DESC allocationDesc{};
        allocationDesc.HeapType = m_heapType;
        if (desc.memoryUsage == PE_MEMORY_USAGE_GPU_ONLY_DEDICATED)
            allocationDesc.Flags = D3D12MA::ALLOCATION_FLAG_COMMITTED;

        D3D12_RESOURCE_DESC resourceDesc = BufferResourceDesc(owner->m_size, desc.usage, m_heapType);
        PE_ERROR_IF(IsAccelerationStructureStorage(desc.usage) && m_heapType != D3D12_HEAP_TYPE_DEFAULT,
                    "Dx12BufferImpl: acceleration-structure storage buffers must use a default heap");
        m_allowsUnorderedAccess = (resourceDesc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) != 0;
        m_state = InitialStateForHeap(m_heapType, desc.usage);
        HRESULT hr = rhi->GetAllocator()->CreateResource(&allocationDesc,
                                                         &resourceDesc,
                                                         m_state,
                                                         nullptr,
                                                         &m_allocation,
                                                         IID_PPV_ARGS(&m_resource));
        PE_ERROR_IF(FAILED(hr),
                    "Dx12BufferImpl: CreateResource failed (0x%08X), size=%zu, usage=0x%llX, memoryUsage=%u, heapType=%u, flags=0x%X, name=%s",
                    static_cast<unsigned>(hr),
                    owner->m_size,
                    static_cast<unsigned long long>(desc.usage),
                    static_cast<unsigned>(desc.memoryUsage),
                    static_cast<unsigned>(m_heapType),
                    static_cast<unsigned>(resourceDesc.Flags),
                    desc.name.c_str());

        if (!desc.name.empty())
        {
            std::wstring name(desc.name.begin(), desc.name.end());
            m_resource->SetName(name.c_str());
            m_allocation->SetName(name.c_str());
        }
    }

    Dx12BufferImpl::~Dx12BufferImpl()
    {
        if (m_mapped)
            Unmap();
        m_resource.Reset();
        m_allocation.Reset();
    }

    void *Dx12BufferImpl::Map()
    {
        if (m_mapped)
            return m_mapped;

        D3D12_RANGE readRange{};
        D3D12_RANGE *range = (m_heapType == D3D12_HEAP_TYPE_UPLOAD) ? &readRange : nullptr;
        HRESULT hr = m_resource->Map(0, range, &m_mapped);
        PE_ERROR_IF(FAILED(hr), "Dx12BufferImpl: Map failed (0x%08X)", static_cast<unsigned>(hr));
        return m_mapped;
    }

    void Dx12BufferImpl::Unmap()
    {
        if (!m_mapped)
            return;
        m_resource->Unmap(0, nullptr);
        m_mapped = nullptr;
    }

    void Dx12BufferImpl::Flush(size_t /*size*/, size_t /*offset*/) const
    {
        // Upload/readback heaps are CPU-coherent in DX12; no explicit flush is needed.
    }

    uint64_t Dx12BufferImpl::GetDeviceAddress() const
    {
        return m_resource ? m_resource->GetGPUVirtualAddress() : 0;
    }

    void Dx12BufferImpl::CopyBuffer(CommandBuffer *cmd,
                                    Buffer *src,
                                    size_t size,
                                    size_t srcOffset,
                                    size_t dstOffset)
    {
        PE_ERROR_IF(!cmd, "Dx12BufferImpl::CopyBuffer: no command buffer specified");
        Dx12CommandBufferImpl::From(cmd)->CopyBuffer(src, m_owner, size, srcOffset, dstOffset);
    }
} // namespace pe
