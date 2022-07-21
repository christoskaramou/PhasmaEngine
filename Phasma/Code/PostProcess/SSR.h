#pragma once

namespace pe
{
    class Descriptor;
    class Image;
    class Pipeline;
    class Buffer;
    class Camera;
    class PipelineCreateInfo;

    class SSR : public IRenderComponent
    {
    public:
        SSR();

        ~SSR();

        void Init() override;

        void UpdatePipelineInfo() override;

        void CreateUniforms(CommandBuffer *cmd) override;

        void UpdateDescriptorSets() override;

        void Update(Camera *camera) override;

        void Draw(CommandBuffer *cmd, uint32_t imageIndex) override;

        void Resize(uint32_t width, uint32_t height) override;

        void Destroy() override;

        mat4 reflectionInput[4];
        Buffer *UBReflection;
        Pipeline *pipeline;
        std::shared_ptr<PipelineCreateInfo> pipelineInfo;
        Descriptor *DSet;
        Image *ssrRT;
        Image *albedoRT;
        Image *normalRT;
        Image *srmRT;
        Image *depth;
    };
}