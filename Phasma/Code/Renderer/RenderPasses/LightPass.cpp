#if PE_VULKAN
#include "Renderer/RenderPasses/LightPass.h"
#include "Scene/Model.h"
#include "Renderer/Swapchain.h"
#include "Renderer/Surface.h"
#include "Shader/Shader.h"
#include "GUI/GUI.h"
#include "tinygltf/stb_image.h"
#include "Shader/Reflection.h"
#include "Renderer/RHI.h"
#include "Renderer/Queue.h"
#include "Renderer/Command.h"
#include "Renderer/RenderPass.h"
#include "Renderer/Descriptor.h"
#include "Systems/LightSystem.h"
#include "Renderer/Framebuffer.h"
#include "Renderer/RenderPass.h"
#include "Renderer/Image.h"
#include "Renderer/Buffer.h"
#include "Renderer/Pipeline.h"
#include "Renderer/Queue.h"
#include "Systems/RendererSystem.h"
#include "Renderer/RenderPasses/SSAOPass.h"
#include "Renderer/RenderPasses/SuperResolutionPass.h"
#include "Utilities/Downsampler.h"

namespace pe
{
    LightPass::LightPass()
    {
    }

    LightPass::~LightPass()
    {
    }

    void LightPass::Init()
    {
        m_cmd = nullptr;
        m_renderQueue = RHII.GetRenderQueue();
        RendererSystem *rs = CONTEXT->GetSystem<RendererSystem>();

        m_srmRT = rs->GetRenderTarget("srm"); // Specular Roughness Metallic
        m_normalRT = rs->GetRenderTarget("normal");
        m_albedoRT = rs->GetRenderTarget("albedo");
        m_velocityRT = rs->GetRenderTarget("velocity");
        m_emissiveRT = rs->GetRenderTarget("emissive");
        m_viewportRT = rs->GetRenderTarget("viewport");
        m_ssaoRT = rs->GetRenderTarget("ssao");
        m_depthStencilRT = rs->GetDepthStencilTarget("depthStencil");
        m_transparencyRT = rs->GetRenderTarget("transparency");

        m_attachmentOpaque = {};
        m_attachmentOpaque.image = m_viewportRT;
        m_attachmentOpaque.initialLayout = ImageLayout::Undefined;
        m_attachmentOpaque.finalLayout = ImageLayout::Attachment;
        m_attachmentOpaque.loadOp = AttachmentLoadOp::Clear;

        m_attachmentTransparent = {};
        m_attachmentTransparent.image = m_viewportRT;
        m_attachmentTransparent.initialLayout = ImageLayout::Attachment;
        m_attachmentTransparent.finalLayout = ImageLayout::ShaderReadOnly;
        m_attachmentTransparent.loadOp = AttachmentLoadOp::Load;
    }

    void LightPass::UpdatePassInfo()
    {
        ShadowPass &shadows = *WORLD_ENTITY->GetComponent<ShadowPass>();

        const std::vector<Define> definesFrag{
            Define{"SHADOWMAP_CASCADES", std::to_string(Settings::Get<Global>().shadowMapCascades)},
            Define{"SHADOWMAP_SIZE", std::to_string((float)Settings::Get<Global>().shadowMapSize)},
            Define{"SHADOWMAP_TEXEL_SIZE", std::to_string(1.0f / (float)Settings::Get<Global>().shadowMapSize)},
            Define{"MAX_POINT_LIGHTS", std::to_string(MAX_POINT_LIGHTS)},
            Define{"MAX_SPOT_LIGHTS", std::to_string(MAX_SPOT_LIGHTS)}};

        // Opaque light pass
        m_passInfoOpaque.name = "lighting_opaque_pipeline";
        m_passInfoOpaque.pVertShader = Shader::Create("Shaders/Common/Quad.hlsl", ShaderStage::VertexBit);
        m_passInfoOpaque.pFragShader = Shader::Create("Shaders/Gbuffer/LightingPS.hlsl", ShaderStage::FragmentBit, definesFrag);
        m_passInfoOpaque.dynamicStates = {DynamicState::Viewport, DynamicState::Scissor};
        m_passInfoOpaque.colorBlendAttachments = {m_viewportRT->GetBlendAttachment()};
        m_passInfoOpaque.colorFormats = m_viewportRT->GetFormatPtr();
        m_passInfoOpaque.depthTestEnable = false;
        m_passInfoOpaque.depthWriteEnable = false;
        m_passInfoOpaque.stencilTestEnable = false;
        m_passInfoOpaque.UpdateHash();

        // Transparent light pass
        m_passInfoTransparent.name = "lighting_transparent_pipeline";
        m_passInfoTransparent.pVertShader = Shader::Create("Shaders/Common/Quad.hlsl", ShaderStage::VertexBit);
        m_passInfoTransparent.pFragShader = Shader::Create("Shaders/Gbuffer/LightingPS.hlsl", ShaderStage::FragmentBit, definesFrag);
        m_passInfoTransparent.dynamicStates = {DynamicState::Viewport, DynamicState::Scissor};
        m_passInfoTransparent.colorBlendAttachments = {m_viewportRT->GetBlendAttachment()};
        m_passInfoTransparent.alphaBlend = true;
        m_passInfoTransparent.colorFormats = m_viewportRT->GetFormatPtr();
        m_passInfoTransparent.depthTestEnable = false;
        m_passInfoTransparent.depthWriteEnable = false;
        m_passInfoTransparent.UpdateHash();
    }

