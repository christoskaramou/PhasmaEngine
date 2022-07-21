#pragma once

#include "Systems/CameraSystem.h"

namespace pe
{
    struct ShadowPushConstData
    {
        mat4 cascade;
        uint32_t meshIndex;
        uint32_t meshJointCount;
    };

    class Descriptor;
    class Image;
    class Buffer;
    class Pipeline;
    class RenderPass;

    class Shadows : public IRenderComponent
    {
    public:
        Shadows();

        ~Shadows();

        void Init() override;

        void UpdatePipelineInfo() override;

        void CreateUniforms(CommandBuffer *cmd) override;

        void UpdateDescriptorSets() override;

        void Update(Camera *camera) override;

        void Draw(CommandBuffer *cmd, uint32_t imageIndex) override;

        void Resize(uint32_t width, uint32_t height) override;

        void Destroy() override;

        void BeginPass(CommandBuffer *cmd, uint32_t imageIndex, uint32_t cascade);

        void EndPass(CommandBuffer *cmd, uint32_t cascade);

        RenderPass *GetRenderPassShadows() { return m_renderPassShadows; }

    private:
        void CalculateCascades(Camera *camera);

    public:
        mat4 cascades[SHADOWMAP_CASCADES];
        vec4 viewZ;
        std::vector<Image *> textures{};
        Descriptor *descriptorSetDeferred;
        Buffer *uniformBuffer;
        Format depthFormat;
        RenderPass *m_renderPassShadows;
    };
}
