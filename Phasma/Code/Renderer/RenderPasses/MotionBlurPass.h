#pragma once

#include "Systems/CameraSystem.h"
#include "Renderer/Pipeline.h"
#include "Renderer/RenderPass.h"

namespace pe
{
    class Descriptor;
    class Image;
    class Buffer;
    class Camera;
    class PassInfo;
    class Queue;

    class MotionBlurPass : public IRenderPassComponent
    {
    public:
        MotionBlurPass();

        ~MotionBlurPass();

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
        Attachment m_attachment;
        Image *m_frameImage;
        Image *m_displayRT;
        Image *m_velocityRT;
        Image *m_depth;
        Queue *m_renderQueue;
    };
}