#include "Particles/ParticleManager.h"
#include "API/Buffer.h"
#include "API/Command.h"
#include "API/Image.h"
#include "API/Queue.h"
#include "API/RHI.h"
#include "Particles/Backends/ParticleBufferBackend.h"

namespace pe
{
    namespace
    {
        constexpr uint32_t kParticleCapacityStep = 1024;

        ParticleEmitter MakeBurstEmitter(const ParticleBurstDesc &desc, float burstToken)
        {
            ParticleEmitter e{};
            const float lifeMin = std::max(0.01f, desc.lifeMin);
            const float lifeMax = std::max(lifeMin, desc.lifeMax);

            e.position = vec4(desc.position, burstToken);
            e.velocity = vec4(desc.velocity, 0.0f);
            e.colorStart = desc.colorStart;
            e.colorEnd = desc.colorEnd;
            e.sizeLife = vec4(std::max(0.0f, desc.sizeMin), std::max(0.0f, desc.sizeMax), lifeMin, lifeMax);
            e.physics = vec4(-1.0f, std::max(0.0f, desc.spawnRadius), desc.noiseStrength, std::max(0.0f, desc.drag));
            e.gravity = vec4(desc.gravity, 0.0f);
            e.animation = vec4(1.0f, 1.0f, 1.0f, 0.0f);
            e.textureIndex = desc.textureIndex;
            e.count = std::max(desc.count, 1u);
            e.offset = 0;
            e.orientation = desc.orientation;
            return e;
        }
    } // namespace

    ParticleManager::ParticleManager()
    {
    }

    ParticleManager::~ParticleManager()
    {
        Destroy();
    }

    void ParticleManager::Init()
    {
        if (m_particleBuffer)
            return;

        m_gpuCapacity = kParticleCapacityStep;
        m_particleCount = 0;
        size_t bufferSize = m_gpuCapacity * sizeof(Particle);

        m_particleBuffer = Buffer::Create({
            .size = bufferSize,
            .usage = PE_BUFFER_USAGE_STORAGE_BUFFER | PE_BUFFER_USAGE_TRANSFER_DST | PE_BUFFER_USAGE_VERTEX_BUFFER,
            .memoryUsage = ParticleBufferBackend::ParticleBufferMemoryUsage(),
            .name = "particle_buffer",
        });
        QueueParticleByteClear(0, m_particleBuffer->Size());

        m_emitterDataVersion++;
    }

    void ParticleManager::UpdateEmitterBuffer()
    {
        // 1. Calculate offsets and total count
        uint32_t totalParticles = 0;
        for (auto &e : m_emitters)
        {
            e.offset = totalParticles;
            totalParticles += e.count;
        }

        // 2. Grow the particle buffer if needed. Do not shrink on transient-burst
        // cleanup; destroying in-use buffers requires a queue idle and causes frame
        // spikes during effect-heavy gameplay.
        bool reallocate = false;
        uint32_t newCapacity = m_gpuCapacity;

        if (totalParticles > m_gpuCapacity)
        {
            newCapacity = ((totalParticles + kParticleCapacityStep - 1) / kParticleCapacityStep) *
                          kParticleCapacityStep;
            reallocate = true;
        }

        if (reallocate || !m_particleBuffer)
        {
            m_gpuCapacity = newCapacity;

            if (m_particleBuffer)
            {
                RHII.AddToDeletionQueue([b = m_particleBuffer]()
                                        { Buffer *buffer = b; Buffer::Destroy(buffer); });
                m_particleBuffer = nullptr;
            }
            m_pendingParticleClears.clear();

            if (m_gpuCapacity > 0)
            {
                size_t bufferSize = m_gpuCapacity * sizeof(Particle);
                m_particleBuffer = Buffer::Create({
                    .size = bufferSize,
                    .usage = PE_BUFFER_USAGE_STORAGE_BUFFER | PE_BUFFER_USAGE_TRANSFER_DST | PE_BUFFER_USAGE_VERTEX_BUFFER,
                    .memoryUsage = ParticleBufferBackend::ParticleBufferMemoryUsage(),
                    .name = "particle_buffer",
                });
                QueueParticleByteClear(0, m_particleBuffer->Size());
            }
        }

        // Update active count for dispatch/draw
        m_particleCount = totalParticles;

        m_emitterDataVersion++;
        m_bufferVersion++;
    }

