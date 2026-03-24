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

        m_directionalLights.resize(1);
        auto &gSettings = Settings::Get<GlobalSettings>();
        m_directionalLights[0].color = {.9765f, .8431f, .9098f, 5.0f};
        m_directionalLights[0].position = {0.0f, 10.0f, 0.0f, 2.0f};
        quat q_dir = quat(radians(vec3(-90.1f, 0.f, 0.f)));
        m_directionalLights[0].rotation = {q_dir.x, q_dir.y, q_dir.z, q_dir.w};
        m_directionalLights[0].name = "Directional Light " + std::to_string(ID::NextID());
        // Initial update to push data to buffers
        Update();
    }

    void LightSystem::Update()
    {
        PE_PROFILE_SCOPE("Light System");
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
            static float lastIntensity = m_directionalLights[0].color.w == 0.0f ? 7.0f : m_directionalLights[0].color.w;
            if (m_directionalLights[0].color.w > 0.0f)
                lastIntensity = m_directionalLights[0].color.w;

            m_directionalLights[0].color.w = gSettings.day ? lastIntensity : 0.0f;
        }

        // Copy light data to POD vectors for GPU upload (reused buffers, no per-frame allocations).
        m_directionalLightsPOD.resize(m_directionalLights.size());
        for (size_t i = 0; i < m_directionalLights.size(); i++)
            m_directionalLightsPOD[i] = m_directionalLights[i];

        m_pointLightsPOD.resize(m_pointLights.size());
        for (size_t i = 0; i < m_pointLights.size(); i++)
            m_pointLightsPOD[i] = m_pointLights[i];

        m_spotLightsPOD.resize(m_spotLights.size());
        for (size_t i = 0; i < m_spotLights.size(); i++)
            m_spotLightsPOD[i] = m_spotLights[i];

        m_areaLightsPOD.resize(m_areaLights.size());
        for (size_t i = 0; i < m_areaLights.size(); i++)
            m_areaLightsPOD[i] = m_areaLights[i];

        // Calculate offsets and total size
        size_t sizeDirectional = m_directionalLightsPOD.size() * sizeof(DirectionalLight);
        size_t sizePoint = m_pointLightsPOD.size() * sizeof(PointLight);
        size_t sizeSpot = m_spotLightsPOD.size() * sizeof(SpotLight);
        size_t sizeArea = m_areaLightsPOD.size() * sizeof(AreaLight);

        auto align16 = [](uint32_t v)
        { return (v + 15u) & ~15u; };
        uint32_t offsetDirectional = 0;
        uint32_t offsetPoint = offsetDirectional + align16(static_cast<uint32_t>(sizeDirectional));
        uint32_t offsetSpot = offsetPoint + align16(static_cast<uint32_t>(sizePoint));
        uint32_t offsetArea = offsetSpot + align16(static_cast<uint32_t>(sizeSpot));
        uint32_t totalSize = offsetArea + static_cast<uint32_t>(sizeArea);

        int frameIndex = RHII.GetFrameIndex();
        Buffer *sb = m_storageBuffers[frameIndex];

        if (sb->Size() < totalSize)
        {
            Buffer::Destroy(sb);
            m_storageBuffers[frameIndex] = Buffer::Create(
                totalSize * 2, // Double capacity to avoid frequent reallocations
                vk::BufferUsageFlagBits2::eStorageBuffer,
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
                "lights_storage_buffer");
            sb = m_storageBuffers[frameIndex];
        }

        // Upload data
        m_uploadRanges.clear();
        m_uploadRanges.reserve(4);
        if (sizeDirectional > 0)
            m_uploadRanges.push_back({m_directionalLightsPOD.data(), sizeDirectional, static_cast<size_t>(offsetDirectional)});
        if (sizePoint > 0)
            m_uploadRanges.push_back({m_pointLightsPOD.data(), sizePoint, static_cast<size_t>(offsetPoint)});
        if (sizeSpot > 0)
            m_uploadRanges.push_back({m_spotLightsPOD.data(), sizeSpot, static_cast<size_t>(offsetSpot)});
        if (sizeArea > 0)
            m_uploadRanges.push_back({m_areaLightsPOD.data(), sizeArea, static_cast<size_t>(offsetArea)});

        if (!m_uploadRanges.empty())
            sb->Copy(static_cast<uint32_t>(m_uploadRanges.size()), m_uploadRanges.data(), true);

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

    void LightSystem::CreateDirectionalLight()
    {
        DirectionalLightEditor l{};
        l.color = vec4(1.0f);
        l.position = vec4(0.0f, 10.0f, 0.0f, 2.0f);
        quat q_dir = quat(radians(vec3(-90.1f, 0.f, 0.f)));
        l.rotation = {q_dir.x, q_dir.y, q_dir.z, q_dir.w};
        l.name = "Directional Light " + std::to_string(ID::NextID());
        m_directionalLights.push_back(l);
    }

    void LightSystem::CreatePointLight()
    {
        PointLightEditor l{};
        l.color = vec4(1.0f);
        l.position = vec4(0.0f, 0.0f, 0.0f, 10.0f);
        l.name = "Point Light " + std::to_string(ID::NextID());
        m_pointLights.push_back(l);
    }

    void LightSystem::CreateSpotLight()
    {
        SpotLightEditor l{};
        l.color = vec4(1.0f);
        l.position = vec4(0.0f, 0.0f, 0.0f, 10.0f);
        l.rotation = vec4(0.0f, 0.0f, 0.0f, 1.0f);
        l.params = vec4(15.0f, 5.0f, 0.0f, 0.0f);
        l.name = "Spot Light " + std::to_string(ID::NextID());
        m_spotLights.push_back(l);
    }

    void LightSystem::CreateAreaLight()
    {
        AreaLightEditor l{};
        l.color = vec4(1.0f);
        l.position = vec4(0.0f, 0.0f, 0.0f, 10.0f);
        l.rotation = vec4(0.0f, 0.0f, 0.0f, 1.0f);
        l.size = vec4(2.0f, 2.0f, 0.0f, 0.0f);
        l.name = "Area Light " + std::to_string(ID::NextID());
        m_areaLights.push_back(l);
    }
} // namespace pe
