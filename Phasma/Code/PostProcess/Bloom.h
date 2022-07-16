#pragma once

namespace pe
{
    class Descriptor;
    class Image;
    class CommandBuffer;
    class Pipeline;
    class Camera;
    class PipelineCreateInfo;

    class Bloom : public IRenderComponent
    {
    public:
        Bloom();

        ~Bloom();

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
        
        void CreateBrightFilterPipeline();

        void CreateGaussianBlurHorizontaPipeline();

        void CreateGaussianBlurVerticalPipeline();

        void CreateCombinePipeline();

    public:
        Pipeline *pipelineBrightFilter;
        Pipeline *pipelineGaussianBlurHorizontal;
        Pipeline *pipelineGaussianBlurVertical;
        Pipeline *pipelineCombine;
        std::shared_ptr<PipelineCreateInfo> pipelineInfoBF;
        std::shared_ptr<PipelineCreateInfo> pipelineInfoGBH;
        std::shared_ptr<PipelineCreateInfo> pipelineInfoGBV;
        std::shared_ptr<PipelineCreateInfo> pipelineInfoCombine;
        Descriptor *DSBrightFilter;
        Descriptor *DSGaussianBlurHorizontal;
        Descriptor *DSGaussianBlurVertical;
        Descriptor *DSCombine;
        Image *frameImage;
        Image *brightFilterRT;
        Image *gaussianBlurHorizontalRT;
        Image *gaussianBlurVerticalRT;
        Image *displayRT;
    };
}