    Buffer *ParticleManager::GetEmitterBuffer(uint32_t frame) const
    {
        if (frame >= m_emitterBuffers.size())
            return nullptr;
        return m_emitterBuffers[frame];
    }

    Buffer *ParticleManager::PrepareEmitterBufferForFrame(uint32_t frame)
    {
        const uint32_t frameCount = RHII.GetSwapchainImageCount();
        if (frameCount == 0)
            return nullptr;
        if (frame >= frameCount)
            frame %= frameCount;

        if (m_emitterBuffers.size() > frameCount)
        {
            for (size_t i = frameCount; i < m_emitterBuffers.size(); i++)
            {
                if (m_emitterBuffers[i])
                {
                    Buffer *buffer = m_emitterBuffers[i];
                    Buffer::Destroy(buffer);
                }
            }
            m_emitterBuffers.resize(frameCount);
            m_emitterBufferUploadVersions.resize(frameCount);
        }
        else if (m_emitterBuffers.size() < frameCount)
        {
            m_emitterBuffers.resize(frameCount, nullptr);
            m_emitterBufferUploadVersions.resize(frameCount, 0);
        }

        const size_t requiredSize = std::max<size_t>(m_emitters.size(), 1) * sizeof(ParticleEmitter);
        Buffer *&buffer = m_emitterBuffers[frame];
        if (!buffer || buffer->Size() < requiredSize)
        {
            if (buffer)
                Buffer::Destroy(buffer);

            buffer = Buffer::Create({
                .size = requiredSize,
                .usage = PE_BUFFER_USAGE_STORAGE_BUFFER | PE_BUFFER_USAGE_TRANSFER_DST,
                .memoryUsage = PE_MEMORY_USAGE_CPU_TO_GPU,
                .name = "emitter_buffer_" + std::to_string(frame),
            });
            m_emitterBufferUploadVersions[frame] = 0;
        }

        if (m_emitterBufferUploadVersions[frame] != m_emitterDataVersion)
        {
            if (m_emitters.empty())
            {
                ParticleEmitter emitter{};
                emitter.count = 0;
                BufferRange range = {&emitter, sizeof(ParticleEmitter), 0};
                buffer->Copy(1, &range, false);
            }
            else
            {
                BufferRange range = {m_emitters.data(), m_emitters.size() * sizeof(ParticleEmitter), 0};
                buffer->Copy(1, &range, false);
            }
            m_emitterBufferUploadVersions[frame] = m_emitterDataVersion;
        }

        return buffer;
    }

    int ParticleManager::EmitBurst(const ParticleBurstDesc &desc)
    {
        const float lifeMin = std::max(0.01f, desc.lifeMin);
        const float lifeMax = std::max(lifeMin, desc.lifeMax);
        const float burstToken = static_cast<float>(m_nextBurstToken++);
        ParticleEmitter e = MakeBurstEmitter(desc, burstToken);
        const std::string name = desc.name.empty() ? "Burst" : desc.name;
        const float cleanupDelay = desc.cleanupDelay > 0.0f ? desc.cleanupDelay : lifeMax + 0.25f;

        for (TransientEmitter &transient : m_transientEmitters)
        {
            if (transient.timeRemaining > 0.0f)
                continue;

            const int index = transient.index;
            if (index < 0 || index >= static_cast<int>(m_emitters.size()))
                continue;

            const ParticleEmitter &oldEmitter = m_emitters[index];
            if (oldEmitter.count != e.count)
                continue;

            QueueParticleRangeClear(oldEmitter.offset, oldEmitter.count);
            m_emitters[index] = e;
            if (index < static_cast<int>(m_emitterNames.size()))
                m_emitterNames[index] = name;
            transient.timeRemaining = cleanupDelay;
            UpdateEmitterBuffer();
            return index;
        }

        m_emitters.push_back(e);
        m_emitterNames.push_back(name);

        const int index = static_cast<int>(m_emitters.size() - 1);
        m_transientEmitters.push_back({index, cleanupDelay});

        UpdateEmitterBuffer();
        return index;
    }

