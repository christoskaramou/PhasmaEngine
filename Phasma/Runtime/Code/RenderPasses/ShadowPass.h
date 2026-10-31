#pragma once

#include "OpenPBRMask.h"

namespace pe
{
    class Image;
    class ImageView;
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
        void DeclareInputs(RGBuilder &builder) override;
        void DeclareOutputs(RGBuilder &builder) override;
        void ExecutePass(CommandBuffer *cmd) override;
        void Resize(uint32_t width, uint32_t height) override;
        void Destroy() override;

        [[nodiscard]] bool HasLiveResources() const;
        // Every image the lighting passes sample: the depth maps, then the tint maps (Lighting.hlsl's Shadow[]).
        [[nodiscard]] std::vector<Image *> GetTextures() const;
        [[nodiscard]] std::vector<ImageView *> GetTextureViews() const;
        [[nodiscard]] Buffer *GetUniform(uint32_t frame) const;
        [[nodiscard]] Sampler *GetSampler() const { return m_sampler; }
        [[nodiscard]] float GetCascadeViewZ(uint32_t cascade) const;
        [[nodiscard]] float GetCascadeTexelSizeWorld(uint32_t cascade) const;
        void SetScene(Scene *scene) { m_scene = scene; }
        OpenPBRMask &GetMasks() { return m_masks; }
        void SyncMaskModules(Scene &scene, bool enabled);
        void ClearDepths(CommandBuffer *cmd);
        // m_voxelPassInfo is the packed-voxel shadow caster (own VS); the framework must see it for
        // shader-reload / pipeline lifecycle alongside the base m_passInfo.
        std::vector<PassInfo *> GetPassInfos() noexcept override
        {
            std::vector<PassInfo *> passInfos{m_passInfo.get()};
            if (m_voxelPassInfo)
                passInfos.push_back(m_voxelPassInfo.get());
            if (m_cullPassInfo)
                passInfos.push_back(m_cullPassInfo.get());
            if (m_fallbackPassInfo)
                passInfos.push_back(m_fallbackPassInfo.get());
            if (m_tintPassInfo)
                passInfos.push_back(m_tintPassInfo.get());
            m_masks.AppendPassInfos(passInfos);
            return passInfos;
        }

    private:
        OpenPBRMask m_masks;
        bool m_maskEnabled = false;
        friend class LightOpaquePass;
        friend class LightTransparentPass;

        void CalculateCascades(Camera *camera);
        void UpdateFallbackPassInfo();
        void UpdateTintPassInfo();
        void DrawTints(CommandBuffer *cmd, uint32_t cascade, const PushConstants_Shadows &pc, bool shadowCull,
                       uint32_t regularCount);
        void BindScene(PassInfo &info, Scene &scene);
        void DrawFallback(CommandBuffer *cmd, const PushConstants_Shadows &pc,
                          Buffer *indirect, Buffer *counters, uint32_t counter, uint32_t capacity);

        static std::array<vec4, 6> ExtractFrustumPlanes(const mat4 &vp);
        static bool AABBInFrustum(const AABB &aabb, const std::array<vec4, 6> &planes);

        std::vector<Buffer *> m_uniforms;
        std::vector<mat4> m_cascades;
        vec4 m_viewZ = vec4(0.0f);
        vec4 m_texelSizeWorld = vec4(0.0f);
        std::vector<Image *> m_textures{};
        // Half-size RGBA16F per cascade: Transmission casters' transmittance product (rgb) and nearest depth (a).
        // ponytail: always allocated (32 MB at the default 2048 maps) so the lighting descriptors never change with
        // the scene; allocate on the first Transmission mesh if that VRAM matters.
        std::vector<Image *> m_tints{};
        Attachment m_tintAttachment{};
        Sampler *m_sampler = nullptr;
        Scene *m_scene = nullptr;
        std::shared_ptr<PassInfo> m_voxelPassInfo; // packed-voxel shadow caster pipeline
        std::shared_ptr<PassInfo> m_cullPassInfo;  // per-cascade light-frustum compact
        std::shared_ptr<PassInfo> m_fallbackPassInfo;
        std::shared_ptr<PassInfo> m_tintPassInfo; // Transmission casters into m_tints

        std::vector<std::array<vec4, 6>> m_cascadePlanes; // [cascade]
    };
} // namespace pe
