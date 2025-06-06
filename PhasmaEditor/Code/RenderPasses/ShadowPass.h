#pragma once

namespace pe
{
    class Image;
    class Buffer;
    class Geometry;
    class CommandBuffer;
    class Sampler;
    class ModelGltf;
    class Camera;

    struct PushConstants_Shadows
    {
        mat4 vp;
        uint32_t jointsCount;
    };

    class ShadowPass : public IRenderPassComponent
    {
    public:
        void Init() override;
        void UpdatePassInfo() override;
        void CreateUniforms(CommandBuffer *cmd) override;
        void UpdateDescriptorSets() override;
        void Update() override;
        void Draw(CommandBuffer *cmd) override;
        void Resize(uint32_t width, uint32_t height) override;
        void Destroy() override;

        void SetGeometry(Geometry *geometry) { m_geometry = geometry; }
        void ClearDepths(CommandBuffer *cmd);

    private:
        friend class Geometry;
        friend class Scene;
        friend class LightOpaquePass;
        friend class LightTransparentPass;

        void CalculateCascades(Camera *camera);

        Buffer *m_uniforms[SWAPCHAIN_IMAGES];
        std::vector<mat4> m_cascades;
        vec4 m_viewZ;
        std::vector<Image *> m_textures{};
        Sampler *m_sampler;
        Geometry *m_geometry = nullptr;
    };
}