    void ParticleManager::FlushPendingParticleClears(CommandBuffer *cmd)
    {
        if (m_pendingParticleClears.empty())
            return;

        if (!cmd || !m_particleBuffer)
        {
            m_pendingParticleClears.clear();
            return;
        }

        std::sort(m_pendingParticleClears.begin(), m_pendingParticleClears.end(),
                  [](const PendingParticleClear &lhs, const PendingParticleClear &rhs)
                  { return lhs.byteOffset < rhs.byteOffset; });

        const size_t bufferSize = m_particleBuffer->Size();
        PendingParticleClear merged = m_pendingParticleClears.front();
        auto flushMerged = [&]()
        {
            if (merged.byteOffset >= bufferSize || merged.byteSize == 0)
                return;

            const size_t byteSize = std::min(merged.byteSize, bufferSize - merged.byteOffset);
            cmd->FillBuffer(m_particleBuffer, merged.byteOffset, byteSize, 0);
        };

        for (size_t i = 1; i < m_pendingParticleClears.size(); ++i)
        {
            const PendingParticleClear &next = m_pendingParticleClears[i];
            const size_t mergedEnd = merged.byteOffset + merged.byteSize;
            if (next.byteOffset <= mergedEnd)
            {
                const size_t nextEnd = next.byteOffset + next.byteSize;
                merged.byteSize = std::max(mergedEnd, nextEnd) - merged.byteOffset;
                continue;
            }

            flushMerged();
            merged = next;
        }

        flushMerged();
        m_pendingParticleClears.clear();
    }

    void ParticleManager::QueueParticleByteClear(size_t byteOffset, size_t byteSize)
    {
        if (!m_particleBuffer || byteSize == 0 || byteOffset >= m_particleBuffer->Size())
            return;

        m_pendingParticleClears.push_back({byteOffset, byteSize});
    }

    void ParticleManager::QueueParticleRangeClear(uint32_t firstParticle, uint32_t particleCount)
    {
        if (!m_particleBuffer || particleCount == 0)
            return;

        const size_t byteOffset = static_cast<size_t>(firstParticle) * sizeof(Particle);
        const size_t byteSize = static_cast<size_t>(particleCount) * sizeof(Particle);
        QueueParticleByteClear(byteOffset, byteSize);
    }

    void ParticleManager::KillEmitterParticles(int index)
    {
        if (index < 0 || index >= static_cast<int>(m_emitters.size()))
            return;

        const ParticleEmitter &emitter = m_emitters[index];
        QueueParticleRangeClear(emitter.offset, emitter.count);
    }

    void ParticleManager::RemoveEmitterNoUpdate(int index)
    {
        if (index < 0 || index >= static_cast<int>(m_emitters.size()))
            return;

        const uint32_t oldParticleCount = m_particleCount;
        const uint32_t firstShiftedParticle = m_emitters[index].offset;
        if (firstShiftedParticle < oldParticleCount)
            QueueParticleRangeClear(firstShiftedParticle, oldParticleCount - firstShiftedParticle);

        m_emitters.erase(m_emitters.begin() + index);
        if (index < static_cast<int>(m_emitterNames.size()))
            m_emitterNames.erase(m_emitterNames.begin() + index);

        for (auto it = m_transientEmitters.begin(); it != m_transientEmitters.end();)
        {
            if (it->index == index)
                it = m_transientEmitters.erase(it);
            else
            {
                if (it->index > index)
                    --it->index;
                ++it;
            }
        }
    }

    void ParticleManager::RemoveEmitter(int index)
    {
        RemoveEmitterNoUpdate(index);
        UpdateEmitterBuffer();
    }

    void ParticleManager::ClearEmitters()
    {
        m_emitters.clear();
        m_emitterNames.clear();
        m_transientEmitters.clear();
        QueueParticleRangeClear(0, m_particleCount);
        UpdateEmitterBuffer();
    }

