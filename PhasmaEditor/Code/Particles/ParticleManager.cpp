#include "Particles/ParticleManager.h"
#include "API/Buffer.h"
#include "API/Command.h"
#include "API/Image.h"
#include "API/Queue.h"
#include "API/RHI.h"

namespace pe
{
    ParticleManager::ParticleManager()
    {
    }

    ParticleManager::~ParticleManager()
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
        Sampler::Destroy(m_sampler);
    }

    void ParticleManager::Init()
    {
        if (m_particleBuffer)
            return;

        m_particleCount = 100; // Default particle count, can change at runtime
        size_t bufferSize = m_particleCount * sizeof(Particle);

        m_particleBuffer = Buffer::Create(
            bufferSize,
            vk::BufferUsageFlagBits2::eStorageBuffer | vk::BufferUsageFlagBits2::eTransferDst | vk::BufferUsageFlagBits2::eVertexBuffer,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
            "particle_buffer");

        m_particleBuffer->Map();
        m_particleBuffer->Zero();
        m_particleBuffer->Flush();
        m_particleBuffer->Unmap();

        // Create emitter buffer (start with space for 16 emitters, will resize if needed)
        size_t emitterBufferSize = 16 * sizeof(ParticleEmitter);
        m_emitterBuffer = Buffer::Create(
            emitterBufferSize,
            vk::BufferUsageFlagBits2::eStorageBuffer | vk::BufferUsageFlagBits2::eTransferDst,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
            "emitter_buffer");

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
                m_particleBuffer = Buffer::Create(
                    bufferSize,
                    vk::BufferUsageFlagBits2::eStorageBuffer | vk::BufferUsageFlagBits2::eTransferDst | vk::BufferUsageFlagBits2::eVertexBuffer,
                    VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
                    "particle_buffer");

                m_particleBuffer->Map();
                m_particleBuffer->Zero();
                m_particleBuffer->Flush();
                m_particleBuffer->Unmap();
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
            m_emitterBuffer = Buffer::Create(
                requiredSize,
                vk::BufferUsageFlagBits2::eStorageBuffer | vk::BufferUsageFlagBits2::eTransferDst,
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
                "emitter_buffer");
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

        vk::SamplerCreateInfo samplerCI = Sampler::CreateInfoInit();
        samplerCI.addressModeU = vk::SamplerAddressMode::eClampToEdge;
        samplerCI.addressModeV = vk::SamplerAddressMode::eClampToEdge;
        samplerCI.addressModeW = vk::SamplerAddressMode::eClampToEdge;
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
                    PE_ERROR("ParticleManager::LoadTexture: File not found: %s", pathInput.c_str());
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
                PE_ERROR("ParticleManager: Failed to load image %s", path.string().c_str());
            }
        }
        else
        {
            PE_ERROR("ParticleManager: Unsupported extension %s", ext.c_str());
        }
        return 0;
    }

    void ParticleManager::Destroy()
    {
    }
} // namespace pe
