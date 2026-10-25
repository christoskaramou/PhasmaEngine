#include "API/DX12/Dx12DescriptorImpl.h"

#include "API/AccelerationStructure.h"
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
            // The texture-table space is one unbounded SRV range from its first slot.
            if (info.dxSpace == PE_TEXTURE_TABLE_DX_SPACE)
                return info.dxRegister;

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

        uint32_t CbvSrvUavTableEnd(const DescriptorBindingInfo &info, uint32_t count)
        {
            if (!IsCbvSrvUavDescriptor(info.type))
                return 0;
            return CbvSrvUavTableOffset(info) + std::max(1u, count);
        }

        uint32_t SamplerTableEnd(const DescriptorBindingInfo &info, uint32_t count)
        {
            if (!IsSamplerDescriptor(info.type))
                return 0;
            return info.dxRegister + std::max(1u, count);
        }

        void ValidateDescriptorCapacity(const DescriptorBindingInfo &info, uint32_t count)
        {
            const uint32_t descriptorCount = std::max(1u, count);
            PE_ERROR_IF(info.dxRegister == static_cast<uint32_t>(-1),
                        "DX12 descriptor binding '%s' has no DX register",
                        info.name.empty() ? "<unnamed>" : info.name.c_str());

            if (info.dxSpace == PE_TEXTURE_TABLE_DX_SPACE)
            {
                PE_ERROR_IF(info.type != PE_DESCRIPTOR_TYPE_SAMPLED_IMAGE || info.dxRegister != 0,
                            "DX12 space %u holds only the scene texture array at t0 ('%s')",
                            PE_TEXTURE_TABLE_DX_SPACE,
                            info.name.c_str());
                return;
            }

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

        // Sampler tables are shared by content: every MaterialX pipeline binds the same OpenPBR samplers once per
        // frame in flight, and D3D12 caps the shader-visible sampler heap at 2048, about 11 materials' worth of copies.
        struct SharedSamplerTable
        {
            uint32_t baseSlot = Dx12DescriptorImpl::InvalidSlot;
            uint32_t count = 0;
            uint32_t refs = 0;
        };

        struct SamplerTables
        {
            std::mutex mutex;
            std::map<std::vector<float>, SharedSamplerTable> tables;
        };

        // Never destroyed, so descriptors released during static destruction still find it.
        SamplerTables &SharedSamplerTables()
        {
            static auto *tables = new SamplerTables();
            return *tables;
        }

        // By description, not by pointer: a destroyed sampler's address can come back holding other state.
        std::vector<float> SamplerTableKey(const std::vector<Sampler *> &samplers)
        {
            std::vector<float> key;
            key.reserve(samplers.size() * 16);
            for (const Sampler *sampler : samplers)
            {
                if (!sampler)
                {
                    key.push_back(0.0f);
                    continue;
                }

                const SamplerDesc &desc = sampler->GetDesc();
                key.insert(key.end(),
                           {1.0f,
                            static_cast<float>(desc.magFilter),
                            static_cast<float>(desc.minFilter),
                            static_cast<float>(desc.mipmapMode),
                            static_cast<float>(desc.addressModeU),
                            static_cast<float>(desc.addressModeV),
                            static_cast<float>(desc.addressModeW),
                            desc.mipLodBias,
                            desc.anisotropyEnable ? 1.0f : 0.0f,
                            desc.maxAnisotropy,
                            desc.compareEnable ? 1.0f : 0.0f,
                            static_cast<float>(desc.compareOp),
                            desc.minLod,
                            desc.maxLod,
                            static_cast<float>(desc.borderColor),
                            desc.unnormalizedCoordinates ? 1.0f : 0.0f});
            }
            return key;
        }

        void ReleaseSamplerTableLocked(SamplerTables &shared, const std::vector<float> *&key)
        {
            if (!key)
                return;

            auto it = shared.tables.find(*key);
            key = nullptr;
            if (it == shared.tables.end() || --it->second.refs > 0)
                return;

            auto *rhi = static_cast<Dx12RhiImpl *>(RHII.GetImpl());
            if (rhi && rhi->GetSamplerHeap())
                rhi->GetSamplerHeap()->FreeRange(it->second.baseSlot, it->second.count);
            shared.tables.erase(it);
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

        const auto &bindingInfos = m_owner->GetBindingInfos();
        for (size_t i = 0; i < bindingInfos.size(); ++i)
        {
            const DescriptorBindingInfo &info = bindingInfos[i];
            const bool textureSpace = info.dxSpace == PE_TEXTURE_TABLE_DX_SPACE;
            PE_ERROR_IF(info.dxSpace >= DX12_DESCRIPTOR_SPACE_COUNT && !textureSpace,
                        "DX12 descriptor space %u exceeds supported count %u",
                        info.dxSpace,
                        DX12_DESCRIPTOR_SPACE_COUNT);

            const uint32_t capacity = m_owner->GetBindingCapacity(i);
            ValidateDescriptorCapacity(info, capacity);

            Required &required = requiredBySpace[info.dxSpace];
            required.cbvSrvUavCount = std::max(required.cbvSrvUavCount, CbvSrvUavTableEnd(info, capacity));
            required.samplerCount = std::max(required.samplerCount, SamplerTableEnd(info, capacity));
        }

        for (const auto &[space, required] : requiredBySpace)
        {
            TableSlots &table = m_tables.emplace_back();
            table.dxSpace = space;
            if (required.cbvSrvUavCount > 0)
            {
                table.cbvSrvUavBaseSlot = rhi->GetCbvSrvUavHeap()->TryAllocateRange(required.cbvSrvUavCount);
                table.cbvSrvUavCount = required.cbvSrvUavCount;
            }

            // The sampler range comes from the shared tables once Update knows what the samplers are.
            table.samplerCount = required.samplerCount;
            table.samplers.resize(required.samplerCount);

            // Growing a bindless array may not fit; the caller keeps the previous descriptor instead of failing.
            const bool exhausted = required.cbvSrvUavCount > 0 && table.cbvSrvUavBaseSlot == InvalidSlot;
            if (exhausted)
            {
                FreeTables();
                throw std::runtime_error("DX12 shader-visible descriptor heap has no free range for space " +
                                         std::to_string(space));
            }
        }

        for (size_t i = 0; i < bindingInfos.size(); ++i)
        {
            const DescriptorBindingInfo &info = bindingInfos[i];
            const uint32_t capacity = std::max(1u, m_owner->GetBindingCapacity(i));
            TableSlots *table = FindTable(info.dxSpace);
            PE_ERROR_IF(!table, "DX12 descriptor table was not allocated");

            BindingSlots slots{};
            slots.binding = info.binding;
            slots.dxRegister = info.dxRegister;
            slots.dxSpace = info.dxSpace;
            if (IsCbvSrvUavDescriptor(info.type))
            {
                const uint32_t offset = CbvSrvUavTableOffset(info);
                slots.cbvSrvUavSlots.resize(capacity);
                for (uint32_t element = 0; element < capacity; ++element)
                    slots.cbvSrvUavSlots[element] = table->cbvSrvUavBaseSlot + offset + element;
            }
            if (IsSamplerDescriptor(info.type))
            {
                slots.samplerSlots.resize(capacity);
                for (uint32_t element = 0; element < capacity; ++element)
                    slots.samplerSlots[element] = info.dxRegister + element;
            }
            m_slots.push_back(std::move(slots));
        }
    }

    void Dx12DescriptorImpl::FreeTables()
    {
        auto *rhi = static_cast<Dx12RhiImpl *>(RHII.GetImpl());
        if (!rhi)
            return;

        SamplerTables &shared = SharedSamplerTables();
        std::lock_guard<std::mutex> lock(shared.mutex);
        for (TableSlots &table : m_tables)
        {
            if (table.cbvSrvUavBaseSlot != InvalidSlot && table.cbvSrvUavCount > 0 && rhi->GetCbvSrvUavHeap())
                rhi->GetCbvSrvUavHeap()->FreeRange(table.cbvSrvUavBaseSlot, table.cbvSrvUavCount);

            ReleaseSamplerTableLocked(shared, table.samplerKey);
        }
        m_tables.clear();
        m_slots.clear();
    }

    // Records the sampler; the table is re-shared after the update when its contents changed.
    bool Dx12DescriptorImpl::SetSampler(const BindingSlots &slots, uint32_t element, Sampler *sampler)
    {
        PE_ERROR_IF(element >= slots.samplerSlots.size(), "Dx12DescriptorImpl: sampler descriptor array exceeds reflected count");
        TableSlots *table = FindTable(slots.dxSpace);
        PE_ERROR_IF(!table, "DX12 descriptor table was not allocated");

        Sampler *&held = table->samplers[slots.samplerSlots[element]];
        Sampler *wanted = Dx12SamplerImpl::TryFrom(sampler) ? sampler : nullptr;
        if (held == wanted)
            return false;

        held = wanted;
        return true;
    }

    void Dx12DescriptorImpl::ShareSamplerTable(TableSlots &table)
    {
        std::vector<float> key = SamplerTableKey(table.samplers);
        SamplerTables &shared = SharedSamplerTables();
        std::lock_guard<std::mutex> lock(shared.mutex);
        if (table.samplerKey && *table.samplerKey == key)
            return;

        ReleaseSamplerTableLocked(shared, table.samplerKey);
        table.samplerBaseSlot = InvalidSlot;

        auto [it, inserted] = shared.tables.try_emplace(std::move(key));
        SharedSamplerTable &entry = it->second;
        if (inserted)
        {
            auto *rhi = static_cast<Dx12RhiImpl *>(RHII.GetImpl());
            entry.count = table.samplerCount;
            entry.baseSlot = rhi->GetSamplerHeap()->TryAllocateRange(entry.count);
            if (entry.baseSlot == InvalidSlot)
            {
                shared.tables.erase(it);
                PE_ERROR("DX12 shader-visible sampler heap has no free range of %u", table.samplerCount);
            }

            // Shared tables never change after this: a new content gets a new table.
            for (uint32_t i = 0; i < entry.count; ++i)
            {
                if (!table.samplers[i])
                    continue;

                rhi->GetDevice()->CopyDescriptorsSimple(1,
                                                        rhi->GetSamplerHeap()->GetCpuHandle(entry.baseSlot + i),
                                                        Dx12SamplerImpl::From(table.samplers[i])->GetCpuHandle(),
                                                        D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
            }
        }

        ++entry.refs;
        table.samplerKey = &it->first;
        table.samplerBaseSlot = entry.baseSlot;
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
        const TableSlots *table = slots ? FindTable(slots->dxSpace) : nullptr;
        if (!table || table->samplerBaseSlot == InvalidSlot || arrayIndex >= slots->samplerSlots.size())
            return InvalidSlot;
        return table->samplerBaseSlot + slots->samplerSlots[arrayIndex];
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
            else if (!updateInfo.accelerationStructures.empty())
            {
                for (uint32_t j = 0; j < updateInfo.accelerationStructures.size(); ++j)
                {
                    const uint32_t sourceSlot = GetCbvSrvUavSlot(updateInfo.binding, j);
                    if (sourceSlot == InvalidSlot || sourceSlot < sourceBase || sourceSlot >= sourceBase + tableCount)
                    {
                        freeOnFailure();
                        return false;
                    }
                    rhi->GetDevice()->CopyDescriptorsSimple(
                        1,
                        heap->GetCpuHandle(dynamicBase + (sourceSlot - sourceBase)),
                        heap->GetCpuHandle(sourceSlot),
                        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
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
        bool samplersChanged = false;

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
                        samplersChanged |= SetSampler(*slots, j, updateInfo.samplers[j]);
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
                    samplersChanged |= SetSampler(*slots, j, updateInfo.samplers[j]);
            }
            else if (!updateInfo.accelerationStructures.empty())
            {
                for (uint32_t j = 0; j < updateInfo.accelerationStructures.size(); j++)
                {
                    PE_ERROR_IF(j >= slots->cbvSrvUavSlots.size(),
                                "Dx12DescriptorImpl: acceleration-structure descriptor array exceeds reflected count");
                    AccelerationStructure *as = updateInfo.accelerationStructures[j];
                    PE_ERROR_IF(!as, "Dx12DescriptorImpl: null acceleration structure for binding %u", updateInfo.binding);
                    PE_ERROR_IF(as->GetDeviceAddress() == 0,
                                "Dx12DescriptorImpl: acceleration structure for binding %u has no GPU address",
                                updateInfo.binding);

                    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
                    srv.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
                    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                    srv.RaytracingAccelerationStructure.Location = as->GetDeviceAddress();
                    device->CreateShaderResourceView(nullptr, &srv, cbvSrvUavHeap->GetCpuHandle(slots->cbvSrvUavSlots[j]));
                }
            }
        }

        if (!samplersChanged)
            return;

        for (TableSlots &table : m_tables)
        {
            if (table.samplerCount > 0)
                ShareSamplerTable(table);
        }
    }
} // namespace pe
