#include "LightSystem.h"
#include "API/Buffer.h"
#include "API/RHI.h"
#include "Camera/Camera.h"
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
        m_uniforms.resize(RHII.GetSwapchainImageCount());
        for (auto &uniform : m_uniforms)
        {
            uniform = Buffer::Create(
                RHII.AlignUniform(sizeof(LightsUBO)),
                vk::BufferUsageFlagBits2::eUniformBuffer,
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
                "lights_uniform_buffer");
            uniform->Map();
            uniform->Zero();
            uniform->Flush();
            uniform->Unmap();
        }

        for (int i = 0; i < MAX_POINT_LIGHTS; i++)
        {
            PointLight &point = m_lubo.pointLights[i];
            point.color = vec4(rand(0.f, 1.f), rand(0.f, 1.f), rand(0.f, 1.f), 1.f);
            point.position = vec4(rand(-10.5f, 10.5f), rand(.7f, 6.7f), rand(-4.5f, 4.5f), 10.f);
        }

        for (int i = 0; i < MAX_SPOT_LIGHTS; i++)
        {
            SpotLight &spot = m_lubo.spotLights[i];
            spot.color = vec4(rand(0.f, 1.f), rand(0.f, 1.f), rand(0.f, 1.f), 1.f);
            spot.start = vec4(rand(-10.5f, 10.5f), rand(.7f, 6.7f), rand(-4.5f, 4.5f), 10.f);
            spot.end = spot.start + normalize(vec4(rand(-1.f, 1.f), rand(-1.f, 1.f), rand(-1.f, 1.f), 0.f));
        }

        BufferRange range{};
        range.data = &m_lubo;
        range.size = sizeof(LightsUBO);
        range.offset = 0;
        for (auto &uniform : m_uniforms)
            uniform->Copy(1, &range, false);
    }

    void LightSystem::Update()
    {
        auto &gSettings = Settings::Get<GlobalSettings>();

        Camera &camera = *GetGlobalSystem<RendererSystem>()->GetScene().GetActiveCamera();
        m_lubo.camPos = {camera.GetPosition(), 1.0f};
        m_lubo.sun.color = {.9765f, .8431f, .9098f, gSettings.day ? gSettings.sun_intensity : 0.0f};
        m_lubo.sun.direction = {gSettings.sun_direction[0], gSettings.sun_direction[1], gSettings.sun_direction[2], 1.f};

        BufferRange range{};
        range.data = &m_lubo;
        range.size = 3 * sizeof(vec4);
        range.offset = 0;
        m_uniforms[RHII.GetFrameIndex()]->Copy(1, &range, false);

        if (gSettings.randomize_lights)
        {
            gSettings.randomize_lights = false;

            for (uint32_t i = 0; i < MAX_POINT_LIGHTS; i++)
            {
                m_lubo.pointLights[i].color = vec4(rand(0.f, 1.f), rand(0.f, 1.f), rand(0.f, 1.f), 1.f);
                m_lubo.pointLights[i].position = vec4(rand(-10.5f, 10.5f), rand(.7f, 6.7f), rand(-4.5f, 4.5f), 10.f);
            }

            range.data = m_lubo.pointLights;
            range.size = sizeof(PointLight) * MAX_POINT_LIGHTS;
            range.offset = offsetof(LightsUBO, pointLights);

            for (auto &uniform : m_uniforms)
                uniform->Copy(1, &range, false);
        }
    }

    void LightSystem::Destroy()
    {
        for (auto &uniform : m_uniforms)
            Buffer::Destroy(uniform);
    }
} // namespace pe
