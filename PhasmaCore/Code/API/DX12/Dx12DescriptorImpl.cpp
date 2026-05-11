#include "API/DX12/Dx12DescriptorImpl.h"

#include "API/Buffer.h"
#include "API/DX12/Dx12BufferImpl.h"
#include "API/DX12/Dx12DescriptorHeap.h"
#include "API/DX12/Dx12ImageViewImpl.h"
#include "API/DX12/Dx12RhiImpl.h"
#include "API/DX12/Dx12SamplerImpl.h"
#include "API/RHI.h"

namespace pe
{
    namespace
    {
        uint64_t BufferRangeBytes(Buffer *buffer, const DescriptorUpdateInfo &updateInfo, uint32_t index)
        {
            const uint64_t offset = index < updateInfo.offsets.size() ? updateInfo.offsets[index] : 0;
            const uint64_t requested = index < updateInfo.ranges.size() ? updateInfo.ranges[index] : 0;
            if (requested > 0)
                return requested;
            return buffer && buffer->Size() > offset ? buffer->Size() - offset : 0;
        }

        uint32_t AlignCbvSize(uint64_t size)
        {
            constexpr uint64_t alignment = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
            return static_cast<uint32_t>((size + alignment - 1) & ~(alignment - 1));
        }

        uint64_t BufferOffsetBytes(const DescriptorUpdateInfo &updateInfo, uint32_t index)
        {
            return index < updateInfo.offsets.size() ? updateInfo.offsets[index] : 0;
        }

        D3D12_BUFFER_UAV RawBufferUav(const DescriptorUpdateInfo &updateInfo, uint32_t index, uint64_t rangeBytes)
        {
            const uint64_t offset = BufferOffsetBytes(updateInfo, index);
            D3D12_BUFFER_UAV uav{};
            uav.FirstElement = offset / sizeof(uint32_t);
            uav.NumElements = static_cast<UINT>(rangeBytes / sizeof(uint32_t));
            uav.StructureByteStride = 0;
            uav.CounterOffsetInBytes = 0;
            uav.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
            return uav;
        }

        D3D12_BUFFER_SRV RawBufferSrv(const DescriptorUpdateInfo &updateInfo, uint32_t index, uint64_t rangeBytes)
        {
            const uint64_t offset = BufferOffsetBytes(updateInfo, index);
            D3D12_BUFFER_SRV srv{};
            srv.FirstElement = offset / sizeof(uint32_t);
            srv.NumElements = static_cast<UINT>(rangeBytes / sizeof(uint32_t));
            srv.StructureByteStride = 0;
            srv.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
            return srv;
        }

        D3D12_BUFFER_UAV RawBufferUav(uint64_t offset, uint64_t rangeBytes)
        {
            D3D12_BUFFER_UAV uav{};
            uav.FirstElement = offset / sizeof(uint32_t);
            uav.NumElements = static_cast<UINT>(rangeBytes / sizeof(uint32_t));
            uav.StructureByteStride = 0;
            uav.CounterOffsetInBytes = 0;
            uav.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
            return uav;
        }

        D3D12_BUFFER_SRV RawBufferSrv(uint64_t offset, uint64_t rangeBytes)
        {
            D3D12_BUFFER_SRV srv{};
            srv.FirstElement = offset / sizeof(uint32_t);
            srv.NumElements = static_cast<UINT>(rangeBytes / sizeof(uint32_t));
            srv.StructureByteStride = 0;
            srv.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
            return srv;
        }

        D3D12_BUFFER_UAV StructuredBufferUav(const DescriptorUpdateInfo &updateInfo,
                                             const DescriptorBindingInfo &bindingInfo,
                                             uint32_t index,
                                             uint64_t rangeBytes)
        {
            const uint32_t stride = bindingInfo.structuredStride;
            const uint64_t offset = BufferOffsetBytes(updateInfo, index);
            PE_ERROR_IF(stride == 0, "DX12 structured UAV '%s' has no reflected element stride", bindingInfo.name.c_str());
            PE_ERROR_IF(offset % stride != 0, "DX12 structured UAV '%s' offset is not stride-aligned", bindingInfo.name.c_str());
            PE_ERROR_IF(rangeBytes < stride, "DX12 structured UAV '%s' range is smaller than one element", bindingInfo.name.c_str());

            D3D12_BUFFER_UAV uav{};
            uav.FirstElement = offset / stride;
            uav.NumElements = static_cast<UINT>(rangeBytes / stride);
            uav.StructureByteStride = stride;
            uav.CounterOffsetInBytes = 0;
            uav.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
            return uav;
        }

