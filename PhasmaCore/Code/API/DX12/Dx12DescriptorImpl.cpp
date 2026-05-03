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

        D3D12_BUFFER_UAV BufferUav(const DescriptorUpdateInfo &updateInfo, uint32_t index, uint64_t rangeBytes)
        {
            const uint64_t offset = index < updateInfo.offsets.size() ? updateInfo.offsets[index] : 0;
            D3D12_BUFFER_UAV uav{};
            uav.FirstElement = offset / sizeof(uint32_t);
            uav.NumElements = static_cast<UINT>(rangeBytes / sizeof(uint32_t));
            uav.StructureByteStride = 0;
            uav.CounterOffsetInBytes = 0;
            uav.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
            return uav;
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
    }

    Dx12DescriptorImpl::~Dx12DescriptorImpl()
    {
        FreeCbvSrvUavSlots();
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

    Dx12DescriptorImpl::BindingSlots &Dx12DescriptorImpl::EnsureSlots(uint32_t binding)
    {
        if (BindingSlots *slots = FindSlots(binding))
            return *slots;

        BindingSlots slots{};
        slots.binding = binding;
        m_slots.push_back(std::move(slots));
        return m_slots.back();
    }

    void Dx12DescriptorImpl::EnsureCbvSrvUavSlots(BindingSlots &slots, uint32_t count)
    {
        auto *rhi = static_cast<Dx12RhiImpl *>(RHII.GetImpl());
        PE_ERROR_IF(!rhi || !rhi->GetCbvSrvUavHeap(), "Dx12DescriptorImpl requires the DX12 CBV/SRV/UAV heap");

        while (slots.cbvSrvUavSlots.size() < count)
            slots.cbvSrvUavSlots.push_back(rhi->GetCbvSrvUavHeap()->Allocate());
    }

    void Dx12DescriptorImpl::EnsureSamplerSlotCount(BindingSlots &slots, uint32_t count)
    {
        slots.samplerSlots.resize(count, InvalidSlot);
    }

    void Dx12DescriptorImpl::FreeCbvSrvUavSlots()
    {
        auto *rhi = static_cast<Dx12RhiImpl *>(RHII.GetImpl());
        Dx12DescriptorHeap *heap = rhi ? rhi->GetCbvSrvUavHeap() : nullptr;
        if (!heap)
            return;

        for (BindingSlots &slots : m_slots)
        {
            for (uint32_t slot : slots.cbvSrvUavSlots)
            {
                if (slot != InvalidSlot)
                    heap->Free(slot);
            }
            slots.cbvSrvUavSlots.clear();
        }
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

            BindingSlots &slots = EnsureSlots(updateInfo.binding);

            if (!updateInfo.views.empty())
            {
                EnsureCbvSrvUavSlots(slots, static_cast<uint32_t>(updateInfo.views.size()));
                for (uint32_t j = 0; j < updateInfo.views.size(); j++)
                {
                    const auto *view = Dx12ImageViewImpl::From(updateInfo.views[j]);
                    const uint32_t slot = slots.cbvSrvUavSlots[j];
                    device->CopyDescriptorsSimple(1,
                                                  cbvSrvUavHeap->GetCpuHandle(slot),
                                                  view->GetCpuHandle(),
                                                  D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                }

                if (bindingInfo.type == PE_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                {
                    EnsureSamplerSlotCount(slots, static_cast<uint32_t>(updateInfo.samplers.size()));
                    for (uint32_t j = 0; j < updateInfo.samplers.size(); j++)
                    {
                        const Dx12SamplerImpl *sampler = Dx12SamplerImpl::TryFrom(updateInfo.samplers[j]);
                        slots.samplerSlots[j] = sampler ? sampler->GetShaderVisibleSlot() : InvalidSlot;
                    }
                }
            }
            else if (!updateInfo.buffers.empty())
            {
                EnsureCbvSrvUavSlots(slots, static_cast<uint32_t>(updateInfo.buffers.size()));
                for (uint32_t j = 0; j < updateInfo.buffers.size(); j++)
                {
                    Buffer *buffer = updateInfo.buffers[j];
                    const Dx12BufferImpl *bufferImpl = Dx12BufferImpl::From(buffer);
                    const uint64_t offset = j < updateInfo.offsets.size() ? updateInfo.offsets[j] : 0;
                    const uint64_t rangeBytes = BufferRangeBytes(buffer, updateInfo, j);
                    const D3D12_CPU_DESCRIPTOR_HANDLE dst = cbvSrvUavHeap->GetCpuHandle(slots.cbvSrvUavSlots[j]);

                    if (bindingInfo.type == PE_DESCRIPTOR_TYPE_UNIFORM_BUFFER ||
                        bindingInfo.type == PE_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC)
                    {
                        D3D12_CONSTANT_BUFFER_VIEW_DESC cbv{};
                        cbv.BufferLocation = bufferImpl->GetResource()->GetGPUVirtualAddress() + offset;
                        cbv.SizeInBytes = AlignCbvSize(rangeBytes);
                        device->CreateConstantBufferView(&cbv, dst);
                    }
                    else
                    {
                        D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
                        uav.Format = DXGI_FORMAT_R32_TYPELESS;
                        uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
                        uav.Buffer = BufferUav(updateInfo, j, rangeBytes);
                        device->CreateUnorderedAccessView(bufferImpl->GetResource(), nullptr, &uav, dst);
                    }
                }
            }
            else if (!updateInfo.samplers.empty() && bindingInfo.type != PE_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
            {
                EnsureSamplerSlotCount(slots, static_cast<uint32_t>(updateInfo.samplers.size()));
                for (uint32_t j = 0; j < updateInfo.samplers.size(); j++)
                {
                    const Dx12SamplerImpl *sampler = Dx12SamplerImpl::TryFrom(updateInfo.samplers[j]);
                    slots.samplerSlots[j] = sampler ? sampler->GetShaderVisibleSlot() : InvalidSlot;
                }
            }
            else if (!updateInfo.accelerationStructures.empty())
            {
                PE_ERROR("Dx12DescriptorImpl: acceleration-structure descriptors wait for the DXR slice");
            }
        }
    }
} // namespace pe
