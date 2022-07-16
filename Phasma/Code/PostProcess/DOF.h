#pragma once

namespace pe
{
    class Descriptor;
    class Image;
    class CommandBuffer;
    class Pipeline;
    class Camera;
    class PipelineCreateInfo;

    class DOF : public IRenderComponent
    {
    public:
        DOF();

        ~DOF();

        void Init() override;

        void CreatePipeline() override;

        void CreateUniforms(CommandBuffer *cmd) override;

        void UpdateDescriptorSets() override;

        void Update(Camera *camera) override;

        void Draw(CommandBuffer *cmd, uint32_t imageIndex) override;

        void Resize(uint32_t width, uint32_t height) override;

        void Destroy() override;

        Pipeline *pipeline;
        std::shared_ptr<PipelineCreateInfo> pipelineInfo;
        Descriptor *DSet;
        Image *frameImage;
        Image *displayRT;
        Image *depth;
    };
}
