#pragma once

#include "API/Descriptor_Internal.h"

#include <d3d12.h>

namespace pe
{
    struct Dx12DescriptorPoolImpl final : public DescriptorPool::Impl
    {
        Dx12DescriptorPoolImpl(DescriptorPool *owner, const DescriptorPoolDesc &desc);
        ~Dx12DescriptorPoolImpl() override = default;

        static Dx12DescriptorPoolImpl *From(DescriptorPool *pool) { return static_cast<Dx12DescriptorPoolImpl *>(pool->m_impl); }
        static const Dx12DescriptorPoolImpl *From(const DescriptorPool *pool) { return static_cast<const Dx12DescriptorPoolImpl *>(pool->m_impl); }
        static Dx12DescriptorPoolImpl *TryFrom(DescriptorPool *pool) { return pool && pool->m_impl ? From(pool) : nullptr; }
        static const Dx12DescriptorPoolImpl *TryFrom(const DescriptorPool *pool) { return pool && pool->m_impl ? From(pool) : nullptr; }

        DescriptorPool *m_owner{};
        DescriptorPoolDesc m_desc{};
    };

    struct Dx12DescriptorLayoutImpl final : public DescriptorLayout::Impl
    {
        explicit Dx12DescriptorLayoutImpl(DescriptorLayout *owner);
        ~Dx12DescriptorLayoutImpl() override = default;

        static Dx12DescriptorLayoutImpl *From(DescriptorLayout *layout) { return static_cast<Dx12DescriptorLayoutImpl *>(layout->m_impl); }
        static const Dx12DescriptorLayoutImpl *From(const DescriptorLayout *layout) { return static_cast<const Dx12DescriptorLayoutImpl *>(layout->m_impl); }
        static Dx12DescriptorLayoutImpl *TryFrom(DescriptorLayout *layout) { return layout && layout->m_impl ? From(layout) : nullptr; }
        static const Dx12DescriptorLayoutImpl *TryFrom(const DescriptorLayout *layout) { return layout && layout->m_impl ? From(layout) : nullptr; }

        DescriptorLayout *m_owner{};
    };

    struct Dx12DescriptorImpl final : public Descriptor::Impl
    {
        explicit Dx12DescriptorImpl(Descriptor *owner);
        ~Dx12DescriptorImpl() override;

        void Update(const std::vector<DescriptorBindingInfo> &bindingInfos,
                    const std::vector<DescriptorUpdateInfo> &updateInfos) override;

        static Dx12DescriptorImpl *From(Descriptor *descriptor) { return static_cast<Dx12DescriptorImpl *>(descriptor->m_impl); }
        static const Dx12DescriptorImpl *From(const Descriptor *descriptor) { return static_cast<const Dx12DescriptorImpl *>(descriptor->m_impl); }
        static Dx12DescriptorImpl *TryFrom(Descriptor *descriptor) { return descriptor && descriptor->m_impl ? From(descriptor) : nullptr; }
        static const Dx12DescriptorImpl *TryFrom(const Descriptor *descriptor) { return descriptor && descriptor->m_impl ? From(descriptor) : nullptr; }

        static constexpr uint32_t InvalidSlot = UINT32_MAX;

        struct DynamicBufferBinding
        {
            uint32_t binding = InvalidSlot;
            Buffer *buffer = nullptr;
            uint64_t offset = 0;
            uint64_t range = 0;
        };

        uint32_t GetCbvSrvUavSlot(uint32_t binding, uint32_t arrayIndex = 0) const;
        uint32_t GetSamplerSlot(uint32_t binding, uint32_t arrayIndex = 0) const;
        bool GetCbvSrvUavTableInfo(uint32_t dxSpace, uint32_t &baseSlot, uint32_t &count) const;
        bool CreateDynamicCbvSrvUavTable(uint32_t dxSpace,
                                         const DynamicBufferBinding *dynamicBindings,
                                         uint32_t dynamicBindingCount,
                                         uint32_t &firstSlot,
                                         uint32_t &count,
                                         D3D12_GPU_DESCRIPTOR_HANDLE &gpuHandle) const;
        D3D12_GPU_DESCRIPTOR_HANDLE GetCbvSrvUavGpuHandle(uint32_t binding, uint32_t arrayIndex = 0) const;
        D3D12_GPU_DESCRIPTOR_HANDLE GetSamplerGpuHandle(uint32_t binding, uint32_t arrayIndex = 0) const;
        D3D12_GPU_DESCRIPTOR_HANDLE GetCbvSrvUavTableGpuHandle(uint32_t dxSpace) const;
        D3D12_GPU_DESCRIPTOR_HANDLE GetSamplerTableGpuHandle(uint32_t dxSpace) const;

        Descriptor *m_owner{};

    private:
        struct BindingSlots
        {
            uint32_t binding = InvalidSlot;
            uint32_t dxRegister = InvalidSlot;
            uint32_t dxSpace = 0;
            std::vector<uint32_t> cbvSrvUavSlots{};
            std::vector<uint32_t> samplerSlots{};
        };

        struct TableSlots
        {
            uint32_t dxSpace = InvalidSlot;
            uint32_t cbvSrvUavBaseSlot = InvalidSlot;
            uint32_t cbvSrvUavCount = 0;
            uint32_t samplerBaseSlot = InvalidSlot;
            uint32_t samplerCount = 0;
        };

        BindingSlots *FindSlots(uint32_t binding);
        const BindingSlots *FindSlots(uint32_t binding) const;
        TableSlots *FindTable(uint32_t dxSpace);
        const TableSlots *FindTable(uint32_t dxSpace) const;
        void AllocateTables();
        void FreeTables();

        std::vector<BindingSlots> m_slots{};
        std::vector<TableSlots> m_tables{};
    };

    inline uint32_t GetDx12CbvSrvUavDescriptorSlot(const Descriptor *descriptor, uint32_t binding, uint32_t arrayIndex = 0)
    {
        const Dx12DescriptorImpl *impl = Dx12DescriptorImpl::TryFrom(descriptor);
        return impl ? impl->GetCbvSrvUavSlot(binding, arrayIndex) : Dx12DescriptorImpl::InvalidSlot;
    }

    inline uint32_t GetDx12SamplerDescriptorSlot(const Descriptor *descriptor, uint32_t binding, uint32_t arrayIndex = 0)
    {
        const Dx12DescriptorImpl *impl = Dx12DescriptorImpl::TryFrom(descriptor);
        return impl ? impl->GetSamplerSlot(binding, arrayIndex) : Dx12DescriptorImpl::InvalidSlot;
    }

    inline D3D12_GPU_DESCRIPTOR_HANDLE GetDx12CbvSrvUavTableGpuHandle(const Descriptor *descriptor, uint32_t dxSpace)
    {
        const Dx12DescriptorImpl *impl = Dx12DescriptorImpl::TryFrom(descriptor);
        return impl ? impl->GetCbvSrvUavTableGpuHandle(dxSpace) : D3D12_GPU_DESCRIPTOR_HANDLE{};
    }

    inline D3D12_GPU_DESCRIPTOR_HANDLE GetDx12SamplerTableGpuHandle(const Descriptor *descriptor, uint32_t dxSpace)
    {
        const Dx12DescriptorImpl *impl = Dx12DescriptorImpl::TryFrom(descriptor);
        return impl ? impl->GetSamplerTableGpuHandle(dxSpace) : D3D12_GPU_DESCRIPTOR_HANDLE{};
    }
} // namespace pe