    void LightPass::CreateUniforms(CommandBuffer *cmd)
    {
        const std::string path = Path::Assets + "Objects/ibl_brdf_lut.png";
        m_ibl_brdf_lut = Image::LoadRGBA8(cmd, path);

        for (uint32_t i = 0; i < SWAPCHAIN_IMAGES; i++)
        {
            m_uniform[i] = Buffer::Create(
                RHII.AlignUniform(sizeof(UBO)),
                BufferUsage::UniformBufferBit,
                AllocationCreate::HostAccessSequentialWriteBit,
                "Gbuffer_uniform_buffer");
            m_uniform[i]->Map();
            m_uniform[i]->Zero();
            m_uniform[i]->Flush();
            m_uniform[i]->Unmap();
        }

        UpdateDescriptorSets();
    }

    void LightPass::UpdateDescriptorSets()
    {
        ShadowPass &shadows = *WORLD_ENTITY->GetComponent<ShadowPass>();
        std::vector<ImageViewHandle> views(shadows.m_textures.size());
        for (uint32_t i = 0; i < shadows.m_textures.size(); i++)
            views[i] = shadows.m_textures[i]->GetSRV();

        const SkyBox &skybox = GUI::shadow_cast ? CONTEXT->GetSystem<RendererSystem>()->GetSkyBoxDay() : CONTEXT->GetSystem<RendererSystem>()->GetSkyBoxNight();

        for (uint32_t i = 0; i < SWAPCHAIN_IMAGES; i++)
        {
            // Opaque
            auto &setsOpaque = m_passInfoOpaque.GetDescriptors(i);

            auto *DSetOpaque = setsOpaque[0];
            DSetOpaque->SetImageView(0, m_depthStencilRT->GetSRV(), m_depthStencilRT->GetSampler()->Handle());
            DSetOpaque->SetImageView(1, m_normalRT->GetSRV(), m_normalRT->GetSampler()->Handle());
            DSetOpaque->SetImageView(2, m_albedoRT->GetSRV(), m_albedoRT->GetSampler()->Handle());
            DSetOpaque->SetImageView(3, m_srmRT->GetSRV(), m_srmRT->GetSampler()->Handle());
            DSetOpaque->SetBuffer(4, CONTEXT->GetSystem<LightSystem>()->GetUniform(i));
            DSetOpaque->SetImageView(5, m_ssaoRT->GetSRV(), m_ssaoRT->GetSampler()->Handle());
            DSetOpaque->SetImageView(6, m_emissiveRT->GetSRV(), m_emissiveRT->GetSampler()->Handle());
            DSetOpaque->SetBuffer(7, m_uniform[i]);
            DSetOpaque->SetImageView(8, m_transparencyRT->GetSRV(), m_transparencyRT->GetSampler()->Handle());
            DSetOpaque->SetImageView(9, m_ibl_brdf_lut->GetSRV(), m_ibl_brdf_lut->GetSampler()->Handle());
            DSetOpaque->Update();

            auto *DSetShadowsOpaque = setsOpaque[1];
            DSetShadowsOpaque->SetBuffer(0, shadows.m_uniforms[i]);
            DSetShadowsOpaque->SetImageViews(1, views, {});
            DSetShadowsOpaque->SetSampler(2, shadows.m_sampler->Handle());
            DSetShadowsOpaque->Update();

            auto *DSetSkyboxOpaque = setsOpaque[2];
            DSetSkyboxOpaque->SetImageView(0, skybox.GetCubeMap()->GetSRV(), skybox.GetCubeMap()->GetSampler()->Handle());
            DSetSkyboxOpaque->Update();

            // Alpha
            auto &setsAlpha = m_passInfoTransparent.GetDescriptors(i);

            auto *DSetAlpha = setsAlpha[0];
            DSetAlpha->SetImageView(0, m_depthStencilRT->GetSRV(), m_depthStencilRT->GetSampler()->Handle());
            DSetAlpha->SetImageView(1, m_normalRT->GetSRV(), m_normalRT->GetSampler()->Handle());
            DSetAlpha->SetImageView(2, m_albedoRT->GetSRV(), m_albedoRT->GetSampler()->Handle());
            DSetAlpha->SetImageView(3, m_srmRT->GetSRV(), m_srmRT->GetSampler()->Handle());
            DSetAlpha->SetBuffer(4, CONTEXT->GetSystem<LightSystem>()->GetUniform(i));
            DSetAlpha->SetImageView(5, m_ssaoRT->GetSRV(), m_ssaoRT->GetSampler()->Handle());
            DSetAlpha->SetImageView(6, m_emissiveRT->GetSRV(), m_emissiveRT->GetSampler()->Handle());
            DSetAlpha->SetBuffer(7, m_uniform[i]);
            DSetAlpha->SetImageView(8, m_transparencyRT->GetSRV(), m_transparencyRT->GetSampler()->Handle());
            DSetAlpha->SetImageView(9, m_ibl_brdf_lut->GetSRV(), m_ibl_brdf_lut->GetSampler()->Handle());
            DSetAlpha->Update();

            auto *DSetShadowsAlpha = setsAlpha[1];
            DSetShadowsAlpha->SetBuffer(0, shadows.m_uniforms[i]);
            DSetShadowsAlpha->SetImageViews(1, views, {});
            DSetShadowsAlpha->SetSampler(2, shadows.m_sampler->Handle());
            DSetShadowsAlpha->Update();

            auto *DSetSkyboxAlpha = setsAlpha[2];
            DSetSkyboxAlpha->SetImageView(0, skybox.GetCubeMap()->GetSRV(), skybox.GetCubeMap()->GetSampler()->Handle());
            DSetSkyboxAlpha->Update();
        }
    }