    void ParticleManager::Update()
    {
        if (m_transientEmitters.empty())
            return;

        const float dt = std::max(0.0f, static_cast<float>(FrameTimer::Instance().GetDelta()) *
                                            Settings::Get<SceneSettings>().time_scale);
        if (dt <= 0.0f)
            return;

        for (TransientEmitter &transient : m_transientEmitters)
            transient.timeRemaining -= dt;

        // Compact from the tail only: RemoveEmitterNoUpdate clears the particle buffer
        // from the removed emitter's offset to the end, which would wipe live particles
        // of any later emitter. A transient stranded behind an active one stays put
        // until the tail clears; its particles age out on the GPU (token mismatch
        // never re-fires once Reset() runs), so leaving the slot is visually harmless.
        bool changed = false;
        while (!m_emitters.empty())
        {
            const int tail = static_cast<int>(m_emitters.size() - 1);
            const TransientEmitter *tailTransient = nullptr;
            for (const TransientEmitter &candidate : m_transientEmitters)
            {
                if (candidate.index == tail)
                {
                    tailTransient = &candidate;
                    break;
                }
            }
            if (!tailTransient || tailTransient->timeRemaining > 0.0f)
                break;

            RemoveEmitterNoUpdate(tail);
            changed = true;
        }

        if (changed)
            UpdateEmitterBuffer();
    }

    void ParticleManager::InitTextures(CommandBuffer *cmd)
    {
        // Load default texture
        m_textures.push_back(Image::LoadRGBA8(cmd, Path::RuntimeAssets + "Particles/particle_white_soft.png"));
        m_textureNames.push_back("Default Soft");

        SamplerDesc samplerCI = Sampler::CreateInfoInit();
        samplerCI.addressModeU = PE_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerCI.addressModeV = PE_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerCI.addressModeW = PE_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        m_sampler = Sampler::Create(samplerCI, "ParticleSampler");
    }

    uint32_t ParticleManager::LoadTexture(const std::string &pathInput)
    {
        std::filesystem::path path(pathInput);

        // Try to find the file
        if (!AssetFileExists(path))
        {
            // Try relative to Assets
            path = std::filesystem::path(Path::Assets) / pathInput;
            if (!AssetFileExists(path))
            {
                // Try relative to Particles
                path = std::filesystem::path(Path::Assets) / "Particles" / pathInput;
                if (!AssetFileExists(path))
                {
                    PE_ERROR("[Particles] LoadTexture: File not found: %s", pathInput.c_str());
                    return 0;
                }
            }
        }

        std::string ext = path.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga")
        {
            // Check for duplicates
            std::string name = path.filename().string();
            for (uint32_t i = 0; i < m_textureNames.size(); ++i)
                if (m_textureNames[i] == name)
                    return i;

            Queue *queue = RHII.GetMainQueue();
            CommandBuffer *cmd = queue->AcquireCommandBuffer();
            cmd->Begin();

            Image *img = Image::LoadRGBA8(cmd, path.string());

            cmd->End();
            queue->Submit(1, &cmd, nullptr, nullptr);
            cmd->Wait();
            cmd->Return();

            if (img)
            {
                m_textures.push_back(img);
                m_textureNames.push_back(name);
                m_texturesChanged = true;
                PE_INFO("ParticleManager: Loaded texture %s", name.c_str());
                return static_cast<uint32_t>(m_textures.size() - 1);
            }
            else
            {
                PE_ERROR("[Particles] Failed to load image %s", path.string().c_str());
            }
        }
        else
        {
            PE_ERROR("[Particles] Unsupported extension %s", ext.c_str());
        }
        return 0;
    }

    void ParticleManager::Destroy()
    {
        if (m_particleBuffer)
        {
            Buffer::Destroy(m_particleBuffer);
            m_particleBuffer = nullptr;
        }
        for (Buffer *&buffer : m_emitterBuffers)
        {
            if (buffer)
                Buffer::Destroy(buffer);
            buffer = nullptr;
        }
        m_emitterBuffers.clear();
        m_emitterBufferUploadVersions.clear();
        for (auto *img : m_textures)
            Image::Destroy(img);
        m_textures.clear();
        m_textureNames.clear();
        if (m_sampler)
        {
            Sampler::Destroy(m_sampler);
            m_sampler = nullptr;
        }
        m_emitters.clear();
        m_emitterNames.clear();
        m_transientEmitters.clear();
        m_pendingParticleClears.clear();
    }
} // namespace pe
