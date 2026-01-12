#pragma once

namespace pe
{
    class Image;
    class Buffer;
    class CommandBuffer;
    class Sampler;
    class Camera;
    class Scene;

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
        void ExecutePass(CommandBuffer *cmd) override;
        void Resize(uint32_t width, uint32_t height) override;
        void Destroy() override;

        void SetScene(Scene *scene) { m_scene = scene; }
        void ClearDepths(CommandBuffer *cmd);

    private:
        friend class Scene;
        friend class LightOpaquePass;
        friend class LightTransparentPass;

        void CalculateCascades(Camera *camera);

        std::vector<Buffer *> m_uniforms;
        std::vector<mat4> m_cascades;
        vec4 m_viewZ;
        std::vector<Image *> m_textures{};
        Sampler *m_sampler;
        Scene *m_scene = nullptr;
    };
} // namespace pe
