#include "ColorGradingPass.h"
#include "API/Command.h"
#include "API/Descriptor.h"
#include "API/Image.h"
#include "API/Pipeline.h"
#include "API/Queue.h"
#include "API/RHI.h"
#include "API/Shader.h"
#include "Render/SceneRendererHost.h"

namespace pe
{
    // Must match PushConstants_ColorGrading in Phasma/Runtime/RuntimeAssets/Shaders/Common/Structures.hlsl.
    // Each color triplet uses a 16-byte lane to match the HLSL float4 fields on Vulkan and DX12.
    struct ColorGradingPushConstants
    {
        float lift[4];
        float gamma[4];
        float gain[4];
        float saturation;
        float contrast;
        float intensity;
        float blend;
        float useMask;
        float pad[3];
    };
    static_assert(sizeof(ColorGradingPushConstants) == 80, "ColorGradingPushConstants must match HLSL PushConstants_ColorGrading");

    void ColorGradingPass::Init()
    {
        SceneRendererHost *rs = &RequireActiveSceneRendererHost();

        m_displayRT = rs->GetRenderTarget("display");
        m_frameImage = rs->CreateFSSampledImage(false);

        m_attachments.resize(1);
        m_attachments[0] = {};
        m_attachments[0].image = m_displayRT;
    }

    void ColorGradingPass::UpdatePassInfo()
    {
        m_passInfo->name = "color_grading_pipeline";
        m_passInfo->pVertShader = Shader::Create({.sourcePath = Path::RuntimeAssets + "Shaders/Common/Quad.hlsl",
                                                  .entryPoint = "mainVS",
                                                  .stage = PE_SHADER_STAGE_VERTEX,
                                                  .defines = std::vector<Define>{}});
        m_passInfo->pFragShader = Shader::Create({.sourcePath = Path::RuntimeAssets + "Shaders/ColorGrading/ColorGradingPS.hlsl",
                                                  .entryPoint = "mainPS",
                                                  .stage = PE_SHADER_STAGE_FRAGMENT,
                                                  .defines = std::vector<Define>{}});
        m_passInfo->dynamicStates = {PE_DYNAMIC_STATE_VIEWPORT, PE_DYNAMIC_STATE_SCISSOR};
        m_passInfo->cullMode = PE_CULL_MODE_NONE;
        m_passInfo->colorBlendAttachments = {BlendState::Default};
        m_passInfo->colorFormats = {m_displayRT->GetFormat()};
        m_passInfo->depthTestEnable = false;
        m_passInfo->depthWriteEnable = false;
        m_passInfo->stencilTestEnable = false;
        m_passInfo->Update();
    }

    void ColorGradingPass::CreateUniforms(CommandBuffer *cmd)
    {
        UpdateDescriptorSets();
    }

    void ColorGradingPass::UpdateDescriptorSets()
    {
        // Binding 1 must always be valid; before a mask loads, the frame image stands in
        // (never sampled into the result while use_mask is 0).
        Image *mask = m_maskImage ? m_maskImage : m_frameImage;
        for (uint32_t i = 0; i < RHII.GetSwapchainImageCount(); i++)
        {
            auto *DSet = m_passInfo->GetDescriptors(i)[0];
            DSet->SetImageView(0, m_frameImage->GetSRV(), m_frameImage->GetSampler());
            DSet->SetImageView(1, mask->GetSRV(), mask->GetSampler());
            DSet->Update();
        }
    }

    void ColorGradingPass::Update()
    {
        // A cleared path keeps the cached mask bound (use_mask just drops to 0), so scripts can
        // pulse a mask on/off per frame; only a different non-empty path pays a real load.
        const std::string &path = ActivePostProcessProfile().color_grading_mask;
        if (!path.empty() && path != m_maskPath)
            LoadMask(path);
    }

    void ColorGradingPass::LoadMask(const std::string &path)
    {
        m_maskPath = path;

        Queue *queue = RHII.GetMainQueue();
        if (!queue)
            return;

        CommandBuffer *cmd = queue->AcquireCommandBuffer();
        cmd->Begin();
        // ponytail: grayscale PNG decoded as RGBA8, shader reads .r; switch to an R8 LoadRaw path
        // if mask VRAM ever matters.
        Image *mask = Image::LoadRGBA8(cmd, Path::ResolveAsset(path));
        cmd->End();
        if (mask)
        {
            queue->Submit(1, &cmd, nullptr, nullptr);
            cmd->Wait();
        }
        else
        {
            PE_WARN("[ColorGrading] strength mask failed to load: %s", path.c_str());
        }
        cmd->Return();

        // Rare (a new mask path, usually once per scene): drain the GPU so no in-flight frame
        // still references the old binding, then rewrite all descriptor sets.
        RHII.WaitDeviceIdle();
        Image::Destroy(m_maskImage);
        m_maskImage = mask;
        UpdateDescriptorSets();
    }

    void ColorGradingPass::ExecutePass(CommandBuffer *cmd)
    {
        auto &gSettings = ActivePostProcessProfile();

        ImageBarrierInfo barrier{};
        barrier.image = m_frameImage;
        barrier.layout = PE_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.stageFlags = PE_STAGE_FRAGMENT_SHADER;
        barrier.accessMask = PE_ACCESS_SHADER_SAMPLED_READ;

        cmd->BeginDebugRegion("ColorGradingPass");
        cmd->CopyImage(m_displayRT, m_frameImage);
        cmd->ImageBarrier(barrier);

        ColorGradingPushConstants pc{};
        pc.lift[0] = gSettings.color_grading_lift_r;
        pc.lift[1] = gSettings.color_grading_lift_g;
        pc.lift[2] = gSettings.color_grading_lift_b;
        pc.lift[3] = 0.0f;
        pc.gamma[0] = gSettings.color_grading_gamma_r;
        pc.gamma[1] = gSettings.color_grading_gamma_g;
        pc.gamma[2] = gSettings.color_grading_gamma_b;
        pc.gamma[3] = 1.0f;
        pc.gain[0] = gSettings.color_grading_gain_r;
        pc.gain[1] = gSettings.color_grading_gain_g;
        pc.gain[2] = gSettings.color_grading_gain_b;
        pc.gain[3] = 1.0f;
        pc.saturation = gSettings.color_grading_saturation;
        pc.contrast = gSettings.color_grading_contrast;
        pc.intensity = gSettings.color_grading_intensity;
        pc.blend = ActivePostProcessBlend().color_grading;
        pc.useMask = m_maskImage && !gSettings.color_grading_mask.empty() ? 1.0f : 0.0f;

        cmd->BeginPass(1, m_attachments.data(), "ColorGrading");
        cmd->BindPipeline(*m_passInfo);
        cmd->SetViewport(0.f, 0.f, m_displayRT->GetWidth_f(), m_displayRT->GetHeight_f());
        cmd->SetScissor(0, 0, m_displayRT->GetWidth(), m_displayRT->GetHeight());
        cmd->SetConstants(pc);
        cmd->PushConstants();
        cmd->Draw(3, 1, 0, 0);
        cmd->EndPass();

        cmd->EndDebugRegion();
    }

    void ColorGradingPass::Resize(uint32_t width, uint32_t height)
    {
        Image::Destroy(m_frameImage);
        Init();
        UpdateDescriptorSets();
    }

    void ColorGradingPass::Destroy()
    {
        Image::Destroy(m_frameImage);
        Image::Destroy(m_maskImage);
        m_maskPath.clear();
    }
} // namespace pe
