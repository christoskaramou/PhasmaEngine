#include "API/Vulkan/VulkanSamplerImpl.h"
#include "API/Debug.h"
#include "API/RHI.h"

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

    vk::CompareOp ToVkCompareOp(PeCompareOp op)
    {
        switch (op)
        {
        case PE_COMPARE_OP_NEVER:
            return vk::CompareOp::eNever;
        case PE_COMPARE_OP_LESS:
            return vk::CompareOp::eLess;
        case PE_COMPARE_OP_EQUAL:
            return vk::CompareOp::eEqual;
        case PE_COMPARE_OP_LESS_OR_EQUAL:
            return vk::CompareOp::eLessOrEqual;
        case PE_COMPARE_OP_GREATER:
            return vk::CompareOp::eGreater;
        case PE_COMPARE_OP_NOT_EQUAL:
            return vk::CompareOp::eNotEqual;
        case PE_COMPARE_OP_GREATER_OR_EQUAL:
            return vk::CompareOp::eGreaterOrEqual;
        case PE_COMPARE_OP_ALWAYS:
            return vk::CompareOp::eAlways;
        default:
            return vk::CompareOp::eLess;
        }
    }

    PeCompareOp FromVkCompareOp(vk::CompareOp op)
    {
        switch (op)
        {
        case vk::CompareOp::eNever:
            return PE_COMPARE_OP_NEVER;
        case vk::CompareOp::eLess:
            return PE_COMPARE_OP_LESS;
        case vk::CompareOp::eEqual:
            return PE_COMPARE_OP_EQUAL;
        case vk::CompareOp::eLessOrEqual:
            return PE_COMPARE_OP_LESS_OR_EQUAL;
        case vk::CompareOp::eGreater:
            return PE_COMPARE_OP_GREATER;
        case vk::CompareOp::eNotEqual:
            return PE_COMPARE_OP_NOT_EQUAL;
        case vk::CompareOp::eGreaterOrEqual:
            return PE_COMPARE_OP_GREATER_OR_EQUAL;
        case vk::CompareOp::eAlways:
            return PE_COMPARE_OP_ALWAYS;
        default:
            return PE_COMPARE_OP_LESS;
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

        m_sampler = RHII.GetDevice().createSampler(info);
        Debug::SetObjectName(m_sampler, owner->m_name);
    }

    VulkanSamplerImpl::~VulkanSamplerImpl()
    {
        if (m_sampler)
            RHII.GetDevice().destroySampler(m_sampler);
    }

    Sampler::Impl *CreateSamplerImpl(Sampler *owner, const SamplerDesc &desc)
    {
        return new VulkanSamplerImpl(owner, desc);
    }
} // namespace pe