    void LightPass::Update(Camera *camera)
    {
        m_ubo = {};
        m_ubo.invViewProj = camera->GetInvViewProjection();
        m_ubo.ssao = GUI::show_ssao;
        m_ubo.ssr = GUI::show_ssr;
        m_ubo.tonemapping = GUI::show_tonemapping;
        m_ubo.fsr2 = Settings::Get<SRSettings>().enable;
        m_ubo.IBL = GUI::use_IBL;
        m_ubo.IBL_intensity = GUI::IBL_intensity;
        m_ubo.volumetric = GUI::use_Volumetric_lights;
        m_ubo.volumetric_steps = GUI::volumetric_steps;
        m_ubo.volumetric_dither_strength = GUI::volumetric_dither_strength;
        m_ubo.lightsIntensity = GUI::lights_intensity;
        m_ubo.lightsRange = GUI::lights_range;
        m_ubo.fog = GUI::use_fog;
        m_ubo.fogThickness = GUI::fog_global_thickness;
        m_ubo.fogMaxHeight = GUI::fog_max_height;
        m_ubo.fogGroundThickness = GUI::fog_ground_thickness;
        m_ubo.shadows = GUI::shadow_cast;

        MemoryRange mr{};
        mr.data = &m_ubo;
        mr.size = sizeof(UBO);
        mr.offset = 0;
        m_uniform[RHII.GetFrameIndex()]->Copy(1, &mr, false);
    }

