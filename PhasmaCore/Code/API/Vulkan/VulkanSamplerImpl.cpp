#include "API/Vulkan/VulkanSamplerImpl.h"
#if defined(PE_WIN32)
#include "API/DX12/Dx12SamplerImpl.h"
#endif
#include "API/RHI.h"
#include "API/Vulkan/RHI_Vulkan.h"
#include "API/Vulkan/VulkanRHITypeUtils.h"

namespace pe
{
    vk::Filter ToVkFilter(PeFilter filter)
    {
        switch (filter)
        {
        case PE_FILTER_NEAREST:
            return vk::Filter::eNearest;
        case PE_FILTER_LINEAR:
            return vk::Filter::eLinear;
        default:
            return vk::Filter::eLinear;
        }
    }

    PeFilter FromVkFilter(vk::Filter filter)
    {
        switch (filter)
        {
        case vk::Filter::eNearest:
            return PE_FILTER_NEAREST;
        case vk::Filter::eLinear:
            return PE_FILTER_LINEAR;
        default:
            return PE_FILTER_LINEAR;
        }
    }

    vk::SamplerAddressMode ToVkSamplerAddressMode(PeSamplerAddressMode mode)
    {
        switch (mode)
        {
        case PE_SAMPLER_ADDRESS_MODE_REPEAT:
            return vk::SamplerAddressMode::eRepeat;
        case PE_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT:
            return vk::SamplerAddressMode::eMirroredRepeat;
        case PE_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE:
            return vk::SamplerAddressMode::eClampToEdge;
        case PE_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER:
            return vk::SamplerAddressMode::eClampToBorder;
        default:
            return vk::SamplerAddressMode::eRepeat;
        }
    }

    PeSamplerAddressMode FromVkSamplerAddressMode(vk::SamplerAddressMode mode)
    {
        switch (mode)
        {
        case vk::SamplerAddressMode::eRepeat:
            return PE_SAMPLER_ADDRESS_MODE_REPEAT;
        case vk::SamplerAddressMode::eMirroredRepeat:
            return PE_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        case vk::SamplerAddressMode::eClampToEdge:
            return PE_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        case vk::SamplerAddressMode::eClampToBorder:
            return PE_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        default:
            return PE_SAMPLER_ADDRESS_MODE_REPEAT;
        }
    }

    vk::SamplerMipmapMode ToVkSamplerMipmapMode(PeSamplerMipmapMode mode)
    {
        switch (mode)
        {
        case PE_SAMPLER_MIPMAP_MODE_NEAREST:
            return vk::SamplerMipmapMode::eNearest;
        case PE_SAMPLER_MIPMAP_MODE_LINEAR:
            return vk::SamplerMipmapMode::eLinear;
        default:
            return vk::SamplerMipmapMode::eLinear;
        }
    }

    PeSamplerMipmapMode FromVkSamplerMipmapMode(vk::SamplerMipmapMode mode)
    {
        switch (mode)
        {
        case vk::SamplerMipmapMode::eNearest:
            return PE_SAMPLER_MIPMAP_MODE_NEAREST;
        case vk::SamplerMipmapMode::eLinear:
            return PE_SAMPLER_MIPMAP_MODE_LINEAR;
        default:
            return PE_SAMPLER_MIPMAP_MODE_LINEAR;
        }
    }

    vk::BorderColor ToVkSamplerBorderColor(PeSamplerBorderColor color)
    {
        switch (color)
        {
        case PE_SAMPLER_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK:
            return vk::BorderColor::eFloatTransparentBlack;
        case PE_SAMPLER_BORDER_COLOR_FLOAT_OPAQUE_BLACK:
            return vk::BorderColor::eFloatOpaqueBlack;
        case PE_SAMPLER_BORDER_COLOR_FLOAT_OPAQUE_WHITE:
            return vk::BorderColor::eFloatOpaqueWhite;
        default:
            return vk::BorderColor::eFloatOpaqueBlack;
        }
    }

    PeSamplerBorderColor FromVkSamplerBorderColor(vk::BorderColor color)
    {
        switch (color)
        {
        case vk::BorderColor::eFloatTransparentBlack:
            return PE_SAMPLER_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
        case vk::BorderColor::eFloatOpaqueBlack:
            return PE_SAMPLER_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
        case vk::BorderColor::eFloatOpaqueWhite:
            return PE_SAMPLER_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        default:
            return PE_SAMPLER_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
        }
    }

    VulkanSamplerImpl::VulkanSamplerImpl(Sampler *owner, const SamplerDesc &desc)
        : m_owner{owner}
    {
        vk::SamplerCreateInfo info{};
        info.magFilter = ToVkFilter(desc.magFilter);
        info.minFilter = ToVkFilter(desc.minFilter);
        info.mipmapMode = ToVkSamplerMipmapMode(desc.mipmapMode);
        info.addressModeU = ToVkSamplerAddressMode(desc.addressModeU);
        info.addressModeV = ToVkSamplerAddressMode(desc.addressModeV);
        info.addressModeW = ToVkSamplerAddressMode(desc.addressModeW);
        info.mipLodBias = desc.mipLodBias;
        info.anisotropyEnable = desc.anisotropyEnable ? VK_TRUE : VK_FALSE;
        info.maxAnisotropy = desc.maxAnisotropy;
        info.compareEnable = desc.compareEnable ? VK_TRUE : VK_FALSE;
        info.compareOp = ToVkCompareOp(desc.compareOp);
        info.minLod = desc.minLod;
        info.maxLod = desc.maxLod;
        info.borderColor = ToVkSamplerBorderColor(desc.borderColor);
        info.unnormalizedCoordinates = desc.unnormalizedCoordinates ? VK_TRUE : VK_FALSE;

        m_sampler = VulkanRhi::Device().createSampler(info);
        Debug::SetObjectName(m_sampler, owner->m_name);
    }

    VulkanSamplerImpl::~VulkanSamplerImpl()
    {
        if (m_sampler)
            VulkanRhi::Device().destroySampler(m_sampler);
    }

    Sampler::Impl *CreateSamplerImpl(Sampler *owner, const SamplerDesc &desc)
    {
        switch (RHII.GetApi())
        {
        case PE_GRAPHICS_API_VULKAN:
            return new VulkanSamplerImpl(owner, desc);
#if defined(PE_WIN32)
        case PE_GRAPHICS_API_DX12:
            return new Dx12SamplerImpl(owner, desc);
#endif
        default:
            PE_ERROR("CreateSamplerImpl: unsupported graphics api %u", static_cast<uint32_t>(RHII.GetApi()));
            return nullptr;
        }
    }
} // namespace pe
