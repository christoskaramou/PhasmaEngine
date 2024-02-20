#pragma once

#include "CACAO/ffx_cacao.h"
#include "CACAO/ffx_cacao_impl.h"

namespace pe
{
    class CommandBuffer;
    class Descriptor;
    class Image;
    class Camera;
    class Queue;

    class SSAOPass : public IRenderPassComponent
    {
    public:
        SSAOPass();

        ~SSAOPass();

        void Init() override;

        void UpdatePassInfo() override{};

        void CreateUniforms(CommandBuffer *cmd) override{};

        void UpdateDescriptorSets() override{};

        void Update(Camera *camera) override;

        CommandBuffer *Draw() override;

        void Resize(uint32_t width, uint32_t height) override;

        void Destroy() override;

    private:
        Image *m_ssaoRT;
        Image *m_normalRT;
        Image *m_depth;
        FFX_CACAO_VkContext *m_context;
        FFX_CACAO_Matrix4x4 m_proj;          /* row major projection matrix */
        FFX_CACAO_Matrix4x4 m_normalsToView; /* row major matrix to convert normals to viewspace */
        Queue *m_queue;
        CommandBuffer *m_previousCmd = nullptr;
    };
}