        D3D12_BUFFER_SRV StructuredBufferSrv(const DescriptorUpdateInfo &updateInfo,
                                             const DescriptorBindingInfo &bindingInfo,
                                             uint32_t index,
                                             uint64_t rangeBytes)
        {
            const uint32_t stride = bindingInfo.structuredStride;
            const uint64_t offset = BufferOffsetBytes(updateInfo, index);
            PE_ERROR_IF(stride == 0, "DX12 structured SRV '%s' has no reflected element stride", bindingInfo.name.c_str());
            PE_ERROR_IF(offset % stride != 0, "DX12 structured SRV '%s' offset is not stride-aligned", bindingInfo.name.c_str());
            PE_ERROR_IF(rangeBytes < stride, "DX12 structured SRV '%s' range is smaller than one element", bindingInfo.name.c_str());

            D3D12_BUFFER_SRV srv{};
            srv.FirstElement = offset / stride;
            srv.NumElements = static_cast<UINT>(rangeBytes / stride);
            srv.StructureByteStride = stride;
            srv.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
            return srv;
        }

        D3D12_BUFFER_UAV StructuredBufferUav(uint64_t offset,
                                             const DescriptorBindingInfo &bindingInfo,
                                             uint64_t rangeBytes)
        {
            const uint32_t stride = bindingInfo.structuredStride;
            PE_ERROR_IF(stride == 0, "DX12 structured UAV '%s' has no reflected element stride", bindingInfo.name.c_str());
            PE_ERROR_IF(offset % stride != 0, "DX12 structured UAV '%s' offset is not stride-aligned", bindingInfo.name.c_str());
            PE_ERROR_IF(rangeBytes < stride, "DX12 structured UAV '%s' range is smaller than one element", bindingInfo.name.c_str());

            D3D12_BUFFER_UAV uav{};
            uav.FirstElement = offset / stride;
            uav.NumElements = static_cast<UINT>(rangeBytes / stride);
            uav.StructureByteStride = stride;
            uav.CounterOffsetInBytes = 0;
            uav.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
            return uav;
        }

        D3D12_BUFFER_SRV StructuredBufferSrv(uint64_t offset,
                                             const DescriptorBindingInfo &bindingInfo,
                                             uint64_t rangeBytes)
        {
            const uint32_t stride = bindingInfo.structuredStride;
            PE_ERROR_IF(stride == 0, "DX12 structured SRV '%s' has no reflected element stride", bindingInfo.name.c_str());
            PE_ERROR_IF(offset % stride != 0, "DX12 structured SRV '%s' offset is not stride-aligned", bindingInfo.name.c_str());
            PE_ERROR_IF(rangeBytes < stride, "DX12 structured SRV '%s' range is smaller than one element", bindingInfo.name.c_str());

            D3D12_BUFFER_SRV srv{};
            srv.FirstElement = offset / stride;
            srv.NumElements = static_cast<UINT>(rangeBytes / stride);
            srv.StructureByteStride = stride;
            srv.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
            return srv;
        }

        bool IsCbvSrvUavDescriptor(PeBindingType type)
        {
            return type != PE_DESCRIPTOR_TYPE_SAMPLER;
        }

        bool IsSamplerDescriptor(PeBindingType type)
        {
            return type == PE_DESCRIPTOR_TYPE_SAMPLER || type == PE_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        }

        uint32_t CbvSrvUavTableOffset(const DescriptorBindingInfo &info)
        {
            switch (info.type)
            {
            case PE_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
            case PE_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
            {
                const uint32_t cbvBase = Dx12CbvBaseRegister(info.dxSpace);
                PE_ERROR_IF(info.dxSpace == 0 && info.dxRegister == 0,
                            "DX12 CBV b0/space0 is reserved for push constants");
                PE_ERROR_IF(info.dxRegister < cbvBase, "DX12 CBV register is below the table base");
                return DX12_CBV_TABLE_OFFSET + (info.dxRegister - cbvBase);
            }
            case PE_DESCRIPTOR_TYPE_STORAGE_IMAGE:
            case PE_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
            case PE_DESCRIPTOR_TYPE_STORAGE_BUFFER:
            case PE_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
                return DX12_UAV_TABLE_OFFSET + info.dxRegister;
            default:
                return DX12_SRV_TABLE_OFFSET + info.dxRegister;
            }
        }

