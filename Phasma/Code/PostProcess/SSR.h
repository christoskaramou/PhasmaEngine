#pragma once

namespace pe
{
    class Descriptor;
    class Image;
    class Buffer;
    class Camera;
    class PassInfo;

    class SSR : public IRenderComponent
    {
    public:
        SSR();

        ~SSR();

        void Init() override;

        void UpdatePassInfo() override;

        void CreateUniforms(CommandBuffer *cmd) override;

        void UpdateDescriptorSets() override;

        void Update(Camera *camera) override;

        void Draw(CommandBuffer *cmd, uint32_t imageIndex) override;

        void Resize(uint32_t width, uint32_t height) override;

        void Destroy() override;

        mat4 reflectionInput[4];
        Buffer *UBReflection;
        std::shared_ptr<PassInfo> passInfo;
        Descriptor *DSet;
        Image *ssrRT;
        Image *albedoRT;
        Image *normalRT;
        Image *depth;
    };
}