#include "SSAOPass.h"
#include "API/Buffer.h"
#include "API/Command.h"
#include "API/Descriptor.h"
#include "API/Image.h"
#include "API/Pipeline.h"
#include "API/RHI.h"
#include "API/RenderGraph.h"
#include "API/Shader.h"
#include "Camera/Camera.h"
#include "Render/SceneRendererHost.h"
#include "Scene/Scene.h"
#include "Scene/SceneAccess.h"

namespace pe
{
    namespace
    {
        struct SSAOUniformData
        {
            mat4 projection;
            mat4 invProjection;
            mat4 normalsToView;
            vec4 framebuffer;     // AO working (half) res: xy size, zw 1/size
            vec4 fullFramebuffer; // full res (depth/normal/output): xy size, zw 1/size
            vec4 params;          // radius, bias, intensity, power
            vec4 misc;            // sampleCount, _, _, _
        };

        constexpr uint32_t kGroupSize = 8;

        uint32_t DivideRoundUp(uint32_t value, uint32_t divisor)
        {
            return (value + divisor - 1u) / divisor;
        }
    } // namespace

    void SSAOPass::Init()
    {
        if (!m_blurPassInfo)
            m_blurPassInfo = std::make_shared<PassInfo>();
        if (!m_upsamplePassInfo)
            m_upsamplePassInfo = std::make_shared<PassInfo>();

        AcquireTargets();
        m_uniforms.resize(RHII.GetSwapchainImageCount(), nullptr);
    }

    void SSAOPass::AcquireTargets()
    {
        SceneRendererHost *rs = &RequireActiveSceneRendererHost();
        m_ssaoRT = rs->GetRenderTarget("ssao");
        if (!m_ssaoRT)
            m_ssaoRT = rs->CreateRenderTarget("ssao", PE_FORMAT_R8_UNORM);

        m_normalRT = rs->GetRenderTarget("normal");
        m_depth = rs->GetDepthStencilTarget("depthStencil");

        // ssaoRaw/ssaoBlur are internal to this pass and live at HALF the ssao resolution: the
        // heavy occlusion pass and the bilateral denoise both run there (a quarter of the
        // invocations), then a cheap full-res apply writes the registered ssao target. They are
        // owned here (not registered) so they never show as profiler thumbnails and so we control
        // their size directly.
        CreateInternalTargets();
    }

    void SSAOPass::CreateInternalTargets()
    {
        if (!m_ssaoRT)
            return;

        const uint32_t halfW = std::max(1u, (m_ssaoRT->GetWidth() + 1u) / 2u);
        const uint32_t halfH = std::max(1u, (m_ssaoRT->GetHeight() + 1u) / 2u);
        const bool rawOk = m_ssaoRawRT && m_ssaoRawRT->GetWidth() == halfW && m_ssaoRawRT->GetHeight() == halfH;
        const bool blurOk = m_ssaoBlurRT && m_ssaoBlurRT->GetWidth() == halfW && m_ssaoBlurRT->GetHeight() == halfH;
        if (rawOk && blurOk)
            return;

        Image::Destroy(m_ssaoRawRT);
        Image::Destroy(m_ssaoBlurRT);

        auto createHalf = [&](const char *name) -> Image *
        {
            ImageDesc desc{};
            desc.format = PE_FORMAT_R16_SFLOAT;
            desc.width = halfW;
            desc.height = halfH;
            desc.usage = PE_IMAGE_USAGE_SAMPLED | PE_IMAGE_USAGE_STORAGE | PE_IMAGE_USAGE_TRANSFER_DST;
            desc.name = name;
            Image *img = Image::Create(desc);
            img->CreateSRV(PE_IMAGE_VIEW_TYPE_2D);
            img->CreateUAV(PE_IMAGE_VIEW_TYPE_2D, 0);
            return img;
        };

        m_ssaoRawRT = createHalf("ssaoRaw");
        m_ssaoBlurRT = createHalf("ssaoBlur");
    }

    void SSAOPass::UpdatePassInfo()
    {
        m_passInfo->name = "SSAO_pipeline";
        m_passInfo->pCompShader = Shader::Create({.sourcePath = Path::RuntimeAssets + "Shaders/Compute/SSAOCS.hlsl",
                                                  .entryPoint = "mainCS",
                                                  .stage = PE_SHADER_STAGE_COMPUTE,
                                                  .defines = std::vector<Define>{}});
        m_passInfo->Update();

        m_blurPassInfo->name = "SSAOBlur_pipeline";
        m_blurPassInfo->pCompShader = Shader::Create({.sourcePath = Path::RuntimeAssets + "Shaders/Compute/SSAOCS.hlsl",
                                                      .entryPoint = "mainCS",
                                                      .stage = PE_SHADER_STAGE_COMPUTE,
                                                      .defines = std::vector<Define>{Define{"SSAO_BLUR_PASS", "1"}}});
        m_blurPassInfo->Update();

        m_upsamplePassInfo->name = "SSAOUpsample_pipeline";
        m_upsamplePassInfo->pCompShader =
            Shader::Create({.sourcePath = Path::RuntimeAssets + "Shaders/Compute/SSAOCS.hlsl",
                            .entryPoint = "mainCS",
                            .stage = PE_SHADER_STAGE_COMPUTE,
                            .defines = std::vector<Define>{Define{"SSAO_UPSAMPLE_PASS", "1"}}});
        m_upsamplePassInfo->Update();
    }

