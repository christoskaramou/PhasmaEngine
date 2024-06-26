#pragma once

#include "Renderer/Pipeline.h"
#include "Renderer/RenderPass.h"

namespace pe
{
    class Descriptor;
    class Image;
    class CommandBuffer;
    class Camera;
    class Queue;

    class FXAAPass : public IRenderPassComponent
    {
    public:
        FXAAPass();

        ~FXAAPass();
        
        void Init() override;

        void UpdatePassInfo() override;

        void CreateUniforms(CommandBuffer *cmd) override;

        void UpdateDescriptorSets() override;

        void Update(Camera *camera) override;

        CommandBuffer *Draw() override;

        void Resize(uint32_t width, uint32_t height) override;

        void Destroy() override;

    private:
        friend class Renderer;

        PassInfo m_passInfo;
        Image *m_frameImage;
        Image *m_viewportRT;
        Attachment m_attachment;
        Queue *m_renderQueue;
    };
}