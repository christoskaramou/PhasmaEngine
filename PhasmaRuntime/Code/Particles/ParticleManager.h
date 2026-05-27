#pragma once

namespace pe
{
    class Buffer;
    class CommandBuffer;
    class Image;
    class Sampler;

    struct Particle
    {
        vec4 position; // w: life
        vec4 velocity; // w: size
        vec4 color;
        vec4 extra; // x: textureIndex
    };

    struct ParticleEmitter
    {
        vec4 position;   // xyz: position
        vec4 velocity;   // xyz: base velocity direction
        vec4 colorStart; // rgba: start color
        vec4 colorEnd;   // rgba: end color
        vec4 sizeLife;   // x: sizeMin, y: sizeMax, z: lifeMin, w: lifeMax
        vec4 physics;    // x: spawnRate, y: spawnRadius, z: noiseStrength, w: drag
        vec4 gravity;    // xyz: gravity vector
        vec4 animation;  // x: rows, y: cols, z: speed, w: unused
        uint32_t textureIndex;
        uint32_t count;
        uint32_t offset;
        uint32_t orientation; // 0: Billboard, 1: Horizontal, 2: Vertical, 3: Velocity
    };

    struct ParticleBurstDesc
    {
        std::string name = "Burst";
        vec3 position = vec3(0.0f);
        vec3 velocity = vec3(0.0f, 3.0f, 0.0f);
        vec4 colorStart = vec4(1.0f);
        vec4 colorEnd = vec4(1.0f, 0.0f, 0.0f, 0.0f);
        vec3 gravity = vec3(0.0f, -4.0f, 0.0f);
        uint32_t count = 48;
        float sizeMin = 0.04f;
        float sizeMax = 0.16f;
        float lifeMin = 0.20f;
        float lifeMax = 0.50f;
        float spawnRadius = 0.15f;
        float noiseStrength = 3.0f;
        float drag = 1.0f;
        uint32_t textureIndex = 0;
        uint32_t orientation = 0;
        float cleanupDelay = 0.0f;
    };

    class ParticleManager
    {
    public:
        ParticleManager();
        ~ParticleManager();

        void Init();
        void Update();
        void Destroy();

        Buffer *GetParticleBuffer() { return m_particleBuffer; }
        Buffer *GetEmitterBuffer() { return m_emitterBuffer; }
        uint32_t GetParticleCount() const { return m_particleCount; }
        uint32_t GetEmitterCount() const { return static_cast<uint32_t>(m_emitters.size()); }
        std::vector<ParticleEmitter> &GetEmitters() { return m_emitters; }
        std::vector<std::string> &GetEmitterNames() { return m_emitterNames; }
        const std::vector<std::string> &GetEmitterNames() const { return m_emitterNames; }
        void UpdateEmitterBuffer(); // Call this when emitters change
        int EmitBurst(const ParticleBurstDesc &desc);
        void KillEmitterParticles(int index);
        void RemoveEmitter(int index);
        void ClearEmitters();

        // Texture Management
        void InitTextures(CommandBuffer *cmd);
        uint32_t LoadTexture(const std::string &path);
        Image *GetTexture(uint32_t index) { return index < m_textures.size() ? m_textures[index] : nullptr; }
        const std::vector<Image *> &GetTextures() const { return m_textures; }
        const std::vector<std::string> &GetTextureNames() const { return m_textureNames; }
        Sampler *GetSampler() const { return m_sampler; }
        bool HasTextureChanged() { return m_texturesChanged; }
        void ResetTextureChanged() { m_texturesChanged = false; }

        uint32_t GetBufferVersion() const { return m_bufferVersion; }

    private:
        Buffer *m_particleBuffer = nullptr;
        Buffer *m_emitterBuffer = nullptr;
        uint32_t m_particleCount = 0;
        uint32_t m_gpuCapacity = 0;
        uint32_t m_bufferVersion = 0;
        std::vector<ParticleEmitter> m_emitters;
        std::vector<std::string> m_emitterNames;

        std::vector<Image *> m_textures;
        std::vector<std::string> m_textureNames;
        Sampler *m_sampler = nullptr;
        bool m_texturesChanged = false;

        struct TransientEmitter
        {
            int index = -1;
            float timeRemaining = 0.0f;
        };

        std::vector<TransientEmitter> m_transientEmitters;
        uint32_t m_nextBurstToken = 1;

        void ZeroParticleRange(uint32_t firstParticle, uint32_t particleCount);
        void RemoveEmitterNoUpdate(int index);
    };
} // namespace pe
