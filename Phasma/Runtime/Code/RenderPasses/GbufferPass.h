#pragma once

#include "Scene/MeshConstants.h"

namespace pe
{
    class Image;
    class Buffer;
    class CommandBuffer;
    class PassInfo;
    class PassInfoAsset;
    class ImageView;
    class Scene;

    struct PushConstants_GBuffer
    {
        uint32_t jointsCount;
        float pad0;
        vec2 projJitter;
        vec2 prevProjJitter;
        uint32_t passType;
        float pad1;
    };

    class GbufferOpaquePass : public IRenderPassComponent
    {
    public:
        void Init() override;
        void UpdatePassInfo() override;
        void CreateUniforms(CommandBuffer *cmd) override {};
        void UpdateDescriptorSets() override { m_lastGeometryVersion = ~0ull; }
        void Update() override;
        void ExecutePass(CommandBuffer *cmd) override;
        void Resize(uint32_t width, uint32_t height) override;
        void Destroy() override;
        std::vector<PassInfo *> GetPassInfos() noexcept override
        {
            std::vector<PassInfo *> passInfos{m_passInfo.get()};
            if (m_passInfoDS)
                passInfos.push_back(m_passInfoDS.get());
            if (m_voxelPassInfo)
                passInfos.push_back(m_voxelPassInfo.get());
            return passInfos;
        }

        void SetScene(Scene *scene) { m_scene = scene; }
        void ClearRenderTargets(CommandBuffer *cmd);
        void ClearDepthStencil(CommandBuffer *cmd);

    private:
        void PassBarriers(CommandBuffer *cmd);

        std::shared_ptr<PassInfo> m_passInfoDS; // double-sided variant (cullMode=eNone)
        std::shared_ptr<PassInfo> m_voxelPassInfo;

        ResourceHandle<PassInfoAsset> m_passAsset;
        ResourceHandle<PassInfoAsset> m_voxelPassAsset;
        Image *m_ibl_brdf_lut;
        Image *m_normalRT;
        Image *m_albedoRT;
        Image *m_srmRT;
        Image *m_velocityRT;
        Image *m_emissiveRT;
        Image *m_viewportRT;
        Image *m_transparencyRT;
        Image *m_depthStencilRT;

        Scene *m_scene = nullptr;
        uint64_t m_lastGeometryVersion = 0;
        ImageView *m_lastVoxelAtlasView = nullptr;
    };

    class GbufferTransparentPass : public IRenderPassComponent
    {
    public:
        void Init() override;
        void UpdatePassInfo() override;
        void CreateUniforms(CommandBuffer *cmd) override {};
        void UpdateDescriptorSets() override { m_lastGeometryVersion = ~0ull; }
        void Update() override;
        void ExecutePass(CommandBuffer *cmd) override;
        void Resize(uint32_t width, uint32_t height) override;
        void Destroy() override;
        std::vector<PassInfo *> GetPassInfos() noexcept override
        {
            std::vector<PassInfo *> passInfos{m_passInfo.get()};
            if (m_voxelPassInfo)
                passInfos.push_back(m_voxelPassInfo.get());
            return passInfos;
        }

        void SetScene(Scene *scene) { m_scene = scene; }
        void ClearRenderTargets(CommandBuffer *cmd);
        void ClearDepthStencil(CommandBuffer *cmd);

    private:
        void PassBarriers(CommandBuffer *cmd);

        std::shared_ptr<PassInfo> m_voxelPassInfo; // voxel water (transparent variant of voxel_gbuffer)
        ResourceHandle<PassInfoAsset> m_passAsset;
        ResourceHandle<PassInfoAsset> m_voxelPassAsset;
        ImageView *m_lastVoxelAtlasView = nullptr;
        Image *m_ibl_brdf_lut;
        Image *m_normalRT;
        Image *m_albedoRT;
        Image *m_srmRT;
        Image *m_velocityRT;
        Image *m_emissiveRT;
        Image *m_viewportRT;
        Image *m_transparencyRT;
        Image *m_depthStencilRT;

        Scene *m_scene = nullptr;
        uint64_t m_lastGeometryVersion = 0;
    };
} // namespace pe
