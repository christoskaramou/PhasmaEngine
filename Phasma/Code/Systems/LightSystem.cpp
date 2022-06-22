/*
Copyright (c) 2018-2021 Christos Karamoustos

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

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
        descriptorSet = nullptr;
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

        DescriptorBindingInfo bindingInfo{};
        bindingInfo.binding = 0;
        bindingInfo.type = DescriptorType::UniformBufferDynamic;
        bindingInfo.pBuffer = uniform;

        DescriptorInfo info{};
        info.count = 1;
        info.bindingInfos = &bindingInfo;
        info.stage = ShaderStage::FragmentBit;

        descriptorSet = Descriptor::Create(&info, "Lights_descriptor");
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
        Descriptor::Destroy(descriptorSet);
    }
}
