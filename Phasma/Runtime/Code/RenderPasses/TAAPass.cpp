#include "TAAPass.h"
#include "API/Command.h"
#include "API/Descriptor.h"
#include "API/Image.h"
#include "API/Pipeline.h"
#include "API/Queue.h"
#include "API/RHI.h"
#include "API/RenderGraph.h"
#include "API/Shader.h"
#include "Render/SceneRendererHost.h"

namespace pe
{
    float Halton(int index, int base)
    {
        float f = 1;
        float r = 0;
        int current = index;
        while (current > 0)
        {
            f = f / base;
            r = r + f * (current % base);
            current = floor(current / base);
        }
        return r;
    }

    void TAAPass::Init()
    {
        SceneRendererHost *rs = &RequireActiveSceneRendererHost();

        m_displayRT = rs->GetDisplayRT();
        m_viewportRT = rs->GetViewportRT(); // Represents current input color
        m_depthStencil = rs->GetDepthStencilRT();
        m_velocityRT = rs->GetRenderTarget("velocity");

        // Create history image if not exists
        if (!m_historyImage)
        {
            ImageDesc desc{};
            desc.format = m_displayRT->GetFormat();
            desc.width = m_displayRT->GetWidth();
            desc.height = m_displayRT->GetHeight();
            desc.usage = PE_IMAGE_USAGE_SAMPLED | PE_IMAGE_USAGE_STORAGE | PE_IMAGE_USAGE_TRANSFER_DST | PE_IMAGE_USAGE_TRANSFER_SRC | PE_IMAGE_USAGE_COLOR_ATTACHMENT;
            desc.name = "TAA_History";
            m_historyImage = Image::Create(desc);
            m_historyImage->CreateRTV();
            m_historyImage->CreateSRV(PE_IMAGE_VIEW_TYPE_2D);

            // Linear sampler for history
            SamplerDesc samplerInfo = Sampler::CreateInfoInit();
            samplerInfo.magFilter = PE_FILTER_LINEAR;
            samplerInfo.minFilter = PE_FILTER_LINEAR;
            samplerInfo.addressModeU = PE_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.addressModeV = PE_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            Sampler *sampler = Sampler::Create(samplerInfo, "TAA_History_Linear");
            m_historyImage->SetSampler(sampler);
            m_resetHistory = true;
        }

        if (!m_taaResolved)
        {
            ImageDesc desc{};
            desc.format = m_displayRT->GetFormat();
            desc.width = m_displayRT->GetWidth();
            desc.height = m_displayRT->GetHeight();
            desc.usage = PE_IMAGE_USAGE_SAMPLED | PE_IMAGE_USAGE_STORAGE | PE_IMAGE_USAGE_TRANSFER_SRC | PE_IMAGE_USAGE_TRANSFER_DST | PE_IMAGE_USAGE_COLOR_ATTACHMENT;
            desc.name = "TAA_Resolved";
            m_taaResolved = Image::Create(desc);
            m_taaResolved->CreateRTV();
            m_taaResolved->CreateSRV(PE_IMAGE_VIEW_TYPE_2D);
            m_taaResolved->CreateUAV(PE_IMAGE_VIEW_TYPE_2D, 0);
        }

        m_jitterIndex = 0;
        m_jitterPhaseCount = 16;
        m_casSharpeningEnabled = ActivePostProcessProfile().cas_sharpening;
    }

    void TAAPass::UpdatePassInfo()
    {
        m_passInfo->name = "TAA_pipeline";
        m_passInfo->pCompShader = Shader::Create({.sourcePath = Path::RuntimeAssets + "Shaders/TAA/TAA.hlsl", .entryPoint = "main", .stage = PE_SHADER_STAGE_COMPUTE, .defines = std::vector<Define>{}});
        m_passInfo->Update();
    }

    void TAAPass::CreateUniforms(CommandBuffer *cmd)
    {
        UpdateDescriptorSets();
    }

    void TAAPass::UpdateDescriptorSets()
    {
        auto &gSettings = ActivePostProcessProfile();
        Image *taaOutput = gSettings.cas_sharpening ? m_taaResolved : m_displayRT;
        if (!m_viewportRT || !m_historyImage || !m_velocityRT || !taaOutput)
            return;

        for (uint32_t i = 0; i < RHII.GetSwapchainImageCount(); i++)
        {
            auto &descriptors = m_passInfo->GetDescriptors(i);
            if (!descriptors.empty())
            {
                Descriptor *dset = descriptors[0];
                dset->SetImageView(0, m_viewportRT->GetSRV());
                dset->SetImageView(1, m_historyImage->GetSRV());
                dset->SetImageView(2, m_velocityRT->GetSRV());
                // dset->SetImageView(3, m_depthStencil->GetSRV()); // Depth unused
                dset->SetSampler(4, m_historyImage->GetSampler());
                dset->SetImageView(5, taaOutput->GetUAV(0));
                dset->Update();
            }
        }
    }

    void TAAPass::Update()
    {
        auto &gSettings = ActivePostProcessProfile();
        if (gSettings.cas_sharpening != m_casSharpeningEnabled)
        {
            m_casSharpeningEnabled = gSettings.cas_sharpening;
            UpdateDescriptorSets();
        }
    }

