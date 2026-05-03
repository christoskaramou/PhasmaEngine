#include "Particles/ParticleManager.h"
#include "API/Buffer.h"
#include "API/Command.h"
#include "API/Image.h"
#include "API/Queue.h"
#include "API/RHI.h"

namespace pe
{
    namespace
    {
        PeMemoryUsage ParticleBufferMemoryUsage()
        {
            return RHII.GetApi() == PE_GRAPHICS_API_DX12 ? PE_MEMORY_USAGE_GPU_ONLY : PE_MEMORY_USAGE_CPU_TO_GPU;
        }

        void ZeroParticleBuffer(Buffer *buffer)
        {
            if (!buffer)
                return;

            if (RHII.GetApi() == PE_GRAPHICS_API_DX12)
            {
                Queue *queue = RHII.GetMainQueue();
                CommandBuffer *cmd = queue->AcquireCommandBuffer();
                cmd->Begin();
                cmd->FillBuffer(buffer, 0, buffer->Size(), 0);
                cmd->End();
                queue->Submit(1, &cmd, nullptr, nullptr);
                cmd->Wait();
                cmd->Return();
                return;
            }

            buffer->Map();
            buffer->Zero();
            buffer->Flush();
            buffer->Unmap();
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

        m_particleCount = 100; // Default particle count, can change at runtime
        size_t bufferSize = m_particleCount * sizeof(Particle);

        m_particleBuffer = Buffer::Create({
            .size = bufferSize,
            .usage = PE_BUFFER_USAGE_STORAGE_BUFFER | PE_BUFFER_USAGE_TRANSFER_DST | PE_BUFFER_USAGE_VERTEX_BUFFER,
            .memoryUsage = ParticleBufferMemoryUsage(),
            .name = "particle_buffer",
        });
        ZeroParticleBuffer(m_particleBuffer);

        // Create emitter buffer (start with space for 16 emitters, will resize if needed)
        size_t emitterBufferSize = 16 * sizeof(ParticleEmitter);
        m_emitterBuffer = Buffer::Create({
            .size = emitterBufferSize,
            .usage = PE_BUFFER_USAGE_STORAGE_BUFFER | PE_BUFFER_USAGE_TRANSFER_DST,
            .memoryUsage = PE_MEMORY_USAGE_CPU_TO_GPU,
            .name = "emitter_buffer",
        });

        m_emitterBuffer->Map();
        m_emitterBuffer->Zero();
        m_emitterBuffer->Flush();
        m_emitterBuffer->Unmap();
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

        // 2. Resize Particle Buffer if needed
        // Logic: Grown/Shrink with hysteresis (step of 1024)
        uint32_t step = 1024;
        bool reallocate = false;
        uint32_t newCapacity = m_gpuCapacity;

        if (totalParticles > m_gpuCapacity)
        {
            // Grow: Ensure strictly enough space, multiple of step
            newCapacity = ((totalParticles + step - 1) / step) * step;
            reallocate = true;
        }
        else if (m_gpuCapacity > step && totalParticles < (m_gpuCapacity - step))
        {
            // Shrink: If we have more than one full step of unused space
            // e.g. Capacity 2048, Particles 1000 -> Keep 2048
            //      Capacity 2048, Particles 100  -> Shrink to 1024

            // Safe check to avoid 0 capacity if totalParticles is 0 (though 0 usually means 0 buffer)
            uint32_t needed = ((totalParticles + step - 1) / step) * step;
            if (needed < m_gpuCapacity)
            {
                newCapacity = needed;
                reallocate = true;
            }
        }

        if (reallocate || !m_particleBuffer)
        {
            // Wait for queue idle before destroying buffer in use
            RHII.GetMainQueue()->WaitIdle();

            m_gpuCapacity = newCapacity;

            if (m_particleBuffer)
            {
                Buffer::Destroy(m_particleBuffer);
                m_particleBuffer = nullptr;
            }

            if (m_gpuCapacity > 0)
            {
                size_t bufferSize = m_gpuCapacity * sizeof(Particle);
                m_particleBuffer = Buffer::Create({
                    .size = bufferSize,
                    .usage = PE_BUFFER_USAGE_STORAGE_BUFFER | PE_BUFFER_USAGE_TRANSFER_DST | PE_BUFFER_USAGE_VERTEX_BUFFER,
                    .memoryUsage = ParticleBufferMemoryUsage(),
                    .name = "particle_buffer",
                });
                ZeroParticleBuffer(m_particleBuffer);
            }
        }

        // Update active count for dispatch/draw
        m_particleCount = totalParticles;

        // 3. Update Emitter Buffer
        size_t emitterCount = m_emitters.size();
        size_t requiredSize = std::max(emitterCount, size_t(1)) * sizeof(ParticleEmitter);

        if (!m_emitterBuffer || requiredSize > m_emitterBuffer->Size())
        {
            if (m_emitterBuffer)
                Buffer::Destroy(m_emitterBuffer);
            m_emitterBuffer = Buffer::Create({
                .size = requiredSize,
                .usage = PE_BUFFER_USAGE_STORAGE_BUFFER | PE_BUFFER_USAGE_TRANSFER_DST,
                .memoryUsage = PE_MEMORY_USAGE_CPU_TO_GPU,
                .name = "emitter_buffer",
            });
        }

        if (m_emitters.empty())
        {
            // Create a dummy emitter if none exist to satisfy shader binding
            // But with 0 count so it does nothing
            ParticleEmitter emitter{};
            emitter.count = 0;
            BufferRange range = {&emitter, sizeof(ParticleEmitter), 0};
            m_emitterBuffer->Copy(1, &range, false);
        }
        else
        {
            BufferRange range = {m_emitters.data(), m_emitters.size() * sizeof(ParticleEmitter), 0};
            m_emitterBuffer->Copy(1, &range, false);
        }

        // Increment version to signal changes to passes
        m_bufferVersion++;
    }

    void ParticleManager::Update()
    {
    }

    void ParticleManager::InitTextures(CommandBuffer *cmd)
    {
        // Load default texture
        m_textures.push_back(Image::LoadRGBA8(cmd, Path::Assets + "Particles/particle_white_soft.png"));
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
        if (!std::filesystem::exists(path))
        {
            // Try relative to Assets
            path = std::filesystem::path(Path::Assets) / pathInput;
            if (!std::filesystem::exists(path))
            {
                // Try relative to Particles
                path = std::filesystem::path(Path::Assets) / "Particles" / pathInput;
                if (!std::filesystem::exists(path))
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
        if (m_emitterBuffer)
        {
            Buffer::Destroy(m_emitterBuffer);
            m_emitterBuffer = nullptr;
        }
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
    }
} // namespace pe
