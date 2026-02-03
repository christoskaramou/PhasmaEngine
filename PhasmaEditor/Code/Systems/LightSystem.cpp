#include "LightSystem.h"
#include "API/Buffer.h"
#include "API/RHI.h"
#include "Systems/RendererSystem.h"

namespace pe
{
    LightSystem::LightSystem()
    {
        m_lubo = {};
    }

    LightSystem::~LightSystem()
    {
    }

    void LightSystem::Init(CommandBuffer *cmd)
    {
        uint32_t count = RHII.GetSwapchainImageCount();
        m_uniforms.resize(count);
        m_storageBuffers.resize(count);

        for (uint32_t i = 0; i < count; i++)
        {
            m_uniforms[i] = Buffer::Create(
                RHII.AlignUniform(sizeof(LightsUBO)),
                vk::BufferUsageFlagBits2::eUniformBuffer,
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
                "lights_uniform_buffer");
            m_uniforms[i]->Map();
            m_uniforms[i]->Zero();
            m_uniforms[i]->Flush();
            m_uniforms[i]->Unmap();

            m_storageBuffers[i] = Buffer::Create(
                1024, // Start with some size
                vk::BufferUsageFlagBits2::eStorageBuffer,
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
                "lights_storage_buffer");
            m_storageBuffers[i]->Map();
            m_storageBuffers[i]->Zero();
            m_storageBuffers[i]->Flush();
            m_storageBuffers[i]->Unmap();
        }

        m_pointLights.resize(5);
        for (int i = 0; i < 5; i++)
        {
            PointLight &point = m_pointLights[i];
            point.color = vec4(rand(0.f, 1.f), rand(0.f, 1.f), rand(0.f, 1.f), 1.0f);              // .w = intensity
            point.position = vec4(rand(-10.5f, 10.5f), rand(.7f, 6.7f), rand(-4.5f, 4.5f), 10.0f); // .w = radius
        }

        m_spotLights.resize(5);
        for (int i = 0; i < 5; i++)
        {
            SpotLight &spot = m_spotLights[i];
            spot.color = vec4(rand(0.f, 1.f), rand(0.f, 1.f), rand(0.f, 1.f), 1.0f);              // .w = intensity
            spot.position = vec4(rand(-10.5f, 10.5f), rand(.7f, 6.7f), rand(-4.5f, 4.5f), 10.0f); // .w = range
            spot.rotation = vec4(rand(-90.f, 90.f), rand(-180.f, 180.f), 15.0f, 5.0f);            // .z = angle, .w = falloff
        }

        m_areaLights.resize(5);
        for (int i = 0; i < 5; i++)
        {
            AreaLight &area = m_areaLights[i];
            area.color = vec4(rand(0.f, 1.f), rand(0.f, 1.f), rand(0.f, 1.f), 1.0f);              // .w = intensity
            area.position = vec4(rand(-10.5f, 10.5f), rand(.7f, 6.7f), rand(-4.5f, 4.5f), 20.0f); // .w = range
            area.rotation = vec4(rand(-90.f, 90.f), rand(-180.f, 180.f), 0.0f, 0.0f);             // .x = pitch, .y = yaw
            area.size = vec4(2.0f, 2.0f, 0.0f, 0.0f);                                             // .x = width, .y = height
        }

        m_directionalLights.resize(1);
        auto &gSettings = Settings::Get<GlobalSettings>();
        m_directionalLights[0].color = {.9765f, .8431f, .9098f, gSettings.day ? gSettings.sun_intensity : 0.0f};
        m_directionalLights[0].direction = {gSettings.sun_direction[0], gSettings.sun_direction[1], gSettings.sun_direction[2], 1.f};

        // Initial update to push data to buffers
        Update();
    }