    void TAAPass::DeclareInputs(RGBuilder &builder)
    {
        Image *taaOutput = m_casSharpeningEnabled ? m_taaResolved : m_displayRT;
        builder.ReadCompute(m_viewportRT);
        if (!m_resetHistory)
            builder.ReadCompute(m_historyImage);
        builder.ReadCompute(m_velocityRT);
        builder.WriteCompute(taaOutput);
    }

    void TAAPass::DeclareOutputs(RGBuilder &builder)
    {
        Image *taaOutput = m_casSharpeningEnabled ? m_taaResolved : m_displayRT;

        // ExecutePass transitions taaOutput/history for the copy operation and leaves
        // them in transfer layouts, so declare those as the final tracked states.
        builder.OutputCustom(taaOutput,
                             PE_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                             PE_STAGE_TRANSFER,
                             PE_ACCESS_TRANSFER_READ);
        builder.OutputCustom(m_historyImage,
                             PE_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             PE_STAGE_TRANSFER,
                             PE_ACCESS_TRANSFER_WRITE);
    }

    void TAAPass::ExecutePass(CommandBuffer *cmd)
    {
        Image *taaOutput = m_casSharpeningEnabled ? m_taaResolved : m_displayRT;

        struct TAAConstants
        {
            vec2 resolution;  // Input resolution
            vec2 displaySize; // Output resolution
            vec2 jitter;
            vec2 padding;
        };

        TAAConstants pc{};
        pc.resolution = vec2(m_viewportRT->GetWidth_f(), m_viewportRT->GetHeight_f());
        pc.displaySize = vec2(taaOutput->GetWidth_f(), taaOutput->GetHeight_f());
        pc.jitter = m_jitter;

        cmd->BeginDebugRegion("TAA");

        // If requested (e.g. after model deletion), clear history so stale
        // shadows / objects don't ghost through temporal accumulation.
        if (m_resetHistory)
        {
            m_resetHistory = false;
            cmd->ClearColors({m_historyImage});
            // ClearColors leaves the image in eTransferDstOptimal; transition
            // back to shader-read so the TAA compute shader can sample it.
            ImageBarrierInfo shaderRead{};
            shaderRead.image = m_historyImage;
            shaderRead.layout = PE_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            shaderRead.stageFlags = PE_STAGE_COMPUTE_SHADER;
            shaderRead.accessMask = PE_ACCESS_SHADER_SAMPLED_READ;
            cmd->ImageBarrier(shaderRead);
        }

        // Bind Pipeline & Descriptors
        // Note: BindPipeline auto-binds descriptors if set true (default).
        cmd->BindPipeline(*m_passInfo);

        cmd->SetConstants(pc);
        cmd->PushConstants();

        // Dispatch
        uint32_t groupX = (taaOutput->GetWidth() + 7) / 8;
        uint32_t groupY = (taaOutput->GetHeight() + 7) / 8;
        cmd->Dispatch(groupX, groupY, 1);

        // Copy Display -> History
        ImageBarrierInfo copyBarrierSrc{};
        copyBarrierSrc.image = taaOutput;
        copyBarrierSrc.layout = PE_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        copyBarrierSrc.stageFlags = PE_STAGE_TRANSFER;
        copyBarrierSrc.accessMask = PE_ACCESS_TRANSFER_READ;

        ImageBarrierInfo copyBarrierDst{};
        copyBarrierDst.image = m_historyImage;
        copyBarrierDst.layout = PE_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        copyBarrierDst.stageFlags = PE_STAGE_TRANSFER;
        copyBarrierDst.accessMask = PE_ACCESS_TRANSFER_WRITE;

        cmd->ImageBarrier(copyBarrierSrc);
        cmd->ImageBarrier(copyBarrierDst);

        // Copy Resolved -> History
        cmd->CopyImage(taaOutput, m_historyImage);
        cmd->EndDebugRegion();
    }

    void TAAPass::Resize(uint32_t width, uint32_t height)
    {
        Destroy();
        Init();
        // The active post-process profile may leave velocity unallocated while this pass is disabled.
        if (m_velocityRT)
            UpdateDescriptorSets();
    }

    void TAAPass::Destroy()
    {
        Image::Destroy(m_historyImage);
        Image::Destroy(m_taaResolved);
    }

    void TAAPass::GenerateJitter()
    {
        if (!m_viewportRT || m_jitterPhaseCount <= 0)
            return;

        m_jitterIndex = (m_jitterIndex + 1) % m_jitterPhaseCount;

        // Halton sequence (2, 3)
        // -0.5 to 0.5 range
        float x = Halton(m_jitterIndex + 1, 2) - 0.5f;
        float y = Halton(m_jitterIndex + 1, 3) - 0.5f;

        m_jitter = vec2(x, y);

        // Projection jitter needs to be in clip space (-1 to 1), scaled by pixel size
        // ProjMatrix offset = (2 * jitterX / width, 2 * jitterY / height)
        // Usually jitter is in pixels, e.g. [-0.5, 0.5]

        float width = m_viewportRT->GetWidth_f();
        float height = m_viewportRT->GetHeight_f();

        m_projectionJitter.x = (2.0f * m_jitter.x) / width;
        m_projectionJitter.y = (2.0f * m_jitter.y) / height;
    }
} // namespace pe
