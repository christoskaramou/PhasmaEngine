#pragma once

#include "Renderer/Pipeline.h"
#include "Renderer/RenderPass.h"

namespace pe
{
    class Descriptor;
    class Image;
    class Buffer;
    class Camera;
    class CommandBuffer;

    class SSRPass : public IRenderPassComponent
    {
    public:
        SSRPass();

        ~SSRPass();

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

        mat4 m_reflectionInput[4];
        Buffer *m_UBReflection[SWAPCHAIN_IMAGES];
        PassInfo m_passInfo;
        Image *m_ssrRT;
        Image *m_viewportRT;
        Image *m_normalRT;
        Image *m_depth;
        Image *m_srmRT;
        Image *m_albedoRT;
        Image *m_frameImage;
        Attachment m_attachment;
        Queue *m_renderQueue;
    };
}