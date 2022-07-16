#pragma once

namespace pe
{
    class CommandBuffer;
    class Descriptor;
    class Image;
    class Pipeline;
    class Buffer;
    class Camera;
    class PipelineCreateInfo;

    class SSAO : public IRenderComponent
    {
    public:
        SSAO();

        ~SSAO();

        void Init() override;

        void CreatePipeline() override;

        void CreateUniforms(CommandBuffer *cmd) override;

        void UpdateDescriptorSets() override;

        void Update(Camera *camera) override;

        void Draw(CommandBuffer *cmd, uint32_t imageIndex) override;

        void Resize(uint32_t width, uint32_t height) override;

        void Destroy() override;

    private:
        friend class Renderer;
        
        void CreateSSAOFrameBuffers();

        void CreateSSAOBlurFrameBuffers();

        void CreateSSAOPipeline();

        void CreateBlurPipeline();

    public:
        struct UBO
        {
            mat4 projection;
            mat4 view;
            mat4 invProjection;
        }ubo;
        Buffer *UB_Kernel;
        Buffer *UB_PVM;
        Image *noiseTex;
        Pipeline *pipeline;
        Pipeline *pipelineBlur;
        std::shared_ptr<PipelineCreateInfo> pipelineInfo;
        std::shared_ptr<PipelineCreateInfo> pipelineInfoBlur;
        Descriptor *DSet;
        Descriptor *DSBlur;
        Image *ssaoRT;
        Image *ssaoBlurRT;
        Image *normalRT;
        Image *depth;
    };
}