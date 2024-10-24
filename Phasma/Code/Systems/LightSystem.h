#pragma once

#include "GUI/GUI.h"
#include "Renderer/Light.h"

namespace pe
{
    constexpr uint32_t MAX_POINT_LIGHTS = 10;
    constexpr uint32_t MAX_SPOT_LIGHTS = 10;

    struct LightsUBO
    {
        vec4 camPos;
        DirectionalLight sun;
        PointLight pointLights[MAX_POINT_LIGHTS];
        SpotLight spotLights[MAX_SPOT_LIGHTS];
    };

    class Buffer;

    class LightSystem : public ISystem
    {
    public:
        LightSystem();
        ~LightSystem();

        void Init(CommandBuffer *cmd) override;
        void Update(double delta) override;
        void Destroy() override;

        Buffer *GetUniform(uint32_t frame) { return m_uniform[frame]; }

    private:
        LightsUBO m_lubo;
        Buffer *m_uniform[SWAPCHAIN_IMAGES];
    };
}
