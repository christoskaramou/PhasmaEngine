#include "Particles/ParticleManager.h"
#include "API/Buffer.h"

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
    }

    void ParticleManager::Init()
    {
        if (m_particleBuffer)
            return;

        m_particleCount = 1000;
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
    }

    void ParticleManager::Update()
    {
    }

    void ParticleManager::Destroy()
    {
    }
} // namespace pe
