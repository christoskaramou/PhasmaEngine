#pragma once

#include "Skybox/Skybox.h"
#include "Renderer/Shadows.h"

namespace pe
{
    class Descriptor;
    class Image;
    class Pipeline;
    class RenderPass;
    class PipelineCreateInfo;

    class Deferred : public IRenderComponent
    {
    public:
        Deferred();

        ~Deferred();

        void Init() override;

        void UpdatePipelineInfo() override;

        void CreateUniforms(CommandBuffer *cmd) override;

        void UpdateDescriptorSets() override;

        void Update(Camera *camera) override;

        void Draw(CommandBuffer *cmd, uint32_t imageIndex) override;

        void Resize(uint32_t width, uint32_t height) override;

        void Destroy() override;

        void BeginPass(CommandBuffer *cmd, uint32_t imageIndex);

        void EndPass(CommandBuffer *cmd);

        RenderPass *GetRenderPassModels() { return m_renderPassModels; }

    public:
        struct UBO
        {
            vec4 screenSpace[8];
        } ubo;
        Buffer *uniform;
        Descriptor *DSet;
        Pipeline *pipeline;
        RenderPass *m_renderPassModels;
        std::shared_ptr<PipelineCreateInfo> pipelineInfo;
        Image *ibl_brdf_lut;
        Image *normalRT;
        Image *albedoRT;
        Image *srmRT;
        Image *velocityRT;
        Image *emissiveRT;
        Image *viewportRT;
        Image *depth;
        Image *ssaoBlurRT;
        Image *ssrRT;
    };
}