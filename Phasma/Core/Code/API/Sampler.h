#pragma once

#include "API/RHITypes.h"

namespace pe
{
    struct SamplerDesc
    {
        PeFilter magFilter = PE_FILTER_LINEAR;
        PeFilter minFilter = PE_FILTER_LINEAR;
        PeSamplerMipmapMode mipmapMode = PE_SAMPLER_MIPMAP_MODE_LINEAR;
        PeSamplerAddressMode addressModeU = PE_SAMPLER_ADDRESS_MODE_REPEAT;
        PeSamplerAddressMode addressModeV = PE_SAMPLER_ADDRESS_MODE_REPEAT;
        PeSamplerAddressMode addressModeW = PE_SAMPLER_ADDRESS_MODE_REPEAT;
        float mipLodBias = 0.0f;
        bool anisotropyEnable = true;
        float maxAnisotropy = 16.0f;
        bool compareEnable = false;
        PeCompareOp compareOp = PE_COMPARE_OP_LESS;
        float minLod = -1000.0f;
        float maxLod = 1000.0f;
        PeSamplerBorderColor borderColor = PE_SAMPLER_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
        bool unnormalizedCoordinates = false;
    };

    class Sampler : public NoCopy
    {
    public:
        struct Impl;

        static Sampler *Create(const SamplerDesc &desc, const std::string &name = "");
        static void Destroy(Sampler *&sampler);
        static std::vector<Sampler *> GetHandles();

        static SamplerDesc CreateInfoInit();

        const SamplerDesc &GetDesc() const { return m_desc; }
        const std::string &GetName() const { return m_name; }

    private:
        friend struct VulkanSamplerImpl;
#if defined(PE_WIN32)
        friend struct Dx12SamplerImpl;
#endif

        Sampler(const SamplerDesc &desc, const std::string &name);
        ~Sampler();

        Impl *m_impl{};
        SamplerDesc m_desc{};
        std::string m_name{};
    };
} // namespace pe
