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
        void DeclareOutputs(RGBuilder &builder) override;
        void ExecutePass(CommandBuffer *cmd) override;
        void Resize(uint32_t width, uint32_t height) override;
        void Destroy() override;

        void SetScene(Scene *scene) { m_scene = scene; }
        void ClearDepths(CommandBuffer *cmd);

    private:
        friend class LightOpaquePass;
        friend class LightTransparentPass;

        void CalculateCascades(Camera *camera);
        void EnsureShadowIndirectCapacity(uint32_t meshCount, uint32_t cascades);
        void BuildShadowIndirects(uint32_t frame);
        void DestroyShadowIndirects();
        static std::array<vec4, 6> ExtractFrustumPlanes(const mat4 &vp);
        static bool AABBInFrustum(const AABB &aabb, const std::array<vec4, 6> &planes);

        std::vector<Buffer *> m_uniforms;
        std::vector<mat4> m_cascades;
        vec4 m_viewZ;
        std::vector<Image *> m_textures{};
        Sampler *m_sampler;
        Scene *m_scene = nullptr;

        // Per-cascade shadow culling
        std::vector<std::array<vec4, 6>> m_cascadePlanes;                            // [cascade]
        std::vector<std::vector<Buffer *>> m_shadowIndirects;                        // [cascade][frame]
        std::vector<uint32_t> m_shadowIndirectCounts;                                // [cascade], current frame
        std::vector<std::vector<vk::DrawIndexedIndirectCommand>> m_shadowCmdScratch; // [cascade] CPU scratch
        uint32_t m_shadowIndirectCapacity = 0;                                       // allocated command slots
    };
} // namespace pe
