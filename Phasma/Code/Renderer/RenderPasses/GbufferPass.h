#pragma once

#include "Renderer/Skybox.h"
#include "Renderer/RenderPasses/ShadowPass.h"
#include "Renderer/Pipeline.h"
#include "Renderer/RenderPass.h"

namespace pe
{
    class Descriptor;
    class Image;
    class RenderPass;
    class PassInfo;
    class DescriptorLayout;
    class Geometry;
    class CommandBuffer;
    class Queue;

    class GbufferPass : public IRenderPassComponent
    {
    public:
        GbufferPass();

        ~GbufferPass();

        void Init() override;

        void UpdatePassInfo() override;

        void CreateUniforms(CommandBuffer *cmd) override;

        void UpdateDescriptorSets() override;

        void Update(Camera *camera) override;

        CommandBuffer *Draw() override;

        void Resize(uint32_t width, uint32_t height) override;

        void Destroy() override;

        void SetCommandBuffer(CommandBuffer *cmd) { m_cmd = cmd; }

        void SetGeometry(Geometry *geometry) { m_geometry = geometry; }

        void ClearRenderTargets(CommandBuffer *cmd);

        void ClearDepthStencil(CommandBuffer *cmd);

        void SetBlendType(BlendType blendType) { m_blendType = blendType; }

    private:
        friend class Geometry;
        friend class Renderer;
        friend class Scene;
        
        void PassBarriers(CommandBuffer *cmd);
        
        Buffer *uniform;
        Descriptor *DSetShadows;
        Descriptor *DSetSkybox;
        Image *ibl_brdf_lut;
        Image *normalRT;
        Image *albedoRT;
        Image *srmRT;
        Image *velocityRT;
        Image *emissiveRT;
        Image *viewportRT;
        Image *transparencyRT;
        Image *depthStencilRT;

        PassInfo m_passInfoOpaque;
        PassInfo m_passInfoAlpha;
        PassInfo m_passInfoAABBs;
        PassInfo m_passInfoDepthPrePass;

        Geometry *m_geometry;
        BlendType m_blendType = BlendType::None;

        Queue *m_renderQueue;
        CommandBuffer *m_cmd;

        std::vector<Attachment> m_attachmentsOpaque;
        std::vector<Attachment> m_attachmentsTransparent;         
    };
}