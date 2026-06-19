#pragma once
#include "GUI/Widget.h"

namespace pe
{
    class Image;
    class ParticleManager;

    class Particles : public Widget
    {
    public:
        Particles() : Widget("Particles") { m_open = false; }
        ~Particles() override;
        void Update() override;
        void DrawEmbed(ParticleManager *pm, int emitterIndex);

    private:
        void DrawEmitterControls(ParticleManager *pm, size_t i);
        std::unordered_map<Image *, void *> m_textureCache;
    };
} // namespace pe