    void LightPass::PassBarriers(CommandBuffer *cmd)
    {
        RendererSystem &rs = *CONTEXT->GetSystem<RendererSystem>();
        const SkyBox &skybox = GUI::shadow_cast ? rs.GetSkyBoxDay() : rs.GetSkyBoxNight();
        ShadowPass &shadows = *WORLD_ENTITY->GetComponent<ShadowPass>();

        std::vector<ImageBarrierInfo> barriers(9 + shadows.m_textures.size());
        for (size_t i = 0; i < barriers.size(); i++)
        {
            barriers[i].layout = ImageLayout::ShaderReadOnly;
            barriers[i].stageFlags = PipelineStage::FragmentShaderBit;
            barriers[i].accessMask = Access::ShaderReadBit;
        }

        barriers[0].image = m_depthStencilRT;
        barriers[1].image = m_normalRT;
        barriers[2].image = m_albedoRT;
        barriers[3].image = m_srmRT;
        barriers[4].image = m_velocityRT;
        barriers[5].image = m_emissiveRT;
        barriers[6].image = m_ssaoRT;
        barriers[7].image = m_transparencyRT;

        barriers[8].image = m_viewportRT;
        barriers[8].layout = ImageLayout::Attachment;
        barriers[8].stageFlags = PipelineStage::ColorAttachmentOutputBit;
        barriers[8].accessMask = Access::ColorAttachmentWriteBit;

        for (uint32_t i = 0; i < shadows.m_textures.size(); i++)
        {
            barriers[i + 9].image = shadows.m_textures[i];
        }

        cmd->ImageBarriers(barriers);
    }

    CommandBuffer *LightPass::Draw()
    {
        PE_ERROR_IF(m_blendType == BlendType::None, "BlendType is None");

        CommandBuffer *cmd;
        if (!m_cmd)
        {
            cmd = CommandBuffer::GetFree(m_renderQueue->GetFamilyId());
            cmd->Begin();
        }
        else
        {
            cmd = m_cmd;
        }

        RendererSystem &rs = *CONTEXT->GetSystem<RendererSystem>();
        const SkyBox &skybox = GUI::shadow_cast ? rs.GetSkyBoxDay() : rs.GetSkyBoxNight();
        ShadowPass &shadows = *WORLD_ENTITY->GetComponent<ShadowPass>();

        bool isTransparent = m_blendType == BlendType::Transparent;
        Attachment &attachment = isTransparent ? m_attachmentTransparent : m_attachmentOpaque;
        PassInfo &passInfo = isTransparent ? m_passInfoTransparent : m_passInfoOpaque;
        std::string passName = isTransparent ? "Transparent" : "Opaque";

        cmd->BeginDebugRegion("LightPass");
        cmd->SetConstant(0, GUI::shadow_cast ? 1.0f : 0.0f); // Shadow cast
        cmd->SetConstant(1, MAX_POINT_LIGHTS);               // num point lights
        cmd->SetConstant(2, m_viewportRT->GetWidth_f());       // framebuffer width
        cmd->SetConstant(3, m_viewportRT->GetHeight_f());      // framebuffer height
        cmd->SetConstant(4, isTransparent ? 1u : 0u);        // transparent pass
        for (uint32_t i = 0; i < Settings::Get<Global>().shadowMapCascades; i++)
            cmd->SetConstant(i + 5, shadows.m_viewZ[i]);     // shadowmap cascade distances

        PassBarriers(cmd);

        cmd->BeginPass({attachment}, passName);
        cmd->BindPipeline(passInfo);
        cmd->SetViewport(0.f, 0.f, m_viewportRT->GetWidth_f(), m_viewportRT->GetHeight_f());
        cmd->SetScissor(0, 0, m_viewportRT->GetWidth(), m_viewportRT->GetHeight());
        cmd->PushConstants();
        cmd->Draw(3, 1, 0, 0);
        cmd->EndPass();

        cmd->EndDebugRegion();

        m_blendType = BlendType::None;

        if (!m_cmd)
            cmd->End();
        m_cmd = nullptr;

        return cmd;
    }

    void LightPass::Resize(uint32_t width, uint32_t height)
    {
        Init();
        UpdateDescriptorSets();
    }

    void LightPass::Destroy()
    {
        for (uint32_t i = 0; i < SWAPCHAIN_IMAGES; i++)
            Buffer::Destroy(m_uniform[i]);
    }
}
#endif
