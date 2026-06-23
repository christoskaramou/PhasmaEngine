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
            vec4 framebuffer;
            vec4 params;
        };

        constexpr uint32_t kGroupSize = 8;
        constexpr float kSsaoRadius = 0.5f;
        constexpr float kSsaoBias = 0.025f;
        constexpr float kSsaoIntensity = 0.5f;
        constexpr float kSsaoPower = 1.0f;

        uint32_t DivideRoundUp(uint32_t value, uint32_t divisor)
        {
            return (value + divisor - 1u) / divisor;
        }
    } // namespace

    void SSAOPass::Init()
    {
        if (!m_blurPassInfo)
            m_blurPassInfo = std::make_shared<PassInfo>();

        AcquireTargets();
        m_uniforms.resize(RHII.GetSwapchainImageCount(), nullptr);
    }

    void SSAOPass::AcquireTargets()
    {
        SceneRendererHost *rs = &RequireActiveSceneRendererHost();
        m_ssaoRT = rs->GetRenderTarget("ssao");
        if (!m_ssaoRT)
            m_ssaoRT = rs->CreateRenderTarget("ssao", PE_FORMAT_R8_UNORM);

        m_ssaoRawRT = rs->GetRenderTarget("ssaoRaw");
        if (!m_ssaoRawRT)
            m_ssaoRawRT = rs->CreateRenderTarget("ssaoRaw", PE_FORMAT_R16_SFLOAT);

        m_normalRT = rs->GetRenderTarget("normal");
        m_depth = rs->GetDepthStencilTarget("depthStencil");
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
        if (!m_depth || !m_normalRT || !m_ssaoRawRT || !m_ssaoRT || m_uniforms.size() != RHII.GetSwapchainImageCount())
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

            if (m_blurPassInfo)
            {
                auto &blurDescriptors = m_blurPassInfo->GetDescriptors(i);
                if (!blurDescriptors.empty())
                {
                    Descriptor *dset = blurDescriptors[0];
                    dset->SetImageView(0, m_ssaoRawRT->GetUAV(0));
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
        auto &gSettings = Settings::Get<GlobalSettings>();
        if (!gSettings.ssao || m_uniforms.empty() || !m_ssaoRT)
            return;

        Camera *camera = GetActiveScene()->GetActiveCamera();
        m_proj = camera->GetProjection();
        m_invProj = camera->GetInvProjection();

        m_normalsToView = camera->GetView();

        const uint32_t frame = RHII.GetFrameIndex();
        if (frame >= m_uniforms.size() || !m_uniforms[frame])
            return;

        const float width = std::max(m_ssaoRT->GetWidth_f(), 1.0f);
        const float height = std::max(m_ssaoRT->GetHeight_f(), 1.0f);

        SSAOUniformData data{};
        data.projection = m_proj;
        data.invProjection = m_invProj;
        data.normalsToView = m_normalsToView;
        data.framebuffer = vec4(width, height, 1.0f / width, 1.0f / height);
        data.params = vec4(kSsaoRadius, kSsaoBias, kSsaoIntensity, kSsaoPower);

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
        if (!m_ssaoRT || !m_ssaoRawRT || !m_depth || !m_normalRT || !m_blurPassInfo)
            return;

        const uint32_t groupX = DivideRoundUp(m_ssaoRT->GetWidth(), kGroupSize);
        const uint32_t groupY = DivideRoundUp(m_ssaoRT->GetHeight(), kGroupSize);

        MemoryBarrierInfo uavBarrier{};
        uavBarrier.srcAccessMask = PE_ACCESS_SHADER_WRITE;
        uavBarrier.dstAccessMask = PE_ACCESS_SHADER_READ;
        uavBarrier.srcStageMask = PE_STAGE_COMPUTE_SHADER;
        uavBarrier.dstStageMask = PE_STAGE_COMPUTE_SHADER;

        cmd->BeginDebugRegion("SSAOPass");
        cmd->BindPipeline(*m_passInfo);
        cmd->Dispatch(groupX, groupY, 1);

        cmd->MemoryBarrier(uavBarrier);

        cmd->BindPipeline(*m_blurPassInfo);
        cmd->Dispatch(groupX, groupY, 1);
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

        if (SceneRendererHost *rs = GetActiveSceneRendererHost())
        {
            rs->DestroyRenderTarget("ssaoRaw");
            rs->DestroyRenderTarget("ssao");
        }

        m_ssaoRT = nullptr;
        m_ssaoRawRT = nullptr;
        m_normalRT = nullptr;
        m_depth = nullptr;
    }

    std::vector<PassInfo *> SSAOPass::GetPassInfos() noexcept
    {
        return {m_passInfo.get(), m_blurPassInfo.get()};
    }
} // namespace pe
