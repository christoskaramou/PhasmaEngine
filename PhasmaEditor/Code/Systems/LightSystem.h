#pragma once

#include "GUI/GUI.h"

namespace pe
{
    struct DirectionalLight
    {
        vec4 color; // .w = intensity
        vec4 position;
        vec4 rotation; // quaternion
    };

    struct PointLight
    {
        vec4 color;    // .w = intensity
        vec4 position; // .w = radius
    };

    struct SpotLight
    {
        vec4 color;    // .w = intensity
        vec4 position; // .w = range
        vec4 rotation; // quaternion
        vec4 params;   // .x = angle, .y = falloff
    };

    struct AreaLight
    {
        vec4 color;    // .w = intensity
        vec4 position; // .w = range
        vec4 rotation; // quaternion
        vec4 size;     // .x = width, .y = height
    };

    struct DirectionalLightEditor : public DirectionalLight
    {
        std::string name;
    };

    struct PointLightEditor : public PointLight
    {
        std::string name;
    };

    struct SpotLightEditor : public SpotLight
    {
        std::string name;
    };

    struct AreaLightEditor : public AreaLight
    {
        std::string name;
    };

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
        std::vector<DirectionalLightEditor> &GetDirectionalLights() { return m_directionalLights; }
        std::vector<PointLightEditor> &GetPointLights() { return m_pointLights; }
        std::vector<SpotLightEditor> &GetSpotLights() { return m_spotLights; }
        std::vector<AreaLightEditor> &GetAreaLights() { return m_areaLights; }

        void CreateDirectionalLight();
        void CreatePointLight();
        void CreateSpotLight();
        void CreateAreaLight();

    private:
        LightsUBO m_lubo;
        std::vector<Buffer *> m_uniforms;
        std::vector<Buffer *> m_storageBuffers;

        std::vector<DirectionalLightEditor> m_directionalLights;
        std::vector<PointLightEditor> m_pointLights;
        std::vector<SpotLightEditor> m_spotLights;
        std::vector<AreaLightEditor> m_areaLights;
    };
} // namespace pe
