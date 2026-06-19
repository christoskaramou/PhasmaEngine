#pragma once

#include "API/Vulkan/VulkanHeaders.h"

#include "API/Sampler_Internal.h"

namespace pe
{
    struct VulkanSamplerImpl final : public Sampler::Impl
    {
        VulkanSamplerImpl(Sampler *owner, const SamplerDesc &desc);
        ~VulkanSamplerImpl() override;

        static VulkanSamplerImpl *From(Sampler *sampler) { return static_cast<VulkanSamplerImpl *>(sampler->m_impl); }
        static const VulkanSamplerImpl *From(const Sampler *sampler) { return static_cast<const VulkanSamplerImpl *>(sampler->m_impl); }
        static VulkanSamplerImpl *TryFrom(Sampler *sampler) { return sampler && sampler->m_impl ? From(sampler) : nullptr; }
        static const VulkanSamplerImpl *TryFrom(const Sampler *sampler) { return sampler && sampler->m_impl ? From(sampler) : nullptr; }

        Sampler *m_owner{};
        vk::Sampler m_sampler{};
    };

    inline vk::Sampler GetVulkanSampler(Sampler *sampler)
    {
        VulkanSamplerImpl *impl = VulkanSamplerImpl::TryFrom(sampler);
        return impl ? impl->m_sampler : vk::Sampler{};
    }

    inline vk::Sampler GetVulkanSampler(const Sampler *sampler)
    {
        const VulkanSamplerImpl *impl = VulkanSamplerImpl::TryFrom(sampler);
        return impl ? impl->m_sampler : vk::Sampler{};
    }

    vk::Filter ToVkFilter(PeFilter filter);
    PeFilter FromVkFilter(vk::Filter filter);

    vk::SamplerAddressMode ToVkSamplerAddressMode(PeSamplerAddressMode mode);
    PeSamplerAddressMode FromVkSamplerAddressMode(vk::SamplerAddressMode mode);

    vk::SamplerMipmapMode ToVkSamplerMipmapMode(PeSamplerMipmapMode mode);
    PeSamplerMipmapMode FromVkSamplerMipmapMode(vk::SamplerMipmapMode mode);

    vk::BorderColor ToVkSamplerBorderColor(PeSamplerBorderColor color);
    PeSamplerBorderColor FromVkSamplerBorderColor(vk::BorderColor color);
} // namespace pe