        uint32_t CbvSrvUavTableEnd(const DescriptorBindingInfo &info)
        {
            if (!IsCbvSrvUavDescriptor(info.type))
                return 0;
            return CbvSrvUavTableOffset(info) + std::max(1u, info.count);
        }

        uint32_t SamplerTableEnd(const DescriptorBindingInfo &info)
        {
            if (!IsSamplerDescriptor(info.type))
                return 0;
            return info.dxRegister + std::max(1u, info.count);
        }

        void ValidateDescriptorCapacity(const DescriptorBindingInfo &info)
        {
            const uint32_t descriptorCount = std::max(1u, info.count);
            PE_ERROR_IF(info.dxRegister == static_cast<uint32_t>(-1),
                        "DX12 descriptor binding '%s' has no DX register",
                        info.name.empty() ? "<unnamed>" : info.name.c_str());

            auto errorIfOverflow = [&](uint64_t registerEnd, const char *rangeType)
            {
                PE_ERROR_IF(registerEnd > DX12_DESCRIPTORS_PER_TYPE,
                            "DX12 %s binding '%s' at register %u/space%u with count %u exceeds per-space capacity %u",
                            rangeType,
                            info.name.empty() ? "<unnamed>" : info.name.c_str(),
                            info.dxRegister,
                            info.dxSpace,
                            descriptorCount,
                            DX12_DESCRIPTORS_PER_TYPE);
            };

            switch (info.type)
            {
            case PE_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
            case PE_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
            {
                const uint32_t cbvBase = Dx12CbvBaseRegister(info.dxSpace);
                PE_ERROR_IF(info.dxSpace == 0 && info.dxRegister == 0,
                            "DX12 CBV b0/space0 is reserved for push constants");
                PE_ERROR_IF(info.dxRegister < cbvBase, "DX12 CBV register is below the table base");
                errorIfOverflow(static_cast<uint64_t>(info.dxRegister - cbvBase) + descriptorCount, "CBV");
                break;
            }
            case PE_DESCRIPTOR_TYPE_STORAGE_IMAGE:
            case PE_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
            case PE_DESCRIPTOR_TYPE_STORAGE_BUFFER:
            case PE_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
                errorIfOverflow(static_cast<uint64_t>(info.dxRegister) + descriptorCount, "UAV");
                break;
            default:
                if (IsCbvSrvUavDescriptor(info.type))
                    errorIfOverflow(static_cast<uint64_t>(info.dxRegister) + descriptorCount, "SRV");
                break;
            }

            if (IsSamplerDescriptor(info.type))
                errorIfOverflow(static_cast<uint64_t>(info.dxRegister) + descriptorCount, "sampler");
        }

        void WriteBufferDescriptor(ID3D12Device *device,
                                   D3D12_CPU_DESCRIPTOR_HANDLE dst,
                                   const DescriptorBindingInfo &bindingInfo,
                                   Buffer *buffer,
                                   uint64_t offset,
                                   uint64_t rangeBytes)
        {
            PE_ERROR_IF(!device, "Dx12DescriptorImpl: DX12 device unavailable");
            PE_ERROR_IF(!buffer, "Dx12DescriptorImpl: null buffer for binding %u", bindingInfo.binding);
            const Dx12BufferImpl *bufferImpl = Dx12BufferImpl::From(buffer);

            if (bindingInfo.type == PE_DESCRIPTOR_TYPE_UNIFORM_BUFFER ||
                bindingInfo.type == PE_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC)
            {
                D3D12_CONSTANT_BUFFER_VIEW_DESC cbv{};
                cbv.BufferLocation = bufferImpl->GetResource()->GetGPUVirtualAddress() + offset;
                cbv.SizeInBytes = AlignCbvSize(rangeBytes);
                device->CreateConstantBufferView(&cbv, dst);
            }
            else if (bindingInfo.type == PE_DESCRIPTOR_TYPE_STRUCTURED_BUFFER ||
                     bindingInfo.type == PE_DESCRIPTOR_TYPE_BYTE_ADDRESS_BUFFER)
            {
                const bool useStructured = bindingInfo.type == PE_DESCRIPTOR_TYPE_STRUCTURED_BUFFER &&
                                           bindingInfo.structuredStride > 0;
                D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
                srv.Format = useStructured ? DXGI_FORMAT_UNKNOWN : DXGI_FORMAT_R32_TYPELESS;
                srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
                srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                srv.Buffer = useStructured ? StructuredBufferSrv(offset, bindingInfo, rangeBytes)
                                           : RawBufferSrv(offset, rangeBytes);
                device->CreateShaderResourceView(bufferImpl->GetResource(), &srv, dst);
            }
            else
            {
                PE_ERROR_IF(!bufferImpl->AllowsUnorderedAccess(),
                            "Dx12DescriptorImpl: buffer '%s' is bound as a UAV but was not created with ALLOW_UNORDERED_ACCESS. Use GPU-only memory for DX12 writable storage buffers.",
                            buffer ? buffer->GetName().c_str() : "<null>");
                D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
                uav.Format = bindingInfo.structuredStride > 0 ? DXGI_FORMAT_UNKNOWN : DXGI_FORMAT_R32_TYPELESS;
                uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
                uav.Buffer = bindingInfo.structuredStride > 0
                                 ? StructuredBufferUav(offset, bindingInfo, rangeBytes)
                                 : RawBufferUav(offset, rangeBytes);
                device->CreateUnorderedAccessView(bufferImpl->GetResource(), nullptr, &uav, dst);
            }
        }
    } // namespace

