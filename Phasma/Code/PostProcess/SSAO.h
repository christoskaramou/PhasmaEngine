#pragma once

#include "CACAO/ffx_cacao.h"
#include "CACAO/ffx_cacao_impl.h"

namespace pe
{
    class CommandBuffer;
    class Descriptor;
    class Image;
    class Pipeline;
    class Buffer;
    class Camera;

    class SSAO : public IRenderComponent
    {
    public:
        SSAO();

        ~SSAO();

        void Init() override;

        void UpdatePipelineInfo() override{};

        void CreateUniforms(CommandBuffer *cmd) override{};

        void UpdateDescriptorSets() override{};

        void Update(Camera *camera) override;

        void Draw(CommandBuffer *cmd, uint32_t imageIndex) override;

        void Resize(uint32_t width, uint32_t height) override;

        void Destroy() override;

        Image *ssaoRT;
        Image *ssaoBlurRT;
        Image *normalRT;
        Image *depth;
        FFX_CACAO_VkContext *m_context;
        FFX_CACAO_Matrix4x4 m_proj;/* row major projection matrix */
        FFX_CACAO_Matrix4x4 m_normalsToView;/* row major matrix to convert normals to viewspace */
    };
}