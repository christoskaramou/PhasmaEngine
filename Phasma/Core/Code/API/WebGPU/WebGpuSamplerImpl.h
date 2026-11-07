#pragma once

#include "API/Sampler_Internal.h"

#include <webgpu/webgpu.h>

namespace pe
{
    struct WebGpuSamplerImpl final : public Sampler::Impl
    {
        WebGpuSamplerImpl(Sampler *owner, const SamplerDesc &desc);
        ~WebGpuSamplerImpl() override;

        static WebGpuSamplerImpl *From(Sampler *sampler) { return static_cast<WebGpuSamplerImpl *>(sampler->m_impl); }

        WGPUSampler m_sampler = nullptr;
        bool m_comparison = false;
    };
} // namespace pe
