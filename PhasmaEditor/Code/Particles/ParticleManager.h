#pragma once

namespace pe
{
    class Buffer;

    struct Particle
    {
        vec4 position; // w: life
        vec4 velocity; // w: size
        vec4 color;
    };

    struct ParticleEmitter
    {
        vec4 position;
        vec4 velocity;
        vec4 color;
        float life;
        float size;
        float spawnRate;
    };

    class ParticleManager
    {
    public:
        ParticleManager();
        ~ParticleManager();

        void Init();
        void Update(); // Future logic
        void Destroy();

        Buffer *GetParticleBuffer() { return m_particleBuffer; }
        uint32_t GetParticleCount() const { return m_particleCount; }
        std::vector<ParticleEmitter> &GetEmitters() { return m_emitters; }

    private:
        Buffer *m_particleBuffer = nullptr;
        uint32_t m_particleCount = 0;
        std::vector<ParticleEmitter> m_emitters;
    };
} // namespace pe
