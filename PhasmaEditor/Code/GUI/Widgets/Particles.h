#pragma once
#include "GUI/Widget.h"

namespace pe
{
    class ParticleManager;

    class Particles : public Widget
    {
    public:
        Particles() : Widget("Particles") { m_open = false; }
        void Update() override;
        void DrawEmbed(ParticleManager *pm, int emitterIndex);

    private:
        void DrawEmitterControls(ParticleManager *pm, size_t i);
        std::unordered_map<void *, void *> m_textureCache; // Image* -> VkDescriptorSet (stored as void*)
    };
} // namespace pe
