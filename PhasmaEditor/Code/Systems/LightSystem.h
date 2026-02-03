#pragma once

#include "API/Light.h"
#include "GUI/GUI.h"

namespace pe
{
    struct LightsUBO
    {
        uint32_t numDirectionalLights;
        uint32_t numPointLights;
        uint32_t numSpotLights;
        uint32_t numAreaLights;
        uint32_t offsetDirectionalLights;
        uint32_t offsetPointLights;
        uint32_t offsetSpotLights;
        uint32_t offsetAreaLights;
    };

    class Buffer;

    class LightSystem : public ISystem
    {
    public:
        LightSystem();
        ~LightSystem();

        void Init(CommandBuffer *cmd) override;
        void Update() override;
        void Destroy() override;

        Buffer *GetUniform(uint32_t frame) { return m_uniforms[frame]; }
        Buffer *GetStorage(uint32_t frame) { return m_storageBuffers[frame]; }
        LightsUBO *GetLights() { return &m_lubo; }
        std::vector<DirectionalLight> &GetDirectionalLights() { return m_directionalLights; }
        std::vector<PointLight> &GetPointLights() { return m_pointLights; }
        std::vector<SpotLight> &GetSpotLights() { return m_spotLights; }
        std::vector<AreaLight> &GetAreaLights() { return m_areaLights; }

    private:
        LightsUBO m_lubo;
        std::vector<Buffer *> m_uniforms;
        std::vector<Buffer *> m_storageBuffers;

        std::vector<DirectionalLight> m_directionalLights;
        std::vector<PointLight> m_pointLights;
        std::vector<SpotLight> m_spotLights;
        std::vector<AreaLight> m_areaLights;
    };
} // namespace pe
