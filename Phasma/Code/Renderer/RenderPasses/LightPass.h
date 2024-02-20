#pragma once

#include "Renderer/Pipeline.h"
#include "Renderer/RenderPass.h"

namespace pe
{
    class Descriptor;
    class Image;
    class RenderPass;
    class PassInfo;
    class DescriptorLayout;
    class CommandBuffer;
    class Queue;

    class LightPass : public IRenderPassComponent
    {
    public:
        LightPass();

        ~LightPass();

        void Init() override;

        void UpdatePassInfo() override;

        void CreateUniforms(CommandBuffer *cmd) override;

        void UpdateDescriptorSets() override;

        void Update(Camera *camera) override;

        CommandBuffer *Draw() override;

        void Resize(uint32_t width, uint32_t height) override;

        void Destroy() override;

        void SetBlendType(BlendType blendType) { m_blendType = blendType; }

        void SetCommandBuffer(CommandBuffer *cmd) { m_cmd = cmd; }

        PassInfo &GetPassInfoOpaque() { return m_passInfoOpaque; };

        PassInfo &GetPassInfoAlpha() { return m_passInfoTransparent; };

    private:
        friend class Renderer;

        void PassBarriers(CommandBuffer *cmd);
        
        struct UBO
        {
            mat4 invViewProj;
            uint32_t ssao;
            uint32_t ssr;
            uint32_t tonemapping;
            uint32_t fsr2;
            uint32_t IBL;
            float IBL_intensity;
            uint32_t volumetric;
            uint32_t volumetric_steps;
            float volumetric_dither_strength;
            float lightsIntensity;
            float lightsRange;
            uint32_t fog;
            float fogThickness;
            float fogMaxHeight;
            float fogGroundThickness;
            uint32_t shadows;
            float dummy;
        } m_ubo;
        Buffer *m_uniform[SWAPCHAIN_IMAGES];
        Image *m_ibl_brdf_lut;
        Image *m_normalRT;
        Image *m_albedoRT;
        Image *m_srmRT;
        Image *m_velocityRT;
        Image *m_emissiveRT;
        Image *m_viewportRT;
        Image *m_depthStencilRT;
        Image *m_ssaoRT;
        Image *m_transparencyRT;

        PassInfo m_passInfoOpaque;
        PassInfo m_passInfoTransparent;
        BlendType m_blendType = BlendType::None;
        Attachment m_attachmentOpaque;
        Attachment m_attachmentTransparent;

        Queue *m_renderQueue;
        CommandBuffer *m_cmd;
    };
}