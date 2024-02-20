#pragma once
#include "Renderer/Pipeline.h"
#include "Renderer/RenderPass.h"

namespace pe
{
    class Descriptor;
    class Image;
    class CommandBuffer;
    class Camera;
    class PassInfo;
    class Queue;

    class BloomPass : public IRenderPassComponent
    {
    public:
        BloomPass();

        ~BloomPass();

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
        
        void UpdatePassInfoBrightFilter();

        void UpdatePassInfoGaussianBlurHorizontal();

        void UpdatePassInfoGaussianBlurVertical();

        void UpdatePassInfoCombine();

        PassInfo m_passInfoBF;
        PassInfo m_passInfoGBH;
        PassInfo m_passInfoGBV;
        PassInfo m_passInfoCombine;
        Image *m_frameImage;
        Image *m_brightFilterRT;
        Image *m_gaussianBlurHorizontalRT;
        Image *m_gaussianBlurVerticalRT;
        Image *m_displayRT;
        Attachment m_attachmentBF;
        Attachment m_attachmentGBH;
        Attachment m_attachmentGBV;
        Attachment m_attachmentCombine;

        Queue *m_renderQueue;
    };
}