    void LightSystem::Update()
    {
        auto &gSettings = Settings::Get<GlobalSettings>();

        if (gSettings.randomize_lights)
        {
            gSettings.randomize_lights = false;

            for (uint32_t i = 0; i < m_pointLights.size(); i++)
            {
                m_pointLights[i].color = vec4(rand(0.f, 1.f), rand(0.f, 1.f), rand(0.f, 1.f), 1.0f);              // .w = intensity
                m_pointLights[i].position = vec4(rand(-10.5f, 10.5f), rand(.7f, 6.7f), rand(-4.5f, 4.5f), 10.0f); // .w = radius
            }
        }

        // Directional Light update
        if (m_directionalLights.size() > 0)
        {
            m_directionalLights[0].color.w = gSettings.day ? gSettings.sun_intensity : 0.0f;
            m_directionalLights[0].direction = {gSettings.sun_direction[0], gSettings.sun_direction[1], gSettings.sun_direction[2], 1.f};
        }

        // Calculate offsets and total size
        size_t sizeDirectional = m_directionalLights.size() * sizeof(DirectionalLight);
        size_t sizePoint = m_pointLights.size() * sizeof(PointLight);
        size_t sizeSpot = m_spotLights.size() * sizeof(SpotLight);
        size_t sizeArea = m_areaLights.size() * sizeof(AreaLight);

        // Align offsets to 16 bytes (float4) for safety in HLSL ByteAddressBuffer
        // Although ByteAddressBuffer is naturally 4-byte aligned, structured load works best with alignment

        uint32_t offsetDirectional = 0;
        uint32_t offsetPoint = offsetDirectional + static_cast<uint32_t>(sizeDirectional);
        uint32_t offsetSpot = offsetPoint + static_cast<uint32_t>(sizePoint);
        uint32_t offsetArea = offsetSpot + static_cast<uint32_t>(sizeSpot);
        uint32_t totalSize = offsetArea + static_cast<uint32_t>(sizeArea);

        int frameIndex = RHII.GetFrameIndex();
        Buffer *sb = m_storageBuffers[frameIndex];

        if (sb->Size() < totalSize)
        {
            Buffer::Destroy(sb);
            m_storageBuffers[frameIndex] = Buffer::Create(
                totalSize * 2, // Double capacity to avoid frequent reallocations
                vk::BufferUsageFlagBits2::eStorageBuffer,
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
                "lights_storage_buffer");
            sb = m_storageBuffers[frameIndex];
        }

        // Upload data
        sb->Map();
        std::vector<BufferRange> ranges;
        if (sizeDirectional > 0)
            ranges.push_back({&m_directionalLights[0], sizeDirectional, static_cast<size_t>(offsetDirectional)});
        if (sizePoint > 0)
            ranges.push_back({&m_pointLights[0], sizePoint, static_cast<size_t>(offsetPoint)});
        if (sizeSpot > 0)
            ranges.push_back({&m_spotLights[0], sizeSpot, static_cast<size_t>(offsetSpot)});
        if (sizeArea > 0)
            ranges.push_back({&m_areaLights[0], sizeArea, static_cast<size_t>(offsetArea)});

        if (!ranges.empty())
            sb->Copy(static_cast<uint32_t>(ranges.size()), ranges.data(), true);

        sb->Flush();
        sb->Unmap();

        // Update UBO
        m_lubo.numDirectionalLights = static_cast<uint32_t>(m_directionalLights.size());
        m_lubo.numPointLights = static_cast<uint32_t>(m_pointLights.size());
        m_lubo.numSpotLights = static_cast<uint32_t>(m_spotLights.size());
        m_lubo.numAreaLights = static_cast<uint32_t>(m_areaLights.size());
        m_lubo.offsetDirectionalLights = offsetDirectional;
        m_lubo.offsetPointLights = offsetPoint;
        m_lubo.offsetSpotLights = offsetSpot;
        m_lubo.offsetAreaLights = offsetArea;

        BufferRange range{};
        range.data = &m_lubo;
        range.size = sizeof(LightsUBO);
        range.offset = 0;
        m_uniforms[frameIndex]->Copy(1, &range, false);
    }

    void LightSystem::Destroy()
    {
        for (auto &uniform : m_uniforms)
            Buffer::Destroy(uniform);
        for (auto &sb : m_storageBuffers)
            Buffer::Destroy(sb);
    }
} // namespace pe
