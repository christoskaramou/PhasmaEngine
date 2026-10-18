#include "API/Sampler_Internal.h"
#include "API/RHI.h"
#include "API/Vulkan/VulkanSamplerImpl.h"
#if defined(PE_WIN32)
#include "API/DX12/Dx12SamplerImpl.h"
#endif

namespace pe
{
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

    namespace
    {
        using SamplerKey = std::tuple<PeFilter,
                                      PeFilter,
                                      PeSamplerMipmapMode,
                                      PeSamplerAddressMode,
                                      PeSamplerAddressMode,
                                      PeSamplerAddressMode,
                                      float,
                                      bool,
                                      float,
                                      bool,
                                      PeCompareOp,
                                      float,
                                      float,
                                      PeSamplerBorderColor,
                                      bool>;

        SamplerKey MakeSamplerKey(const SamplerDesc &desc)
        {
            return {desc.magFilter,
                    desc.minFilter,
                    desc.mipmapMode,
                    desc.addressModeU,
                    desc.addressModeV,
                    desc.addressModeW,
                    desc.mipLodBias,
                    desc.anisotropyEnable,
                    desc.maxAnisotropy,
                    desc.compareEnable,
                    desc.compareOp,
                    desc.minLod,
                    desc.maxLod,
                    desc.borderColor,
                    desc.unnormalizedCoordinates};
        }

        struct SharedSampler
        {
            Sampler *sampler{};
            uint32_t references{};
        };

        struct SamplerRegistry
        {
            std::mutex mutex;
            std::map<SamplerKey, SharedSampler> samplers;
        };

        // Never destroyed, so a sampler released during static destruction still finds it.
        SamplerRegistry &Registry()
        {
            static auto *registry = new SamplerRegistry();
            return *registry;
        }
    } // namespace

    // Samplers are immutable, so identical descriptions share one object. Devices cap sampler objects (4000 on many
    // Vulkan drivers, 2048 in a D3D12 shader-visible sampler heap) and every loaded image asks for its own.
    Sampler *Sampler::Create(const SamplerDesc &desc, const std::string &name)
    {
        SamplerRegistry &registry = Registry();
        std::lock_guard<std::mutex> lock(registry.mutex);
        const SamplerKey key = MakeSamplerKey(desc);
        auto it = registry.samplers.find(key);
        if (it == registry.samplers.end())
        {
            Sampler *sampler = new Sampler(desc, name);
            it = registry.samplers.emplace(key, SharedSampler{sampler, 0}).first;
#if defined(PE_TRACK_RESOURCES)
            PeTracker::Track(typeid(Sampler), reinterpret_cast<void *>(sampler));
#if !defined(PE_TRACK_RESOURCES_NOSPAM)
            PE_INFO("Object Sampler created (Handle: %p)", reinterpret_cast<void *>(sampler));
#endif
#endif
        }

        it->second.references++;
        return it->second.sampler;
    }

    void Sampler::Destroy(Sampler *&sampler)
    {
        if (!sampler)
            return;

        SamplerRegistry &registry = Registry();
        std::lock_guard<std::mutex> lock(registry.mutex);
        auto it = registry.samplers.find(MakeSamplerKey(sampler->m_desc));
        PE_ERROR_IF(it == registry.samplers.end() || it->second.sampler != sampler, "Sampler::Destroy: unknown sampler");
        if (--it->second.references == 0)
        {
#if defined(PE_TRACK_RESOURCES) && !defined(PE_TRACK_RESOURCES_NOSPAM)
            PE_INFO("Object Sampler destroyed (Handle: %p)", reinterpret_cast<void *>(sampler));
#endif
            delete sampler;
#if defined(PE_TRACK_RESOURCES)
            PeTracker::Untrack(typeid(Sampler), reinterpret_cast<void *>(sampler));
#endif
            registry.samplers.erase(it);
        }

        sampler = nullptr;
    }

    std::vector<Sampler *> Sampler::GetHandles()
    {
#if defined(PE_TRACK_RESOURCES)
        auto ptrs = PeTracker::GetHandles(typeid(Sampler));
        std::vector<Sampler *> out;
        out.reserve(ptrs.size());
        for (void *p : ptrs)
            out.push_back(static_cast<Sampler *>(p));
        return out;
#else
        return {};
#endif
    }

    SamplerDesc Sampler::CreateInfoInit()
    {
        return {};
    }

    Sampler::Sampler(const SamplerDesc &desc, const std::string &name)
        : m_desc{desc}, m_name{name}
    {
        m_impl = CreateSamplerImpl(this, desc);
    }

    Sampler::~Sampler()
    {
        delete m_impl;
    }
} // namespace pe
