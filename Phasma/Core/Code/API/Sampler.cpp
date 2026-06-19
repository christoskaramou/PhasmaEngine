#include "API/Sampler_Internal.h"

namespace pe
{
    Sampler *Sampler::Create(const SamplerDesc &desc, const std::string &name)
    {
        Sampler *sampler = new Sampler(desc, name);
#if defined(PE_TRACK_RESOURCES)
        PeTracker::Track(typeid(Sampler), reinterpret_cast<void *>(sampler));
#if !defined(PE_TRACK_RESOURCES_NOSPAM)
        PE_INFO("Object Sampler created (Handle: %p)", reinterpret_cast<void *>(sampler));
#endif
#endif
        return sampler;
    }

    void Sampler::Destroy(Sampler *&sampler)
    {
        if (sampler)
        {
#if defined(PE_TRACK_RESOURCES) && !defined(PE_TRACK_RESOURCES_NOSPAM)
            PE_INFO("Object Sampler destroyed (Handle: %p)", reinterpret_cast<void *>(sampler));
#endif
            delete sampler;
#if defined(PE_TRACK_RESOURCES)
            PeTracker::Untrack(typeid(Sampler), reinterpret_cast<void *>(sampler));
#endif
            sampler = nullptr;
        }
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
