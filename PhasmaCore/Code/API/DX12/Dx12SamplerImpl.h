#pragma once

#include "API/Sampler_Internal.h"

#include <wrl/client.h>

namespace pe
{
    struct Dx12SamplerImpl final : public Sampler::Impl
    {
        Dx12SamplerImpl(Sampler *owner, const SamplerDesc &desc);
        ~Dx12SamplerImpl() override = default;

        static Dx12SamplerImpl *From(Sampler *sampler) { return static_cast<Dx12SamplerImpl *>(sampler->m_impl); }
        static const Dx12SamplerImpl *From(const Sampler *sampler) { return static_cast<const Dx12SamplerImpl *>(sampler->m_impl); }
        static Dx12SamplerImpl *TryFrom(Sampler *sampler) { return sampler && sampler->m_impl ? From(sampler) : nullptr; }
        static const Dx12SamplerImpl *TryFrom(const Sampler *sampler) { return sampler && sampler->m_impl ? From(sampler) : nullptr; }

        D3D12_CPU_DESCRIPTOR_HANDLE GetCpuHandle() const { return m_cpuHandle; }

        Sampler *m_owner = nullptr;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_heap;
        D3D12_CPU_DESCRIPTOR_HANDLE m_cpuHandle{};
    };
} // namespace pe