    Dx12DescriptorPoolImpl::Dx12DescriptorPoolImpl(DescriptorPool *owner, const DescriptorPoolDesc &desc)
        : m_owner{owner}, m_desc{desc}
    {
    }

    Dx12DescriptorLayoutImpl::Dx12DescriptorLayoutImpl(DescriptorLayout *owner)
        : m_owner{owner}
    {
    }

    Dx12DescriptorImpl::Dx12DescriptorImpl(Descriptor *owner)
        : m_owner{owner}
    {
        AllocateTables();
    }

    Dx12DescriptorImpl::~Dx12DescriptorImpl()
    {
        FreeTables();
    }

    Dx12DescriptorImpl::BindingSlots *Dx12DescriptorImpl::FindSlots(uint32_t binding)
    {
        for (BindingSlots &slots : m_slots)
        {
            if (slots.binding == binding)
                return &slots;
        }
        return nullptr;
    }

    const Dx12DescriptorImpl::BindingSlots *Dx12DescriptorImpl::FindSlots(uint32_t binding) const
    {
        for (const BindingSlots &slots : m_slots)
        {
            if (slots.binding == binding)
                return &slots;
        }
        return nullptr;
    }

    Dx12DescriptorImpl::TableSlots *Dx12DescriptorImpl::FindTable(uint32_t dxSpace)
    {
        for (TableSlots &table : m_tables)
        {
            if (table.dxSpace == dxSpace)
                return &table;
        }
        return nullptr;
    }

    const Dx12DescriptorImpl::TableSlots *Dx12DescriptorImpl::FindTable(uint32_t dxSpace) const
    {
        for (const TableSlots &table : m_tables)
        {
            if (table.dxSpace == dxSpace)
                return &table;
        }
        return nullptr;
    }

