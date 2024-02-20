#include "LightSystem.h"
#include "GUI/GUI.h"
#include "Renderer/RHI.h"
#include "Renderer/Descriptor.h"
#include "Systems/CameraSystem.h"
#include "Renderer/Buffer.h"

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
        for (uint32_t i = 0; i < SWAPCHAIN_IMAGES; i++)
        {
            m_uniform[i] = Buffer::Create(
                RHII.AlignUniform(sizeof(LightsUBO)), // * SWAPCHAIN_IMAGES,
                BufferUsage::UniformBufferBit,
                AllocationCreate::HostAccessSequentialWriteBit,
                "lights_uniform_buffer");
            m_uniform[i]->Map();
            m_uniform[i]->Zero();
            m_uniform[i]->Flush();
            m_uniform[i]->Unmap();
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

        MemoryRange mr{};
        mr.data = &m_lubo;
        mr.size = sizeof(LightsUBO);
        mr.offset = 0;
        for (uint32_t i = 0; i < SWAPCHAIN_IMAGES; i++)
            m_uniform[i]->Copy(1, &mr, false);

        std::vector<DescriptorBindingInfo> bindingInfos(1);
        bindingInfos[0].binding = 0;
        bindingInfos[0].type = DescriptorType::UniformBufferDynamic;
    }

    void LightSystem::Update(double delta)
    {
        Camera &camera = *CONTEXT->GetSystem<CameraSystem>()->GetCamera(0);
        m_lubo.camPos = {camera.GetPosition(), 1.0f};
        m_lubo.sun.color = {.9765f, .8431f, .9098f, GUI::sun_intensity};
        m_lubo.sun.direction = {GUI::sun_direction[0], GUI::sun_direction[1], GUI::sun_direction[2], 1.f};

        size_t frameOffset = 0; // RHII.GetFrameDynamicOffset(uniform->Size(), RHII.GetFrameIndex());

        MemoryRange mr{};
        mr.data = &m_lubo;
        mr.size = 3 * sizeof(vec4);
        mr.offset = frameOffset;
        m_uniform[RHII.GetFrameIndex()]->Copy(1, &mr, false);

        if (GUI::randomize_lights)
        {
            GUI::randomize_lights = false;

            for (uint32_t i = 0; i < MAX_POINT_LIGHTS; i++)
            {
                m_lubo.pointLights[i].color = vec4(rand(0.f, 1.f), rand(0.f, 1.f), rand(0.f, 1.f), 1.f);
                m_lubo.pointLights[i].position = vec4(rand(-10.5f, 10.5f), rand(.7f, 6.7f), rand(-4.5f, 4.5f), 10.f);
            }

            mr.data = m_lubo.pointLights;
            mr.size = sizeof(PointLight) * MAX_POINT_LIGHTS;
            mr.offset = offsetof(LightsUBO, pointLights);

            for (uint32_t i = 0; i < SWAPCHAIN_IMAGES; i++)
                m_uniform[i]->Copy(1, &mr, false);
        }
    }

    void LightSystem::Destroy()
    {
        for (uint32_t i = 0; i < SWAPCHAIN_IMAGES; i++)
            Buffer::Destroy(m_uniform[i]);
    }
}
