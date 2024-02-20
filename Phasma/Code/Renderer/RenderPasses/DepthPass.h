#pragma once

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

    class DepthPass : public IRenderPassComponent
    {
    public:
        DepthPass();

        ~DepthPass();

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

        void ClearDepthStencil(CommandBuffer *cmd);

    private:
        friend class Geometry;
        friend class Scene;
        friend class Renderer;
        
        Image *m_depthStencil;
        Geometry *m_geometry;

        PassInfo m_passInfo;
        Attachment attachment;
        
        CommandBuffer *m_cmd;
        Queue *m_renderQueue;
    };
}