    void Dx12DescriptorImpl::AllocateTables()
    {
        auto *rhi = static_cast<Dx12RhiImpl *>(RHII.GetImpl());
        PE_ERROR_IF(!rhi || !rhi->GetCbvSrvUavHeap() || !rhi->GetSamplerHeap(), "Dx12DescriptorImpl requires DX12 descriptor heaps");

        struct Required
        {
            uint32_t cbvSrvUavCount = 0;
            uint32_t samplerCount = 0;
        };
        std::unordered_map<uint32_t, Required> requiredBySpace;

        for (const DescriptorBindingInfo &info : m_owner->GetBindingInfos())
        {
            PE_ERROR_IF(info.dxSpace >= DX12_DESCRIPTOR_SPACE_COUNT,
                        "DX12 descriptor space %u exceeds supported count %u",
                        info.dxSpace,
                        DX12_DESCRIPTOR_SPACE_COUNT);
            ValidateDescriptorCapacity(info);

            Required &required = requiredBySpace[info.dxSpace];
            required.cbvSrvUavCount = std::max(required.cbvSrvUavCount, CbvSrvUavTableEnd(info));
            required.samplerCount = std::max(required.samplerCount, SamplerTableEnd(info));
        }

        for (const auto &[space, required] : requiredBySpace)
        {
            TableSlots table{};
            table.dxSpace = space;
            if (required.cbvSrvUavCount > 0)
            {
                table.cbvSrvUavBaseSlot = rhi->GetCbvSrvUavHeap()->AllocateRange(required.cbvSrvUavCount);
                table.cbvSrvUavCount = required.cbvSrvUavCount;
            }
            if (required.samplerCount > 0)
            {
                table.samplerBaseSlot = rhi->GetSamplerHeap()->AllocateRange(required.samplerCount);
                table.samplerCount = required.samplerCount;
            }
            m_tables.push_back(table);
        }

        for (const DescriptorBindingInfo &info : m_owner->GetBindingInfos())
        {
            TableSlots *table = FindTable(info.dxSpace);
            PE_ERROR_IF(!table, "DX12 descriptor table was not allocated");

            BindingSlots slots{};
            slots.binding = info.binding;
            slots.dxRegister = info.dxRegister;
            slots.dxSpace = info.dxSpace;
            if (IsCbvSrvUavDescriptor(info.type))
            {
                const uint32_t offset = CbvSrvUavTableOffset(info);
                slots.cbvSrvUavSlots.resize(std::max(1u, info.count));
                for (uint32_t i = 0; i < slots.cbvSrvUavSlots.size(); ++i)
                    slots.cbvSrvUavSlots[i] = table->cbvSrvUavBaseSlot + offset + i;
            }
            if (IsSamplerDescriptor(info.type))
            {
                slots.samplerSlots.resize(std::max(1u, info.count));
                for (uint32_t i = 0; i < slots.samplerSlots.size(); ++i)
                    slots.samplerSlots[i] = table->samplerBaseSlot + info.dxRegister + i;
            }
            m_slots.push_back(std::move(slots));
        }
    }

    void Dx12DescriptorImpl::FreeTables()
    {
        auto *rhi = static_cast<Dx12RhiImpl *>(RHII.GetImpl());
        if (!rhi)
            return;

        for (TableSlots &table : m_tables)
        {
            if (table.cbvSrvUavBaseSlot != InvalidSlot && table.cbvSrvUavCount > 0 && rhi->GetCbvSrvUavHeap())
                rhi->GetCbvSrvUavHeap()->FreeRange(table.cbvSrvUavBaseSlot, table.cbvSrvUavCount);
            if (table.samplerBaseSlot != InvalidSlot && table.samplerCount > 0 && rhi->GetSamplerHeap())
                rhi->GetSamplerHeap()->FreeRange(table.samplerBaseSlot, table.samplerCount);
        }
        m_tables.clear();
        m_slots.clear();
    }

    uint32_t Dx12DescriptorImpl::GetCbvSrvUavSlot(uint32_t binding, uint32_t arrayIndex) const
    {
        const BindingSlots *slots = FindSlots(binding);
        if (!slots || arrayIndex >= slots->cbvSrvUavSlots.size())
            return InvalidSlot;
        return slots->cbvSrvUavSlots[arrayIndex];
    }

    uint32_t Dx12DescriptorImpl::GetSamplerSlot(uint32_t binding, uint32_t arrayIndex) const
    {
        const BindingSlots *slots = FindSlots(binding);
        if (!slots || arrayIndex >= slots->samplerSlots.size())
            return InvalidSlot;
        return slots->samplerSlots[arrayIndex];
    }

    bool Dx12DescriptorImpl::GetCbvSrvUavTableInfo(uint32_t dxSpace, uint32_t &baseSlot, uint32_t &count) const
    {
        const TableSlots *table = FindTable(dxSpace);
        if (!table || table->cbvSrvUavBaseSlot == InvalidSlot || table->cbvSrvUavCount == 0)
            return false;
        baseSlot = table->cbvSrvUavBaseSlot;
        count = table->cbvSrvUavCount;
        return true;
    }

