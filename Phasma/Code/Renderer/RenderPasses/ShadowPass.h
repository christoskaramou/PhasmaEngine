#pragma once

#include "Renderer/Pipeline.h"
#include "Renderer/RenderPass.h"

namespace pe
{
    class Camera;
    class Image;
    class RenderPass;
    class PassInfo;
    class Geometry;
    class CommandBuffer;
    class Queue;
    class Sampler;
    class ModelGltf;

    class ShadowPass : public IRenderPassComponent
    {
    public:
        void Init() override;

        void UpdatePassInfo() override;

        void CreateUniforms(CommandBuffer *cmd) override;

        void UpdateDescriptorSets() override;

        void Update(Camera *camera) override;

        CommandBuffer *Draw() override;

        void Resize(uint32_t width, uint32_t height) override;

        void Destroy() override;

        void SetCommandBuffer(CommandBuffer *cmd) { m_cmd = cmd; }

        void SetGeometry(Geometry *geometry) { m_geometry = geometry; }

        void ClearDepths(CommandBuffer *cmd);

    private:
        friend class Geometry;
        friend class Scene;
        friend class LightPass;
        friend class Renderer;

        void DrawNode(CommandBuffer *cmd, ModelGltf &model, int node, uint32_t cascade);

        void CalculateCascades(Camera *camera);
        
        Buffer *m_uniforms[SWAPCHAIN_IMAGES];
        std::vector<mat4> m_cascades;
        vec4 m_viewZ;
        std::vector<Image *> m_textures{};
        Attachment m_attachment;
        Sampler *m_sampler;
        Geometry *m_geometry = nullptr;
        PassInfo m_passInfo;
        CommandBuffer *m_cmd = nullptr;
        Queue *m_renderQueue = nullptr;

    };
}