    void SSAOPass::CreateUniforms(CommandBuffer *cmd)
    {
        (void)cmd;

        m_uniforms.resize(RHII.GetSwapchainImageCount(), nullptr);
        for (auto &uniform : m_uniforms)
        {
            if (uniform)
                continue;

            uniform = Buffer::Create({
                .size = RHII.AlignUniform(sizeof(SSAOUniformData)),
                .usage = PE_BUFFER_USAGE_UNIFORM_BUFFER,
                .memoryUsage = PE_MEMORY_USAGE_CPU_TO_GPU,
                .name = "SSAO_uniform_buffer",
            });
            uniform->Map();
            uniform->Zero();
            uniform->Flush();
            uniform->Unmap();
        }

        UpdateDescriptorSets();
    }

    void SSAOPass::UpdateDescriptorSets()
    {
        if (!m_depth || !m_normalRT || !m_ssaoRawRT || !m_ssaoBlurRT || !m_ssaoRT ||
            m_uniforms.size() != RHII.GetSwapchainImageCount())
            return;

        for (uint32_t i = 0; i < RHII.GetSwapchainImageCount(); ++i)
        {
            if (!m_uniforms[i])
                continue;

            auto &aoDescriptors = m_passInfo->GetDescriptors(i);
            if (!aoDescriptors.empty())
            {
                Descriptor *dset = aoDescriptors[0];
                dset->SetImageView(0, m_depth->GetSRV());
                dset->SetImageView(1, m_normalRT->GetSRV());
                dset->SetImageView(2, m_ssaoRawRT->GetUAV(0));
                dset->SetBuffer(3, m_uniforms[i]);
                dset->Update();
            }

            // Denoise: half-res raw AO -> half-res blurred AO.
            if (m_blurPassInfo)
            {
                auto &blurDescriptors = m_blurPassInfo->GetDescriptors(i);
                if (!blurDescriptors.empty())
                {
                    Descriptor *dset = blurDescriptors[0];
                    dset->SetImageView(0, m_ssaoRawRT->GetUAV(0));
                    dset->SetImageView(1, m_depth->GetSRV());
                    dset->SetImageView(2, m_ssaoBlurRT->GetUAV(0));
                    dset->SetBuffer(3, m_uniforms[i]);
                    dset->Update();
                }
            }

            // Apply: half-res blurred AO -> full-res ssao (joint bilateral upsample).
            if (m_upsamplePassInfo)
            {
                auto &upsampleDescriptors = m_upsamplePassInfo->GetDescriptors(i);
                if (!upsampleDescriptors.empty())
                {
                    Descriptor *dset = upsampleDescriptors[0];
                    dset->SetImageView(0, m_ssaoBlurRT->GetUAV(0));
                    dset->SetImageView(1, m_depth->GetSRV());
                    dset->SetImageView(2, m_ssaoRT->GetUAV(0));
                    dset->SetBuffer(3, m_uniforms[i]);
                    dset->Update();
                }
            }
        }
    }

    void SSAOPass::Update()
    {
        auto &gSettings = ActivePostProcessProfile();
        if (!gSettings.ssao || m_uniforms.empty() || !m_ssaoRT || !m_ssaoRawRT)
            return;

        Camera *camera = GetActiveScene()->GetActiveCamera();
        m_proj = camera->GetProjection();
        m_invProj = camera->GetInvProjection();

        m_normalsToView = camera->GetView();

        const uint32_t frame = RHII.GetFrameIndex();
        if (frame >= m_uniforms.size() || !m_uniforms[frame])
            return;

        const float halfW = std::max(m_ssaoRawRT->GetWidth_f(), 1.0f);
        const float halfH = std::max(m_ssaoRawRT->GetHeight_f(), 1.0f);
        const float fullW = std::max(m_ssaoRT->GetWidth_f(), 1.0f);
        const float fullH = std::max(m_ssaoRT->GetHeight_f(), 1.0f);

        SSAOUniformData data{};
        data.projection = m_proj;
        data.invProjection = m_invProj;
        data.normalsToView = m_normalsToView;
        data.framebuffer = vec4(halfW, halfH, 1.0f / halfW, 1.0f / halfH);
        data.fullFramebuffer = vec4(fullW, fullH, 1.0f / fullW, 1.0f / fullH);
        data.params = vec4(gSettings.ssao_radius, gSettings.ssao_bias, gSettings.ssao_intensity, gSettings.ssao_power);
        const float sampleCount = static_cast<float>(std::clamp(gSettings.ssao_samples, 1, 64));
        data.misc = vec4(sampleCount, 0.0f, 0.0f, 0.0f);

        BufferRange range{};
        range.data = &data;
        range.size = sizeof(data);
        range.offset = 0;
        m_uniforms[frame]->Copy(1, &range, false);
    }

