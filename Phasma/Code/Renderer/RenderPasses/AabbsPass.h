#pragma once
#include "Renderer/Pipeline.h"
#include "Renderer/RenderPass.h"

namespace pe
{
    class Image;
    class CommandBuffer;
    class Camera;
    class PassInfo;
    class Queue;
    class Geometry;

    class AabbsPass : public IRenderPassComponent
    {
    public:
        AabbsPass();

        ~AabbsPass();

        void Init() override;

        void UpdatePassInfo() override;

        void CreateUniforms(CommandBuffer *cmd) override;

        void UpdateDescriptorSets() override;

        void Update(Camera *camera) override;

        CommandBuffer *Draw() override;

        void Resize(uint32_t width, uint32_t height) override;

        void Destroy() override;

        void SetGeometry(Geometry *geometry) { m_geometry = geometry; }

        void SetCommandBuffer(CommandBuffer *cmd) { m_cmd = cmd; }

    private:
        friend class Scene;
        friend class Renderer;
        
        void UpdatePassInfoBrightFilter();

        void UpdatePassInfoGaussianBlurHorizontal();

        void UpdatePassInfoGaussianBlurVertical();

        void UpdatePassInfoCombine();

        PassInfo m_passInfo;
        Image *m_viewportRT;
        Image *m_depthRT;

        Queue *m_renderQueue;
        Geometry *m_geometry;
        CommandBuffer *m_cmd;
    };
}
