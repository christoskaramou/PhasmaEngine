#pragma once

#include "Systems/CameraSystem.h"

namespace pe
{
    class Descriptor;
    class Image;
    class Buffer;
    class Camera;
    class PassInfo;

    class MotionBlur : public IRenderComponent
    {
    public:
        MotionBlur();

        ~MotionBlur();

        void Init() override;

        void UpdatePassInfo() override;

        void CreateUniforms(CommandBuffer *cmd) override;

        void UpdateDescriptorSets() override;

        void Update(Camera *camera) override;

        void Draw(CommandBuffer *cmd, uint32_t imageIndex) override;

        void Resize(uint32_t width, uint32_t height) override;

        void Destroy() override;

        std::shared_ptr<PassInfo> passInfo;
        Descriptor *DSet;
        Image *frameImage;
        Image *displayRT;
        Image *velocityRT;
        Image *depth;
        
        vec4 pushConstData;
    };
}