    bool Dx12DescriptorImpl::CreateDynamicCbvSrvUavTable(uint32_t dxSpace,
                                                         const DynamicBufferBinding *dynamicBindings,
                                                         uint32_t dynamicBindingCount,
                                                         uint32_t &firstSlot,
                                                         uint32_t &count,
                                                         D3D12_GPU_DESCRIPTOR_HANDLE &gpuHandle) const
    {
        auto *rhi = static_cast<Dx12RhiImpl *>(RHII.GetImpl());
        if (!rhi || !rhi->GetDevice() || !rhi->GetCbvSrvUavHeap())
            return false;

        uint32_t sourceBase = InvalidSlot;
        uint32_t tableCount = 0;
        if (!GetCbvSrvUavTableInfo(dxSpace, sourceBase, tableCount))
            return false;

        auto findDynamicBinding = [&](uint32_t binding) -> const DynamicBufferBinding *
        {
            for (uint32_t i = 0; i < dynamicBindingCount; ++i)
                if (dynamicBindings[i].binding == binding)
                    return &dynamicBindings[i];
            return nullptr;
        };

        Dx12DescriptorHeap *heap = rhi->GetCbvSrvUavHeap();
        const uint32_t dynamicBase = heap->AllocateRange(tableCount);
        auto freeOnFailure = [&]()
        {
            heap->FreeRange(dynamicBase, tableCount);
        };

        const std::vector<DescriptorBindingInfo> &bindingInfos = m_owner->GetBindingInfos();
        const std::vector<DescriptorUpdateInfo> &boundResources = m_owner->GetBoundResources();

        for (uint32_t i = 0; i < bindingInfos.size(); ++i)
        {
            const DescriptorBindingInfo &bindingInfo = bindingInfos[i];
            if (bindingInfo.dxSpace != dxSpace || !IsCbvSrvUavDescriptor(bindingInfo.type))
                continue;

            const DynamicBufferBinding *dynamicBinding = findDynamicBinding(bindingInfo.binding);
            if (dynamicBinding)
            {
                const uint32_t sourceSlot = GetCbvSrvUavSlot(bindingInfo.binding);
                if (sourceSlot == InvalidSlot || sourceSlot < sourceBase || sourceSlot >= sourceBase + tableCount)
                {
                    freeOnFailure();
                    return false;
                }
                const D3D12_CPU_DESCRIPTOR_HANDLE dst =
                    heap->GetCpuHandle(dynamicBase + (sourceSlot - sourceBase));
                WriteBufferDescriptor(rhi->GetDevice(), dst, bindingInfo, dynamicBinding->buffer,
                                      dynamicBinding->offset, dynamicBinding->range);
                continue;
            }

            if (i >= boundResources.size())
                continue;

            const DescriptorUpdateInfo &updateInfo = boundResources[i];
            if (!updateInfo.views.empty())
            {
                for (uint32_t j = 0; j < updateInfo.views.size(); ++j)
                {
                    const uint32_t sourceSlot = GetCbvSrvUavSlot(updateInfo.binding, j);
                    if (sourceSlot == InvalidSlot || sourceSlot < sourceBase || sourceSlot >= sourceBase + tableCount)
                    {
                        freeOnFailure();
                        return false;
                    }
                    const auto *view = Dx12ImageViewImpl::From(updateInfo.views[j]);
                    rhi->GetDevice()->CopyDescriptorsSimple(
                        1,
                        heap->GetCpuHandle(dynamicBase + (sourceSlot - sourceBase)),
                        view->GetCpuHandle(),
                        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                }
            }
            else if (!updateInfo.buffers.empty())
            {
                for (uint32_t j = 0; j < updateInfo.buffers.size(); ++j)
                {
                    const uint32_t sourceSlot = GetCbvSrvUavSlot(updateInfo.binding, j);
                    if (sourceSlot == InvalidSlot || sourceSlot < sourceBase || sourceSlot >= sourceBase + tableCount)
                    {
                        freeOnFailure();
                        return false;
                    }
                    Buffer *buffer = updateInfo.buffers[j];
                    const uint64_t offset = j < updateInfo.offsets.size() ? updateInfo.offsets[j] : 0;
                    const uint64_t rangeBytes = BufferRangeBytes(buffer, updateInfo, j);
                    const D3D12_CPU_DESCRIPTOR_HANDLE dst =
                        heap->GetCpuHandle(dynamicBase + (sourceSlot - sourceBase));
                    WriteBufferDescriptor(rhi->GetDevice(), dst, bindingInfo, buffer, offset, rangeBytes);
                }
            }
        }

        firstSlot = dynamicBase;
        count = tableCount;
        gpuHandle = heap->GetGpuHandle(dynamicBase);
        return true;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE Dx12DescriptorImpl::GetCbvSrvUavGpuHandle(uint32_t binding, uint32_t arrayIndex) const
    {
        auto *rhi = static_cast<Dx12RhiImpl *>(RHII.GetImpl());
        const uint32_t slot = GetCbvSrvUavSlot(binding, arrayIndex);
        if (!rhi || !rhi->GetCbvSrvUavHeap() || slot == InvalidSlot)
            return {};
        return rhi->GetCbvSrvUavHeap()->GetGpuHandle(slot);
    }

    D3D12_GPU_DESCRIPTOR_HANDLE Dx12DescriptorImpl::GetSamplerGpuHandle(uint32_t binding, uint32_t arrayIndex) const
    {
        auto *rhi = static_cast<Dx12RhiImpl *>(RHII.GetImpl());
        const uint32_t slot = GetSamplerSlot(binding, arrayIndex);
        if (!rhi || !rhi->GetSamplerHeap() || slot == InvalidSlot)
            return {};
        return rhi->GetSamplerHeap()->GetGpuHandle(slot);
    }

    D3D12_GPU_DESCRIPTOR_HANDLE Dx12DescriptorImpl::GetCbvSrvUavTableGpuHandle(uint32_t dxSpace) const
    {
        auto *rhi = static_cast<Dx12RhiImpl *>(RHII.GetImpl());
        const TableSlots *table = FindTable(dxSpace);
        if (!rhi || !rhi->GetCbvSrvUavHeap() || !table || table->cbvSrvUavBaseSlot == InvalidSlot)
            return {};
        return rhi->GetCbvSrvUavHeap()->GetGpuHandle(table->cbvSrvUavBaseSlot);
    }

    D3D12_GPU_DESCRIPTOR_HANDLE Dx12DescriptorImpl::GetSamplerTableGpuHandle(uint32_t dxSpace) const
    {
        auto *rhi = static_cast<Dx12RhiImpl *>(RHII.GetImpl());
        const TableSlots *table = FindTable(dxSpace);
        if (!rhi || !rhi->GetSamplerHeap() || !table || table->samplerBaseSlot == InvalidSlot)
            return {};
        return rhi->GetSamplerHeap()->GetGpuHandle(table->samplerBaseSlot);
    }

    void Dx12DescriptorImpl::Update(const std::vector<DescriptorBindingInfo> &bindingInfos,
                                    const std::vector<DescriptorUpdateInfo> &updateInfos)
    {
        auto *rhi = static_cast<Dx12RhiImpl *>(RHII.GetImpl());
        PE_ERROR_IF(!rhi || !rhi->GetDevice(), "Dx12DescriptorImpl requires an initialized DX12 device");

        ID3D12Device *device = rhi->GetDevice();
        Dx12DescriptorHeap *cbvSrvUavHeap = rhi->GetCbvSrvUavHeap();

        for (uint32_t i = 0; i < updateInfos.size(); i++)
        {
            const DescriptorUpdateInfo &updateInfo = updateInfos[i];
            const DescriptorBindingInfo &bindingInfo = bindingInfos[i];

            if (updateInfo.views.empty() && updateInfo.buffers.empty() && updateInfo.samplers.empty() &&
                updateInfo.accelerationStructures.empty())
            {
                continue;
            }

            BindingSlots *slots = FindSlots(updateInfo.binding);
            PE_ERROR_IF(!slots, "Dx12DescriptorImpl: update binding has no allocated DX12 slots");

            if (!updateInfo.views.empty())
            {
                for (uint32_t j = 0; j < updateInfo.views.size(); j++)
                {
                    PE_ERROR_IF(!updateInfo.views[j], "Dx12DescriptorImpl: null image view for binding %u", updateInfo.binding);
                    PE_ERROR_IF(j >= slots->cbvSrvUavSlots.size(), "Dx12DescriptorImpl: image descriptor array exceeds reflected count");
                    const auto *view = Dx12ImageViewImpl::From(updateInfo.views[j]);
                    const uint32_t slot = slots->cbvSrvUavSlots[j];
                    device->CopyDescriptorsSimple(1,
                                                  cbvSrvUavHeap->GetCpuHandle(slot),
                                                  view->GetCpuHandle(),
                                                  D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                }

                if (bindingInfo.type == PE_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                {
                    for (uint32_t j = 0; j < updateInfo.samplers.size(); j++)
                    {
                        PE_ERROR_IF(j >= slots->samplerSlots.size(), "Dx12DescriptorImpl: sampler descriptor array exceeds reflected count");
                        const Dx12SamplerImpl *sampler = Dx12SamplerImpl::TryFrom(updateInfo.samplers[j]);
                        if (sampler)
                        {
                            device->CopyDescriptorsSimple(1,
                                                          rhi->GetSamplerHeap()->GetCpuHandle(slots->samplerSlots[j]),
                                                          sampler->GetCpuHandle(),
                                                          D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
                        }
                    }
                }
            }
            else if (!updateInfo.buffers.empty())
            {
                for (uint32_t j = 0; j < updateInfo.buffers.size(); j++)
                {
                    PE_ERROR_IF(j >= slots->cbvSrvUavSlots.size(), "Dx12DescriptorImpl: buffer descriptor array exceeds reflected count");
                    Buffer *buffer = updateInfo.buffers[j];
                    PE_ERROR_IF(!buffer, "Dx12DescriptorImpl: null buffer for binding %u", updateInfo.binding);
                    const Dx12BufferImpl *bufferImpl = Dx12BufferImpl::From(buffer);
                    const uint64_t offset = j < updateInfo.offsets.size() ? updateInfo.offsets[j] : 0;
                    const uint64_t rangeBytes = BufferRangeBytes(buffer, updateInfo, j);
                    const D3D12_CPU_DESCRIPTOR_HANDLE dst = cbvSrvUavHeap->GetCpuHandle(slots->cbvSrvUavSlots[j]);

                    if (bindingInfo.type == PE_DESCRIPTOR_TYPE_UNIFORM_BUFFER ||
                        bindingInfo.type == PE_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC)
                    {
                        D3D12_CONSTANT_BUFFER_VIEW_DESC cbv{};
                        cbv.BufferLocation = bufferImpl->GetResource()->GetGPUVirtualAddress() + offset;
                        cbv.SizeInBytes = AlignCbvSize(rangeBytes);
                        device->CreateConstantBufferView(&cbv, dst);
                    }
                    else if (bindingInfo.type == PE_DESCRIPTOR_TYPE_STRUCTURED_BUFFER ||
                             bindingInfo.type == PE_DESCRIPTOR_TYPE_BYTE_ADDRESS_BUFFER)
                    {
                        const bool useStructured = bindingInfo.type == PE_DESCRIPTOR_TYPE_STRUCTURED_BUFFER &&
                                                   bindingInfo.structuredStride > 0;
                        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
                        srv.Format = useStructured ? DXGI_FORMAT_UNKNOWN : DXGI_FORMAT_R32_TYPELESS;
                        srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
                        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                        srv.Buffer = useStructured ? StructuredBufferSrv(updateInfo, bindingInfo, j, rangeBytes)
                                                   : RawBufferSrv(updateInfo, j, rangeBytes);
                        device->CreateShaderResourceView(bufferImpl->GetResource(), &srv, dst);
                    }
                    else
                    {
                        PE_ERROR_IF(!bufferImpl->AllowsUnorderedAccess(),
                                    "Dx12DescriptorImpl: buffer '%s' is bound as a UAV but was not created with ALLOW_UNORDERED_ACCESS. Use GPU-only memory for DX12 writable storage buffers.",
                                    buffer ? buffer->GetName().c_str() : "<null>");
                        D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
                        uav.Format = bindingInfo.structuredStride > 0 ? DXGI_FORMAT_UNKNOWN : DXGI_FORMAT_R32_TYPELESS;
                        uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
                        uav.Buffer = bindingInfo.structuredStride > 0
                                         ? StructuredBufferUav(updateInfo, bindingInfo, j, rangeBytes)
                                         : RawBufferUav(updateInfo, j, rangeBytes);
                        device->CreateUnorderedAccessView(bufferImpl->GetResource(), nullptr, &uav, dst);
                    }
                }
            }
            else if (!updateInfo.samplers.empty() && bindingInfo.type != PE_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
            {
                for (uint32_t j = 0; j < updateInfo.samplers.size(); j++)
                {
                    PE_ERROR_IF(j >= slots->samplerSlots.size(), "Dx12DescriptorImpl: sampler descriptor array exceeds reflected count");
                    const Dx12SamplerImpl *sampler = Dx12SamplerImpl::TryFrom(updateInfo.samplers[j]);
                    if (sampler)
                    {
                        device->CopyDescriptorsSimple(1,
                                                      rhi->GetSamplerHeap()->GetCpuHandle(slots->samplerSlots[j]),
                                                      sampler->GetCpuHandle(),
                                                      D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
                    }
                }
            }
            else if (!updateInfo.accelerationStructures.empty())
            {
                PE_ERROR("Dx12DescriptorImpl: acceleration-structure descriptors wait for the DXR slice");
            }
        }
    }
} // namespace pe