    void SSAOPass::DeclareInputs(RGBuilder &builder)
    {
        builder.ReadCompute(m_normalRT);
        builder.ReadCompute(m_depth);
        builder.WriteCompute(m_ssaoRawRT);
        builder.WriteCompute(m_ssaoBlurRT);
        builder.WriteCompute(m_ssaoRT);
    }

    void SSAOPass::DeclareOutputs(RGBuilder &builder)
    {
        builder.OutputCustom(m_ssaoRT,
                             PE_IMAGE_LAYOUT_GENERAL,
                             PE_STAGE_COMPUTE_SHADER,
                             PE_ACCESS_SHADER_WRITE);
    }

    void SSAOPass::ExecutePass(CommandBuffer *cmd)
    {
        if (!m_ssaoRT || !m_ssaoRawRT || !m_ssaoBlurRT || !m_depth || !m_normalRT || !m_blurPassInfo ||
            !m_upsamplePassInfo)
            return;

        // Occlusion + denoise run at half resolution (ssaoRaw/ssaoBlur); only the apply is full-res.
        const uint32_t halfGroupX = DivideRoundUp(m_ssaoRawRT->GetWidth(), kGroupSize);
        const uint32_t halfGroupY = DivideRoundUp(m_ssaoRawRT->GetHeight(), kGroupSize);
        const uint32_t fullGroupX = DivideRoundUp(m_ssaoRT->GetWidth(), kGroupSize);
        const uint32_t fullGroupY = DivideRoundUp(m_ssaoRT->GetHeight(), kGroupSize);

        MemoryBarrierInfo uavBarrier{};
        uavBarrier.srcAccessMask = PE_ACCESS_SHADER_WRITE;
        uavBarrier.dstAccessMask = PE_ACCESS_SHADER_READ;
        uavBarrier.srcStageMask = PE_STAGE_COMPUTE_SHADER;
        uavBarrier.dstStageMask = PE_STAGE_COMPUTE_SHADER;

        cmd->BeginDebugRegion("SSAOPass");
        cmd->BindPipeline(*m_passInfo);
        cmd->Dispatch(halfGroupX, halfGroupY, 1);

        cmd->MemoryBarrier(uavBarrier);

        cmd->BindPipeline(*m_blurPassInfo);
        cmd->Dispatch(halfGroupX, halfGroupY, 1);

        cmd->MemoryBarrier(uavBarrier);

        cmd->BindPipeline(*m_upsamplePassInfo);
        cmd->Dispatch(fullGroupX, fullGroupY, 1);
        cmd->EndDebugRegion();
    }

    void SSAOPass::Resize(uint32_t width, uint32_t height)
    {
        (void)width;
        (void)height;

        AcquireTargets();
        UpdateDescriptorSets();
    }

    void SSAOPass::Destroy()
    {
        for (auto &uniform : m_uniforms)
            Buffer::Destroy(uniform);
        m_uniforms.clear();

        if (m_passInfo)
            Shader::Destroy(m_passInfo->pCompShader);
        if (m_blurPassInfo)
        {
            Shader::Destroy(m_blurPassInfo->pCompShader);
            m_blurPassInfo.reset();
        }
        if (m_upsamplePassInfo)
        {
            Shader::Destroy(m_upsamplePassInfo->pCompShader);
            m_upsamplePassInfo.reset();
        }

        Image::Destroy(m_ssaoRawRT); // pass-owned half-res targets
        Image::Destroy(m_ssaoBlurRT);

        if (SceneRendererHost *rs = GetActiveSceneRendererHost())
            rs->DestroyRenderTarget("ssao");

        m_ssaoRT = nullptr;
        m_ssaoRawRT = nullptr;
        m_ssaoBlurRT = nullptr;
        m_normalRT = nullptr;
        m_depth = nullptr;
    }

    std::vector<PassInfo *> SSAOPass::GetPassInfos() noexcept
    {
        return {m_passInfo.get(), m_blurPassInfo.get(), m_upsamplePassInfo.get()};
    }
} // namespace pe
