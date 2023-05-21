#pragma once

namespace pe
{
    class Descriptor;
    class Image;
    class CommandBuffer;
    class Camera;
    class PassInfo;

    class Bloom : public IRenderComponent
    {
    public:
        Bloom();

        ~Bloom();

        void Init() override;

        void UpdatePassInfo() override;

        void CreateUniforms(CommandBuffer *cmd) override;

        void UpdateDescriptorSets() override;

        void Update(Camera *camera) override;

        void Draw(CommandBuffer *cmd, uint32_t imageIndex) override;

        void Resize(uint32_t width, uint32_t height) override;

        void Destroy() override;

    private:
        friend class Renderer;
        
        void UpdatePassInfoBrightFilter();

        void UpdatePassInfoGaussianBlurHorizontal();

        void UpdatePassInfoGaussianBlurVertical();

        void UpdatePassInfoCombine();

    public:
        std::shared_ptr<PassInfo> passInfoBF;
        std::shared_ptr<PassInfo> passInfoGBH;
        std::shared_ptr<PassInfo> passInfoGBV;
        std::shared_ptr<PassInfo> passInfoCombine;
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
