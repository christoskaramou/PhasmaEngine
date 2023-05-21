#pragma once

#include "Skybox/Skybox.h"
#include "Renderer/Shadows.h"

namespace pe
{
    class Descriptor;
    class Image;
    class RenderPass;
    class PassInfo;
    class DescriptorLayout;

    class Deferred : public IRenderComponent
    {
    public:
        Deferred();

        ~Deferred();

        void Init() override;

        void UpdatePassInfo() override;

        void UpdatePassInfoGBuffer();

        void UpdatePassInfoAABBs();

        void CreateUniforms(CommandBuffer *cmd) override;

        void UpdateDescriptorSets() override;

        void Update(Camera *camera) override;

        void Draw(CommandBuffer *cmd, uint32_t imageIndex) override;

        void Resize(uint32_t width, uint32_t height) override;

        void Destroy() override;

        void BeginPass(CommandBuffer *cmd, uint32_t imageIndex);

        void EndPass(CommandBuffer *cmd);

        const PassInfo &GetGBufferPassInfo() { return *passInfoGBuffer; }
        const PassInfo &GetAABBsPassInfo() { return *passInfoAABBs; }

    public:
        struct UBO
        {
            vec4 screenSpace[8];
        } ubo;
        Buffer *uniform;
        Descriptor *DSet;
        DescriptorLayout *dlBuffer;
        DescriptorLayout *dlImages;
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

        std::shared_ptr<PassInfo> passInfo;
        std::shared_ptr<PassInfo> passInfoGBuffer;
        std::shared_ptr<PassInfo> passInfoAABBs;
    };
}