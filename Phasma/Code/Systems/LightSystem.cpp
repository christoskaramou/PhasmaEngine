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
        lubo = {};
        uniform = nullptr;
        DSet = nullptr;
    }

    LightSystem::~LightSystem()
    {
    }

    void LightSystem::Init(CommandBuffer *cmd)
    {
        uniform = Buffer::Create(
            RHII.AlignUniform(sizeof(LightsUBO)) * SWAPCHAIN_IMAGES,
            BufferUsage::UniformBufferBit,
            AllocationCreate::HostAccessSequentialWriteBit,
            "lights_uniform_buffer");
        uniform->Map();
        uniform->Zero();
        uniform->Flush();
        uniform->Unmap();

        for (int i = 0; i < MAX_POINT_LIGHTS; i++)
        {
            PointLight &point = lubo.pointLights[i];
            point.color = vec4(rand(0.f, 1.f), rand(0.f, 1.f), rand(0.f, 1.f), 1.f);
            point.position = vec4(rand(-10.5f, 10.5f), rand(.7f, 6.7f), rand(-4.5f, 4.5f), 10.f);
        }

        for (int i = 0; i < MAX_SPOT_LIGHTS; i++)
        {
            SpotLight &spot = lubo.spotLights[i];
            spot.color = vec4(rand(0.f, 1.f), rand(0.f, 1.f), rand(0.f, 1.f), 1.f);
            spot.start = vec4(rand(-10.5f, 10.5f), rand(.7f, 6.7f), rand(-4.5f, 4.5f), 10.f);
            spot.end = spot.start + normalize(vec4(rand(-1.f, 1.f), rand(-1.f, 1.f), rand(-1.f, 1.f), 0.f));
        }

        MemoryRange mr{};
        mr.data = &lubo;
        mr.size = sizeof(LightsUBO);
        for (uint32_t i = 0; i < SWAPCHAIN_IMAGES; i++)
        {
            mr.offset = RHII.GetFrameDynamicOffset(uniform->Size(), i);
            uniform->Copy(1, &mr, false);
        }

        std::vector<DescriptorBindingInfo> bindingInfos(1);
        bindingInfos[0].binding = 0;
        bindingInfos[0].type = DescriptorType::UniformBufferDynamic;
        DSet = Descriptor::Create(bindingInfos, ShaderStage::FragmentBit, "Lights_descriptor");

        DSet->SetBuffer(0, uniform);
        DSet->UpdateDescriptor();
    }

    void LightSystem::Update(double delta)
    {
        MemoryRange mrs[5]{};
        uint32_t count = 1;

        Camera &camera = *CONTEXT->GetSystem<CameraSystem>()->GetCamera(0);
        lubo.camPos = {camera.position, 1.0f};
        lubo.sun.color = {.9765f, .8431f, .9098f, GUI::sun_intensity};
        lubo.sun.direction = {GUI::sun_direction[0], GUI::sun_direction[1], GUI::sun_direction[2], 1.f};

        size_t frameOffset = RHII.GetFrameDynamicOffset(uniform->Size(), RHII.GetFrameIndex());

        mrs[0].data = &lubo;
        mrs[0].size = 3 * sizeof(vec4);
        mrs[0].offset = frameOffset;

        if (GUI::randomize_lights)
        {
            GUI::randomize_lights = false;

            for (uint32_t i = 0; i < MAX_POINT_LIGHTS; i++)
            {
                lubo.pointLights[i].color = vec4(rand(0.f, 1.f), rand(0.f, 1.f), rand(0.f, 1.f), 1.f);
                lubo.pointLights[i].position = vec4(rand(-10.5f, 10.5f), rand(.7f, 6.7f), rand(-4.5f, 4.5f), 10.f);
            }

            for (uint32_t i = 0; i < SWAPCHAIN_IMAGES; i++)
            {
                mrs[i + 1].data = lubo.pointLights;
                mrs[i + 1].size = sizeof(PointLight) * MAX_POINT_LIGHTS;
                mrs[i + 1].offset =  RHII.GetFrameDynamicOffset(uniform->Size(), i) + offsetof(LightsUBO, pointLights);
                count++;
            }
        }

        uniform->Copy(count, mrs, false);
    }

    void LightSystem::Destroy()
    {
        Buffer::Destroy(uniform);
        Descriptor::Destroy(DSet);
    }
}
