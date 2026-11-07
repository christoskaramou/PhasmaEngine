#include "API/WebGPU/WebGpuSamplerImpl.h"
#include "API/WebGPU/WebGpuRhiImpl.h"

namespace pe
{
    namespace
    {
        WGPUFilterMode ToWgpuFilter(PeFilter filter)
        {
            return filter == PE_FILTER_NEAREST ? WGPUFilterMode_Nearest : WGPUFilterMode_Linear;
        }

        // WebGPU has no clamp-to-border; edge clamping is the closest match.
        WGPUAddressMode ToWgpuAddress(PeSamplerAddressMode mode)
        {
            switch (mode)
            {
            case PE_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT:
                return WGPUAddressMode_MirrorRepeat;
            case PE_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE:
            case PE_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER:
                return WGPUAddressMode_ClampToEdge;
            default:
                return WGPUAddressMode_Repeat;
            }
        }

        WGPUCompareFunction ToWgpuCompare(PeCompareOp op)
        {
            switch (op)
            {
            case PE_COMPARE_OP_NEVER:
                return WGPUCompareFunction_Never;
            case PE_COMPARE_OP_LESS:
                return WGPUCompareFunction_Less;
            case PE_COMPARE_OP_EQUAL:
                return WGPUCompareFunction_Equal;
            case PE_COMPARE_OP_LESS_OR_EQUAL:
                return WGPUCompareFunction_LessEqual;
            case PE_COMPARE_OP_GREATER:
                return WGPUCompareFunction_Greater;
            case PE_COMPARE_OP_NOT_EQUAL:
                return WGPUCompareFunction_NotEqual;
            case PE_COMPARE_OP_GREATER_OR_EQUAL:
                return WGPUCompareFunction_GreaterEqual;
            default:
                return WGPUCompareFunction_Always;
            }
        }
    } // namespace

    // mipLodBias, border colours and unnormalized coordinates have no WebGPU equivalent and are ignored.
    WebGpuSamplerImpl::WebGpuSamplerImpl(Sampler *owner, const SamplerDesc &desc)
        : m_comparison{desc.compareEnable}
    {
        WGPUSamplerDescriptor samplerDesc{};
        samplerDesc.label = {owner->GetName().c_str(), owner->GetName().size()};
        samplerDesc.addressModeU = ToWgpuAddress(desc.addressModeU);
        samplerDesc.addressModeV = ToWgpuAddress(desc.addressModeV);
        samplerDesc.addressModeW = ToWgpuAddress(desc.addressModeW);
        samplerDesc.magFilter = ToWgpuFilter(desc.magFilter);
        samplerDesc.minFilter = ToWgpuFilter(desc.minFilter);
        samplerDesc.mipmapFilter = desc.mipmapMode == PE_SAMPLER_MIPMAP_MODE_NEAREST ? WGPUMipmapFilterMode_Nearest
                                                                                     : WGPUMipmapFilterMode_Linear;
        samplerDesc.lodMinClamp = std::clamp(desc.minLod, 0.0f, 32.0f);
        samplerDesc.lodMaxClamp = std::clamp(desc.maxLod, samplerDesc.lodMinClamp, 32.0f);
        samplerDesc.compare = desc.compareEnable ? ToWgpuCompare(desc.compareOp) : WGPUCompareFunction_Undefined;

        // Anisotropy above 1 requires linear filtering on every axis.
        const bool allLinear = samplerDesc.magFilter == WGPUFilterMode_Linear &&
                               samplerDesc.minFilter == WGPUFilterMode_Linear &&
                               samplerDesc.mipmapFilter == WGPUMipmapFilterMode_Linear;
        const float anisotropy = desc.anisotropyEnable && allLinear ? std::clamp(desc.maxAnisotropy, 1.0f, 16.0f) : 1.0f;
        samplerDesc.maxAnisotropy = static_cast<uint16_t>(anisotropy);

        m_sampler = wgpuDeviceCreateSampler(WebGpuRhiImpl::Get()->m_device, &samplerDesc);
        PE_ERROR_IF(!m_sampler, "WebGpuSamplerImpl: failed to create sampler '%s'", owner->GetName().c_str());
    }

    WebGpuSamplerImpl::~WebGpuSamplerImpl()
    {
        if (m_sampler)
            wgpuSamplerRelease(m_sampler);
    }
} // namespace pe
